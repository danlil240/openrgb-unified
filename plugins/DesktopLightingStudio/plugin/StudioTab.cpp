/*---------------------------------------------------------*\
|| StudioTab.cpp                                             |
||                                                           |
||   Studio tab widget — hosts a QQuickWidget running the   |
||   desk scene (Stage 1) plus device-inspection controls   |
||   kept from the Stage 0 probe for calibration.           |
||                                                           |
||   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "StudioTab.h"
#include "SceneBridge.h"
#include "../inputs/ScreenSampler.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWidget>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSignalBlocker>
#include <QSlider>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

#include <chrono>
#include <thread>
#include <QMutex>
#include <QMutexLocker>

#include "OpenRGBPluginInterface.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

/*---------------------------------------------------------*\
|| Directory containing this plugin DLL. Packaged QML        |
|| modules are deployed beside it in a "qml" subfolder.      |
\*---------------------------------------------------------*/
static QString PluginDirectory()
{
#ifdef Q_OS_WIN
    HMODULE module = nullptr;
    if(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                        | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          reinterpret_cast<LPCWSTR>(&PluginDirectory), &module))
    {
        wchar_t path[MAX_PATH];
        if(GetModuleFileNameW(module, path, MAX_PATH))
        {
            return QFileInfo(QString::fromWCharArray(path)).absolutePath();
        }
    }
#endif
    return QString();
}

/* Serializes this tab's controller writes (flash/measure). The bridge's
   live push uses its own mutex; stop other effect writers first. */
static QMutex g_io_mutex;

static const char* GraphicsApiName(QSGRendererInterface::GraphicsApi api)
{
    switch(api)
    {
    case QSGRendererInterface::Software:   return "Software";
    case QSGRendererInterface::OpenVG:     return "OpenVG";
    case QSGRendererInterface::OpenGL:     return "OpenGL";
    case QSGRendererInterface::Direct3D11: return "Direct3D11";
    case QSGRendererInterface::Vulkan:     return "Vulkan";
    case QSGRendererInterface::Metal:      return "Metal";
    case QSGRendererInterface::Direct3D12: return "Direct3D12";
    default:                               return "Unknown";
    }
}

StudioTab::StudioTab(OpenRGBPluginAPIInterface* plugin_api, QWidget* parent)
    : QWidget(parent)
    , api(plugin_api)
{
    bridge = new studio::SceneBridge(plugin_api, this);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    /*-----------------------------------------------------*\
    | 3D desk viewport                                      |
    \*-----------------------------------------------------*/
    quick_widget = new QQuickWidget(this);
    quick_widget->setResizeMode(QQuickWidget::SizeRootObjectToView);
    quick_widget->setMinimumHeight(320);
    quick_widget->rootContext()->setContextProperty("bridge", bridge);

    const QString plugin_dir = PluginDirectory();
    if(!plugin_dir.isEmpty())
    {
        quick_widget->engine()->addImportPath(plugin_dir + "/qml");
    }

    /* Prefer the loose QML file beside the plugin (editable without a
       rebuild); fall back to the embedded copy. */
    const QString file_scene = plugin_dir + "/ui/StudioScene.qml";
    if(QFileInfo::exists(file_scene))
    {
        quick_widget->setSource(QUrl::fromLocalFile(file_scene));
    }
    else
    {
        quick_widget->setSource(QUrl(QStringLiteral("qrc:/studio/StudioScene.qml")));
    }

    /*-----------------------------------------------------*\
    | Scene control row                                     |
    \*-----------------------------------------------------*/
    QWidget*     scene_bar  = new QWidget(this);
    QHBoxLayout* scene_row  = new QHBoxLayout(scene_bar);
    scene_row->setContentsMargins(8, 4, 8, 4);

    selection_label = new QLabel(QStringLiteral("(nothing selected)"), scene_bar);
    selection_label->setMinimumWidth(220);

    color_btn = new QPushButton(QStringLiteral("Color…"), scene_bar);
    color_btn->setEnabled(false);

    brightness_slider = new QSlider(Qt::Horizontal, scene_bar);
    brightness_slider->setRange(0, 100);
    brightness_slider->setValue(100);
    brightness_slider->setMaximumWidth(120);

    live_check = new QCheckBox(QStringLiteral("Live output"), scene_bar);
    QCheckBox* ghost_check = new QCheckBox(QStringLiteral("Ghost case"), scene_bar);

    QPushButton* undo_btn = new QPushButton(QStringLiteral("Undo"), scene_bar);
    QPushButton* redo_btn = new QPushButton(QStringLiteral("Redo"), scene_bar);
    QPushButton* save_btn = new QPushButton(QStringLiteral("Save"), scene_bar);
    QPushButton* load_btn = new QPushButton(QStringLiteral("Load"), scene_bar);
    QPushButton* reset_btn = new QPushButton(QStringLiteral("Reset"), scene_bar);

    scene_row->addWidget(selection_label);
    scene_row->addWidget(color_btn);
    scene_row->addWidget(new QLabel(QStringLiteral("Brightness"), scene_bar));
    scene_row->addWidget(brightness_slider);
    scene_row->addWidget(live_check);
    scene_row->addWidget(ghost_check);
    scene_row->addStretch(1);
    scene_row->addWidget(undo_btn);
    scene_row->addWidget(redo_btn);
    scene_row->addWidget(save_btn);
    scene_row->addWidget(load_btn);
    scene_row->addWidget(reset_btn);

    /*-----------------------------------------------------*\
    | Scene strip (Stage 2) — preset cards + playback        |
    \*-----------------------------------------------------*/
    QWidget*     fx_bar = new QWidget(this);
    QHBoxLayout* fx_row = new QHBoxLayout(fx_bar);
    fx_row->setContentsMargins(8, 4, 8, 4);

    fx_row->addWidget(new QLabel(QStringLiteral("Scene"), fx_bar));

    preset_group = new QButtonGroup(fx_bar);
    preset_group->setExclusive(true);
    const QVariantList presets = bridge->presetList();
    for(int i = 0; i < presets.size(); i++)
    {
        const QVariantMap p = presets[i].toMap();
        QPushButton* card = new QPushButton(p["name"].toString(), fx_bar);
        card->setCheckable(true);
        card->setToolTip(p["description"].toString());
        card->setProperty("preset_id", p["id"]);
        preset_group->addButton(card, i);
        fx_row->addWidget(card);
    }

    play_btn  = new QPushButton(QStringLiteral("Play"), fx_bar);
    stop_btn  = new QPushButton(QStringLiteral("Stop"), fx_bar);
    remix_btn = new QPushButton(QStringLiteral("Remix"), fx_bar);
    remix_btn->setToolTip(QStringLiteral("Re-roll this preset's random choices (seeded, reproducible)"));

    fx_row->addWidget(play_btn);
    fx_row->addWidget(stop_btn);
    fx_row->addWidget(remix_btn);
    fx_row->addStretch(1);

    fx_row->addWidget(new QLabel(QStringLiteral("Speed"), fx_bar));
    speed_slider = new QSlider(Qt::Horizontal, fx_bar);
    speed_slider->setRange(10, 400);
    speed_slider->setValue(100);
    speed_slider->setMaximumWidth(110);
    speed_label = new QLabel(QStringLiteral("100%"), fx_bar);
    speed_label->setMinimumWidth(40);
    fx_row->addWidget(speed_slider);
    fx_row->addWidget(speed_label);

    fx_row->addWidget(new QLabel(QStringLiteral("Intensity"), fx_bar));
    intensity_slider = new QSlider(Qt::Horizontal, fx_bar);
    intensity_slider->setRange(0, 100);
    intensity_slider->setValue(100);
    intensity_slider->setMaximumWidth(110);
    intensity_label = new QLabel(QStringLiteral("100%"), fx_bar);
    intensity_label->setMinimumWidth(40);
    fx_row->addWidget(intensity_slider);
    fx_row->addWidget(intensity_label);

    /*-----------------------------------------------------*\
    | Inputs row (Stage 3) — reactive signal sources         |
    \*-----------------------------------------------------*/
    QWidget*     inputs_bar = new QWidget(this);
    QHBoxLayout* inputs_row = new QHBoxLayout(inputs_bar);
    inputs_row->setContentsMargins(8, 4, 8, 4);

    inputs_row->addWidget(new QLabel(QStringLiteral("Inputs"), inputs_bar));

    audio_check = new QCheckBox(QStringLiteral("Audio"), inputs_bar);
    audio_check->setToolTip(QStringLiteral(
        "WASAPI loopback on the default output — onsets drive shockwave rings"));
    inputs_row->addWidget(audio_check);

    inputs_row->addWidget(new QLabel(QStringLiteral("Sens"), inputs_bar));
    sens_slider = new QSlider(Qt::Horizontal, inputs_bar);
    sens_slider->setRange(25, 200);
    sens_slider->setValue(bridge->audioSensitivityPct());
    sens_slider->setMaximumWidth(90);
    sens_label = new QLabel(QStringLiteral("%1%").arg(bridge->audioSensitivityPct()), inputs_bar);
    sens_label->setMinimumWidth(38);
    inputs_row->addWidget(sens_slider);
    inputs_row->addWidget(sens_label);

    key_check = new QCheckBox(QStringLiteral("Keys"), inputs_bar);
    key_check->setToolTip(QStringLiteral(
        "Low-level keyboard hook — key presses spawn ripples at the mapped key"));
    inputs_row->addWidget(key_check);

    screen_check = new QCheckBox(QStringLiteral("Screen"), inputs_bar);
    screen_check->setToolTip(QStringLiteral(
        "Sample the display — ambient colors wash over the setup"));
    inputs_row->addWidget(screen_check);

    screen_combo = new QComboBox(inputs_bar);
    screen_combo->setMinimumWidth(140);
    inputs_row->addWidget(screen_combo);

    inputs_row->addStretch(1);
    inputs_row->addWidget(new QLabel(QStringLiteral("Decay"), inputs_bar));
    decay_slider = new QSlider(Qt::Horizontal, inputs_bar);
    decay_slider->setRange(50, 300);
    decay_slider->setValue(bridge->rippleDecayPct());
    decay_slider->setMaximumWidth(90);
    decay_slider->setToolTip(QStringLiteral("Ripple ring lifetime — higher decays faster"));
    decay_label = new QLabel(QStringLiteral("%1%").arg(bridge->rippleDecayPct()), inputs_bar);
    decay_label->setMinimumWidth(38);
    inputs_row->addWidget(decay_slider);
    inputs_row->addWidget(decay_label);

    /*-----------------------------------------------------*\
    | Device-inspection bar (Stage 0 measurements)          |
    \*-----------------------------------------------------*/
    QWidget*        bar     = new QWidget(this);
    QHBoxLayout*    bar_row = new QHBoxLayout(bar);
    bar_row->setContentsMargins(8, 4, 8, 4);

    controller_combo  = new QComboBox(bar);
    zone_combo        = new QComboBox(bar);
    QPushButton* refresh_btn = new QPushButton(QStringLiteral("Refresh"), bar);
    QPushButton* flash_btn   = new QPushButton(QStringLiteral("Flash zone 4s"), bar);
    QPushButton* measure_btn = new QPushButton(QStringLiteral("Measure write latency"), bar);
    QPushButton* bindings_btn = new QPushButton(QStringLiteral("Bindings"), bar);

    controller_combo->setMinimumWidth(260);
    zone_combo->setMinimumWidth(180);

    bar_row->addWidget(controller_combo);
    bar_row->addWidget(zone_combo);
    bar_row->addWidget(refresh_btn);
    bar_row->addWidget(flash_btn);
    bar_row->addWidget(measure_btn);
    bar_row->addWidget(bindings_btn);
    bar_row->addStretch(1);

    results_box = new QPlainTextEdit(this);
    results_box->setReadOnly(true);
    results_box->setMaximumHeight(130);
    results_box->setStyleSheet("QPlainTextEdit { background: #141418; color: #b8b8c2; font-family: Consolas, monospace; }");

    status_label = new QLabel(this);
    status_label->setStyleSheet("QLabel { background: #18181c; color: #9a9aa5; padding: 4px 8px; }");

    layout->addWidget(quick_widget, 1);
    layout->addWidget(scene_bar);
    layout->addWidget(fx_bar);
    layout->addWidget(inputs_bar);
    layout->addWidget(bar);
    layout->addWidget(results_box);
    layout->addWidget(status_label);

    /*-----------------------------------------------------*\
    | Scene wiring                                          |
    \*-----------------------------------------------------*/
    connect(color_btn, &QPushButton::clicked, this, [this]() { PickColor(); });

    connect(brightness_slider, &QSlider::valueChanged,
            bridge, &studio::SceneBridge::setBrightnessPct);
    connect(live_check, &QCheckBox::toggled,
            bridge, &studio::SceneBridge::setLive);
    connect(ghost_check, &QCheckBox::toggled,
            bridge, &studio::SceneBridge::setCaseGhost);
    connect(undo_btn, &QPushButton::clicked, bridge, &studio::SceneBridge::undo);
    connect(redo_btn, &QPushButton::clicked, bridge, &studio::SceneBridge::redo);
    connect(save_btn, &QPushButton::clicked, bridge, &studio::SceneBridge::saveScene);
    connect(load_btn, &QPushButton::clicked, bridge, &studio::SceneBridge::loadScene);
    connect(reset_btn, &QPushButton::clicked, bridge, &studio::SceneBridge::resetScene);

    /*-----------------------------------------------------*\
    | Scene strip wiring                                    |
    \*-----------------------------------------------------*/
    connect(preset_group, &QButtonGroup::idClicked, this, [this](int id)
    {
        const QVariantList presets = bridge->presetList();
        if(id >= 0 && id < presets.size())
        {
            bridge->playPreset(presets[id].toMap()["id"].toString());
        }
    });

    connect(play_btn, &QPushButton::clicked, this, [this]()
    {
        if(bridge->playing())
        {
            bridge->setPlaying(false);
        }
        else if(bridge->activePreset().isEmpty())
        {
            const QVariantList presets = bridge->presetList();
            if(!presets.isEmpty())
            {
                bridge->playPreset(presets.first().toMap()["id"].toString());
            }
        }
        else
        {
            bridge->setPlaying(true);
        }
    });

    connect(stop_btn,  &QPushButton::clicked, bridge, &studio::SceneBridge::stopEffect);
    connect(remix_btn, &QPushButton::clicked, bridge, &studio::SceneBridge::remix);

    connect(speed_slider, &QSlider::valueChanged,
            bridge, &studio::SceneBridge::setEffectSpeedPct);
    connect(intensity_slider, &QSlider::valueChanged,
            bridge, &studio::SceneBridge::setEffectIntensityPct);

    auto sync_fx = [this]()
    {
        const QString active = bridge->activePreset();
        const bool    is_playing = bridge->playing();
        for(QAbstractButton* card : preset_group->buttons())
        {
            card->setChecked(is_playing && card->property("preset_id").toString() == active);
        }
        play_btn->setText(is_playing ? QStringLiteral("Pause") : QStringLiteral("Play"));
        remix_btn->setEnabled(!active.isEmpty());

        /* Sliders: block signals so programmatic sync can't re-enter. */
        QSignalBlocker block_speed(speed_slider);
        QSignalBlocker block_intensity(intensity_slider);
        speed_slider->setValue(bridge->effectSpeedPct());
        intensity_slider->setValue(bridge->effectIntensityPct());
        speed_label->setText(QStringLiteral("%1%").arg(bridge->effectSpeedPct()));
        intensity_label->setText(QStringLiteral("%1%").arg(bridge->effectIntensityPct()));
    };

    connect(bridge, &studio::SceneBridge::playingChanged,      this, sync_fx);
    connect(bridge, &studio::SceneBridge::presetChanged,       this, sync_fx);
    connect(bridge, &studio::SceneBridge::effectParamsChanged, this, sync_fx);
    sync_fx();

    /*-----------------------------------------------------*\
    | Inputs row wiring                                     |
    \*-----------------------------------------------------*/
    screen_combo->addItems(studio::ScreenSampler::DisplayNames());
    screen_combo->setCurrentIndex(bridge->screenIndex());

    connect(audio_check, &QCheckBox::toggled,
            bridge, &studio::SceneBridge::setAudioInput);
    connect(key_check, &QCheckBox::toggled,
            bridge, &studio::SceneBridge::setKeyInput);
    connect(screen_check, &QCheckBox::toggled,
            bridge, &studio::SceneBridge::setScreenInput);
    connect(screen_combo, &QComboBox::currentIndexChanged,
            bridge, &studio::SceneBridge::setScreenIndex);
    connect(sens_slider, &QSlider::valueChanged,
            bridge, &studio::SceneBridge::setAudioSensitivityPct);
    connect(decay_slider, &QSlider::valueChanged,
            bridge, &studio::SceneBridge::setRippleDecayPct);

    auto sync_inputs = [this]()
    {
        QSignalBlocker b_audio(audio_check);
        QSignalBlocker b_key(key_check);
        QSignalBlocker b_screen(screen_check);
        QSignalBlocker b_combo(screen_combo);
        QSignalBlocker b_sens(sens_slider);
        QSignalBlocker b_decay(decay_slider);
        audio_check->setChecked(bridge->audioInput());
        key_check->setChecked(bridge->keyInput());
        screen_check->setChecked(bridge->screenInput());
        if(bridge->screenIndex() < screen_combo->count())
        {
            screen_combo->setCurrentIndex(bridge->screenIndex());
        }
        sens_slider->setValue(bridge->audioSensitivityPct());
        decay_slider->setValue(bridge->rippleDecayPct());
        sens_label->setText(QStringLiteral("%1%").arg(bridge->audioSensitivityPct()));
        decay_label->setText(QStringLiteral("%1%").arg(bridge->rippleDecayPct()));
    };
    connect(bridge, &studio::SceneBridge::inputsChanged, this, sync_inputs);
    sync_inputs();

    connect(bridge, &studio::SceneBridge::selectionChanged, this, [this]()
    {
        const QVariantMap info = bridge->objectInfo(bridge->selectedId());
        if(info.isEmpty())
        {
            selection_label->setText(QStringLiteral("(nothing selected)"));
            color_btn->setEnabled(false);
            return;
        }
        const bool writable = info.value("writable").toBool();
        QString note;
        if(!info.value("bound").toBool())
        {
            note = QStringLiteral(" — ") + info.value("reason").toString();
        }
        else if(!writable)
        {
            note = QStringLiteral(" — unverified, writes off");
        }
        selection_label->setText(info.value("label").toString() + note);
        color_btn->setEnabled(info.value("kind").toString() != "decor");
    });

    connect(bridge, &studio::SceneBridge::statusMessage,
            this, [this](const QString& line) { AppendResult(line); });

    connect(bridge, &studio::SceneBridge::statusChanged, this, [this]()
    {
        status_label->setText(bridge->statusText());
    });

    /*-----------------------------------------------------*\
    | Device-inspection wiring                              |
    \*-----------------------------------------------------*/
    connect(refresh_btn, &QPushButton::clicked, this, [this]()
    {
        RefreshControllers();
        bridge->refreshDevices();
    });

    connect(controller_combo, &QComboBox::currentIndexChanged, this, [this](int)
    {
        zone_combo->clear();
        const int idx = controller_combo->currentData().toInt();
        if(idx < 0 || idx >= (int)controllers.size())
        {
            return;
        }
        RGBControllerInterface* ctrl = controllers[idx];
        for(unsigned int z = 0; z < ctrl->GetZoneCount(); z++)
        {
            zone_combo->addItem(QStringLiteral("%0: %1 (%2 LEDs)")
                                .arg(z)
                                .arg(QString::fromStdString(ctrl->GetZoneName(z)))
                                .arg(ctrl->GetZoneLEDsCount(z)),
                                (int)z);
        }
    });

    connect(flash_btn, &QPushButton::clicked, this, [this]() { FlashSelectedZone(); });
    connect(measure_btn, &QPushButton::clicked, this, [this]() { MeasureWriteLatency(); });
    connect(bindings_btn, &QPushButton::clicked, this, [this]()
    {
        AppendResult(bridge->bindingReport());
    });

    /*-----------------------------------------------------*\
    | QML status reporting                                  |
    \*-----------------------------------------------------*/
    auto report_qml = [this](QQuickWidget::Status status)
    {
        if(status == QQuickWidget::Ready && quick_widget->quickWindow() != nullptr)
        {
            QSGRendererInterface* rhi = quick_widget->quickWindow()->rendererInterface();
            const char* api_name = rhi ? GraphicsApiName(rhi->graphicsApi()) : "Unknown";
            status_label->setText(QStringLiteral("Studio ready - RHI backend: %1 - drag to orbit, scroll to zoom, click a part")
                                  .arg(QString::fromLatin1(api_name)));
        }
        else if(status == QQuickWidget::Error)
        {
            QStringList lines;
            for(const QQmlError& error : quick_widget->errors())
            {
                lines << error.toString();
            }
            status_label->setText("QML load failed: " + lines.join(" | "));
        }
    };

    connect(quick_widget, &QQuickWidget::sceneGraphError, this,
            [this](QQuickWindow::SceneGraphError error, const QString& message)
    {
        AppendResult(QStringLiteral("sceneGraphError %1: %2").arg((int)error).arg(message));
    });

    if(quick_widget->status() == QQuickWidget::Error)
    {
        report_qml(QQuickWidget::Error);
    }
    else
    {
        connect(quick_widget, &QQuickWidget::statusChanged, this, report_qml);
    }

    /* Restore the saved scene if one exists. */
    bridge->loadScene();

    RefreshControllers();
}

void StudioTab::OnDevicesChanged()
{
    /* Only re-resolve bindings — repopulating the inspection bar on
       every resource signal would wipe the results box. */
    bridge->refreshDevices();
}

void StudioTab::PickColor()
{
    const QColor color = QColorDialog::getColor(Qt::white, this,
                                                QStringLiteral("Object color"));
    if(color.isValid())
    {
        bridge->setPaintColor(color);
        bridge->setSelectedColor(color);
    }
}

void StudioTab::RefreshControllers()
{
    controllers.clear();
    controller_combo->clear();
    zone_combo->clear();
    results_box->clear();

    if(api == nullptr)
    {
        AppendResult(QStringLiteral("plugin API not available"));
        return;
    }

    controllers = api->GetRGBControllers();

    for(size_t i = 0; i < controllers.size(); i++)
    {
        RGBControllerInterface* ctrl = controllers[i];
        controller_combo->addItem(QStringLiteral("[%0] %1 - %2")
                                  .arg(i)
                                  .arg(QString::fromStdString(ctrl->GetName()))
                                  .arg(QString::fromStdString(api->DeviceTypeToString(ctrl->GetDeviceType()))),
                                  (int)i);

        AppendResult(QStringLiteral("[%0] %1 | type=%2 | zones=%3 | leds=%4 | serial=%5 | loc=%6")
                     .arg(i)
                     .arg(QString::fromStdString(ctrl->GetName()))
                     .arg(QString::fromStdString(api->DeviceTypeToString(ctrl->GetDeviceType())))
                     .arg(ctrl->GetZoneCount())
                     .arg(ctrl->GetLEDCount())
                     .arg(QString::fromStdString(ctrl->GetSerial()))
                     .arg(QString::fromStdString(ctrl->GetLocation())));
    }

    if(controllers.empty())
    {
        AppendResult(QStringLiteral("no controllers detected - run OpenRGB elevated for full detection"));
    }
}

/*---------------------------------------------------------*\
|| Flash a low-brightness identification pattern on the      |
|| selected zone (red/black alternating, ~4 s).              |
\*---------------------------------------------------------*/
void StudioTab::FlashSelectedZone()
{
    const int ci = controller_combo->currentData().toInt();
    const int zi = zone_combo->currentData().toInt();
    if(ci < 0 || zi < 0 || ci >= (int)controllers.size())
    {
        return;
    }

    RGBControllerInterface* ctrl = controllers[ci];
    const int zone_idx = zi;

    status_label->setText(QStringLiteral("flashing %1 / %2 ...")
                          .arg(QString::fromStdString(ctrl->GetName()))
                          .arg(QString::fromStdString(ctrl->GetZoneName(zone_idx))));

    std::thread([this, ctrl, zone_idx]()
    {
        bridge->pausePushes();
        {
            QMutexLocker io_lock(&g_io_mutex);
            const unsigned int leds = ctrl->GetZoneLEDsCount(zone_idx);
            for(int i = 0; i < 8; i++)
            {
                const RGBColor color = (i % 2 == 0) ? ToRGBColor(64, 0, 0) : 0;
                for(unsigned int l = 0; l < leds; l++)
                {
                    ctrl->SetColor(ctrl->GetZoneStartIndex(zone_idx) + l, color);
                }
                ctrl->UpdateZoneLEDs(zone_idx);
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
        }
        bridge->resumePushes();
        QMetaObject::invokeMethod(status_label, "setText", Qt::QueuedConnection,
                                  Q_ARG(QString, QStringLiteral("flash done")));
    }).detach();
}

/*---------------------------------------------------------*\
|| Measure per-zone UpdateZoneLEDs() wall time on a worker   |
|| thread; reports avg/max ms and implied max update rate.   |
|| Zones not already in a per-LED color mode are switched    |
|| into one for the measurement (mirroring EnsurePerLedMode) |
|| and the previous mode is restored afterwards. Zones with |
|| no per-LED mode at all are reported with the active mode. |
\*---------------------------------------------------------*/
void StudioTab::MeasureWriteLatency()
{
    if(api == nullptr || controllers.empty())
    {
        return;
    }

    status_label->setText(QStringLiteral("measuring write latency..."));
    results_box->clear();

    auto snapshot = controllers;
    QThread* worker = QThread::create([this, snapshot]()
    {
        constexpr int SAMPLES = 15;

        /* Exclusive hardware access: live pushes pause and drain
           until resumePushes() at the end of the run. */
        bridge->pausePushes();

        auto emit_line = [this](const QString& head, RGBControllerInterface* ctrl,
                                unsigned int z, const QString& tail)
        {
            const QString line = QStringLiteral("%1 |   zone %2 %3 (%4 LEDs): %5")
                                 .arg(head)
                                 .arg(z)
                                 .arg(QString::fromStdString(ctrl->GetZoneName(z)))
                                 .arg(ctrl->GetZoneLEDsCount(z))
                                 .arg(tail);
            QMetaObject::invokeMethod(this, "AppendResult", Qt::QueuedConnection,
                                      Q_ARG(QString, line));
        };

        auto sample_zone = [this, &emit_line](const QString& head,
                                            RGBControllerInterface* ctrl,
                                            unsigned int z, const QString& mode_note)
        {
            /* Alternate two dim colors per sample so drivers can't
               dedup an unchanged buffer; restore the colors after. */
            const unsigned int leds  = ctrl->GetZoneLEDsCount(z);
            const unsigned int start = ctrl->GetZoneStartIndex(z);
            std::vector<RGBColor> saved(leds);
            for(unsigned int l = 0; l < leds; l++)
            {
                saved[l] = ctrl->GetZoneColor(z, l);
            }

            QElapsedTimer timer;
            qint64 total = 0;
            qint64 best  = -1;
            qint64 worst = 0;
            for(int s = 0; s < SAMPLES; s++)
            {
                const RGBColor c = (s & 1) ? ToRGBColor(0, 0, 24)
                                          : ToRGBColor(24, 0, 0);
                for(unsigned int l = 0; l < leds; l++)
                {
                    ctrl->SetColor(start + l, c);
                }
                timer.start();
                ctrl->UpdateZoneLEDs(z);
                const qint64 ms = timer.elapsed();
                total += ms;
                worst  = qMax(worst, ms);
                if(best < 0 || ms < best)
                {
                    best = ms;
                }
            }
            for(unsigned int l = 0; l < leds; l++)
            {
                ctrl->SetColor(start + l, saved[l]);
            }
            ctrl->UpdateZoneLEDs(z);

            const double avg = (double)total / SAMPLES;
            QString tail = mode_note + QStringLiteral("avg %1 ms, min %2 ms, max %3 ms, ~%4 updates/s")
                           .arg(avg, 0, 'f', 1)
                           .arg(best)
                           .arg(worst)
                           .arg(avg > 0.0 ? QString::number(1000.0 / avg, 'f', 0)
                                          : QStringLiteral("inf"));
            if(ctrl->GetLocation().rfind("Wireless:", 0) == 0)
            {
                tail += QStringLiteral("  (enqueue only - tx on ~300 ms poll)");
            }
            emit_line(head, ctrl, z, tail);
        };

        for(size_t ci = 0; ci < snapshot.size(); ci++)
        {
            RGBControllerInterface* ctrl = snapshot[ci];
            const QString head = QStringLiteral("[%1] %2")
                                 .arg(ci)
                                 .arg(QString::fromStdString(ctrl->GetName()));

            if(ctrl->SupportsPerZoneModes())
            {
                /* Per-zone modes: switch and restore each zone. */
                for(unsigned int z = 0; z < ctrl->GetZoneCount(); z++)
                {
                    const int prev = ctrl->GetZoneActiveMode(z);

                    int per_led = -1;
                    for(unsigned int m = 0; m < ctrl->GetZoneModeCount(z); m++)
                    {
                        if(ctrl->GetZoneModeFlags(z, m) & MODE_FLAG_HAS_PER_LED_COLOR)
                        {
                            per_led = (int)m;
                            break;
                        }
                    }

                    if(per_led < 0)
                    {
                        emit_line(head, ctrl, z,
                                  (prev >= 0)
                                  ? QStringLiteral("skipped - mode '%1', no per-LED mode available")
                                    .arg(QString::fromStdString(ctrl->GetZoneModeName(z, prev)))
                                  : QStringLiteral("skipped - no modes on this zone"));
                        continue;
                    }

                    const QString prev_name = (prev >= 0)
                        ? QStringLiteral("'%1'")
                          .arg(QString::fromStdString(ctrl->GetZoneModeName(z, prev)))
                        : QStringLiteral("(none)");
                    const QString note = (prev == per_led)
                        ? QStringLiteral("mode '%1': ")
                          .arg(QString::fromStdString(ctrl->GetZoneModeName(z, per_led)))
                        : QStringLiteral("mode %1 -> '%2': ")
                          .arg(prev_name)
                          .arg(QString::fromStdString(ctrl->GetZoneModeName(z, per_led)));

                    QMutexLocker io_lock(&g_io_mutex);
                    if(prev != per_led)
                    {
                        ctrl->SetZoneActiveMode(z, per_led);
                    }
                    sample_zone(head, ctrl, z, note);
                    if(prev != per_led)
                    {
                        ctrl->SetZoneActiveMode(z, prev);
                    }
                }
            }
            else
            {
                /* Device-level modes: one switch covers all zones. */
                const int prev = ctrl->GetActiveMode();

                int per_led = -1;
                for(unsigned int m = 0; m < ctrl->GetModeCount(); m++)
                {
                    if(ctrl->GetModeFlags(m) & MODE_FLAG_HAS_PER_LED_COLOR)
                    {
                        per_led = (int)m;
                        break;
                    }
                }

                if(per_led < 0)
                {
                    const QString tail = (ctrl->GetModeCount() > 0)
                        ? QStringLiteral("skipped - device mode '%1', no per-LED mode available")
                          .arg(QString::fromStdString(ctrl->GetModeName(prev)))
                        : QStringLiteral("skipped - device has no modes");
                    for(unsigned int z = 0; z < ctrl->GetZoneCount(); z++)
                    {
                        emit_line(head, ctrl, z, tail);
                    }
                    continue;
                }

                const QString note = (prev == per_led)
                    ? QStringLiteral("mode '%1': ")
                      .arg(QString::fromStdString(ctrl->GetModeName(per_led)))
                    : QStringLiteral("mode '%1' -> '%2': ")
                      .arg(QString::fromStdString(ctrl->GetModeName(prev)))
                      .arg(QString::fromStdString(ctrl->GetModeName(per_led)));

                QMutexLocker io_lock(&g_io_mutex);
                if(prev != per_led)
                {
                    ctrl->SetActiveMode(per_led);
                }
                for(unsigned int z = 0; z < ctrl->GetZoneCount(); z++)
                {
                    sample_zone(head, ctrl, z, note);
                }
                if(prev != per_led)
                {
                    ctrl->SetActiveMode(prev);
                }
            }
        }
        bridge->resumePushes();
        QMetaObject::invokeMethod(status_label, "setText", Qt::QueuedConnection,
                                  Q_ARG(QString, QStringLiteral("measurement complete")));
    });
    connect(worker, &QThread::finished, worker, &QThread::deleteLater);
    worker->start();
}

void StudioTab::AppendResult(const QString& line)
{
    results_box->appendPlainText(line);
}

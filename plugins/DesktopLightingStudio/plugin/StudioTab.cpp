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
        QMetaObject::invokeMethod(status_label, "setText", Qt::QueuedConnection,
                                  Q_ARG(QString, QStringLiteral("flash done")));
    }).detach();
}

/*---------------------------------------------------------*\
|| Measure per-zone UpdateZoneLEDs() wall time on a worker   |
|| thread; reports avg/max ms and implied max update rate.   |
|| Zones whose active mode lacks per-LED color are skipped.  |
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
        for(RGBControllerInterface* ctrl : snapshot)
        {
            QString head = QStringLiteral("%1").arg(QString::fromStdString(ctrl->GetName()));
            for(unsigned int z = 0; z < ctrl->GetZoneCount(); z++)
            {
                const int active = ctrl->GetZoneActiveMode(z);
                const unsigned int flags = (active >= 0) ? ctrl->GetZoneModeFlags(z, active) : 0;

                QString line = QStringLiteral("  zone %0 %1 (%2 LEDs): ")
                               .arg(z)
                               .arg(QString::fromStdString(ctrl->GetZoneName(z)))
                               .arg(ctrl->GetZoneLEDsCount(z));

                if(!(flags & MODE_FLAG_HAS_PER_LED_COLOR))
                {
                    line += QStringLiteral("skipped - active mode has no per-LED color");
                }
                else
                {
                    QMutexLocker io_lock(&g_io_mutex);
                    QElapsedTimer timer;
                    qint64 total = 0;
                    qint64 worst = 0;
                    for(int s = 0; s < SAMPLES; s++)
                    {
                        timer.start();
                        ctrl->UpdateZoneLEDs(z);
                        const qint64 ms = timer.elapsed();
                        total += ms;
                        worst = qMax(worst, ms);
                    }
                    const double avg = (double)total / SAMPLES;
                    line += QStringLiteral("avg %1 ms, max %2 ms, ~%3 updates/s")
                            .arg(avg, 0, 'f', 1)
                            .arg(worst)
                            .arg(avg > 0.0 ? QString::number(1000.0 / avg, 'f', 0) : QStringLiteral("inf"));
                }
                QMetaObject::invokeMethod(this, "AppendResult", Qt::QueuedConnection,
                                          Q_ARG(QString, head + QStringLiteral(" | ") + line));
            }
        }
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

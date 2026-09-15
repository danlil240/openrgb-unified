/*---------------------------------------------------------*\
|| StudioTab.cpp                                             |
||                                                           |
||   Studio tab widget — hosts a QQuickWidget running the    |
||   Stage 0 Qt Quick 3D probe scene inside a QWidget tab,   |
||   plus minimal device-inspection controls used to gather  |
||   the Stage 0 capability/rate snapshot.                   |
||                                                           |
||   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "StudioTab.h"

#include <QComboBox>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QQmlEngine>
#include <QQuickWidget>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QThread>
#include <QTimer>
#include <QImage>
#include <QVBoxLayout>

#include <chrono>
#include <thread>

#include "OpenRGBPluginInterface.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

/*---------------------------------------------------------*\
| Directory containing this plugin DLL. Packaged QML        |
| modules are deployed beside it in a "qml" subfolder.      |
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
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    /*-----------------------------------------------------*\
    | 3D probe viewport                                     |
    \*-----------------------------------------------------*/
    quick_widget = new QQuickWidget(this);
    quick_widget->setResizeMode(QQuickWidget::SizeRootObjectToView);
    quick_widget->setMinimumHeight(320);

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

    controller_combo->setMinimumWidth(260);
    zone_combo->setMinimumWidth(180);

    bar_row->addWidget(controller_combo);
    bar_row->addWidget(zone_combo);
    bar_row->addWidget(refresh_btn);
    bar_row->addWidget(flash_btn);
    bar_row->addWidget(measure_btn);
    bar_row->addStretch(1);

    results_box = new QPlainTextEdit(this);
    results_box->setReadOnly(true);
    results_box->setMaximumHeight(130);
    results_box->setStyleSheet("QPlainTextEdit { background: #141418; color: #b8b8c2; font-family: Consolas, monospace; }");

    status_label = new QLabel(this);
    status_label->setStyleSheet("QLabel { background: #18181c; color: #9a9aa5; padding: 4px 8px; }");

    layout->addWidget(quick_widget, 1);
    layout->addWidget(bar);
    layout->addWidget(results_box);
    layout->addWidget(status_label);

    /*-----------------------------------------------------*\
    | Wiring                                                |
    \*-----------------------------------------------------*/
    connect(refresh_btn, &QPushButton::clicked, this, [this]() { RefreshControllers(); });

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

    /*-----------------------------------------------------*\
    | QML status reporting                                  |
    \*-----------------------------------------------------*/
    auto report_qml = [this](QQuickWidget::Status status)
    {
        if(status == QQuickWidget::Ready && quick_widget->quickWindow() != nullptr)
        {
            QSGRendererInterface* rhi = quick_widget->quickWindow()->rendererInterface();
            const char* api_name = rhi ? GraphicsApiName(rhi->graphicsApi()) : "Unknown";
            status_label->setText(QStringLiteral("Qt Quick 3D probe ready - RHI backend: %1 - drag to orbit, scroll to zoom, click a part")
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

    /* Framebuffer probe: after the scene settles, grab a frame and count
       lit pixels to distinguish a live View3D from a black render. */
    QTimer::singleShot(2500, this, [this]()
    {
        if(quick_widget->quickWindow() == nullptr)
        {
            AppendResult(QStringLiteral("fb probe: no quick window"));
            return;
        }
        QImage frame = quick_widget->grabFramebuffer();
        long lit = 0;
        for(int y = 0; y < frame.height(); y += 8)
        {
            for(int x = 0; x < frame.width(); x += 8)
            {
                const QRgb px = frame.pixel(x, y);
                if(qRed(px) > 40 || qGreen(px) > 40 || qBlue(px) > 40)
                {
                    lit++;
                }
            }
        }
        AppendResult(QStringLiteral("fb probe: %1x%2, lit samples=%3 (nonzero = scene renders)")
                     .arg(frame.width()).arg(frame.height()).arg(lit));
    });

    if(quick_widget->status() == QQuickWidget::Error)
    {
        report_qml(QQuickWidget::Error);
    }
    else
    {
        connect(quick_widget, &QQuickWidget::statusChanged, this, report_qml);
    }

    RefreshControllers();
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
| Flash a low-brightness identification pattern on the      |
| selected zone (red/black alternating, ~4 s).              |
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
| Measure per-zone UpdateZoneLEDs() wall time on a worker   |
| thread; reports avg/max ms and implied max update rate.   |
| Zones whose active mode lacks per-LED color are skipped.  |
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

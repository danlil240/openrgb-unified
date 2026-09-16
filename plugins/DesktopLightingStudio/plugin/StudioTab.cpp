/*---------------------------------------------------------*\
||| StudioTab.cpp                                             |
|||                                                           |
|||   Studio tab widget — hosts a QQuickWidget running the   |
|||   unified workspace shell (ui/StudioWorkspace.qml, with  |
|||   ui/StudioScene.qml as its embedded viewport). The old  |
|||   C++ control bars migrated into QML; what stays here:   |
|||   the QQuickWidget host, the probe worker, and the       |
|||   `studioHost` invokable seam for file dialogs /         |
|||   confirmations (QFileDialog/QMessageBox) plus the       |
|||   serialized diagnostics probes.                         |
|||                                                           |
|||   SPDX-License-Identifier: GPL-2.0-or-later               |
|\*---------------------------------------------------------*/

#include "StudioTab.h"
#include "SceneBridge.h"
#include "../inputs/ScreenSampler.h"

#include <QColorDialog>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickWidget>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QThread>
#include <QVBoxLayout>
#include <QVariantMap>

#include <set>

#include <chrono>
#include <thread>
#include <QMutex>
#include <QMutexLocker>

#include "OpenRGBPluginInterface.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

/*---------------------------------------------------------*\
||| Directory containing this plugin DLL. Packaged QML        |
||| modules are deployed beside it in a "qml" subfolder.      |
|\*---------------------------------------------------------*/
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
    | Workspace viewport — the QML shell owns all chrome.   |
    \*-----------------------------------------------------*/
    quick_widget = new QQuickWidget(this);
    quick_widget->setResizeMode(QQuickWidget::SizeRootObjectToView);
    quick_widget->setMinimumHeight(320);
    quick_widget->rootContext()->setContextProperty("bridge", bridge);
    /* Second context object: file dialogs / confirm prompts and
       the diagnostics probes need native code — the QML shell
       calls these as `studioHost.*`. */
    quick_widget->rootContext()->setContextProperty("studioHost", this);

    const QString plugin_dir = PluginDirectory();
    if(!plugin_dir.isEmpty())
    {
        quick_widget->engine()->addImportPath(plugin_dir + "/qml");
    }

    /* Slim error strip — visible ONLY when the QML root fails to
       load or the scene graph dies; with all chrome in QML an
       engine error would otherwise leave a silent blank tab. */
    error_label = new QLabel(this);
    error_label->setStyleSheet(
        "QLabel { background: #2a1418; color: #e0a0a0; padding: 4px 8px; }");
    error_label->setWordWrap(true);
    error_label->setVisible(false);

    layout->addWidget(quick_widget, 1);
    layout->addWidget(error_label);

    /* Prefer the loose QML beside the plugin (editable without a
       rebuild); fall back to the embedded copy, then to the bare
       scene if the workspace files aren't deployed. */
    const QString file_workspace = plugin_dir + "/ui/StudioWorkspace.qml";
    const QString file_scene     = plugin_dir + "/ui/StudioScene.qml";
    if(QFileInfo::exists(file_workspace))
    {
        quick_widget->setSource(QUrl::fromLocalFile(file_workspace));
    }
    else if(QFileInfo::exists(QStringLiteral(":/studio/StudioWorkspace.qml")))
    {
        quick_widget->setSource(
            QUrl(QStringLiteral("qrc:/studio/StudioWorkspace.qml")));
    }
    else if(QFileInfo::exists(file_scene))
    {
        quick_widget->setSource(QUrl::fromLocalFile(file_scene));
    }
    else
    {
        quick_widget->setSource(QUrl(QStringLiteral("qrc:/studio/StudioScene.qml")));
    }

    /*-----------------------------------------------------*\
    | Workspace prompts — detection lives in the store; the |
    | tab only asks which side wins.                        |
    \*-----------------------------------------------------*/
    connect(bridge, &studio::SceneBridge::externalChangeDetected, this,
            [this](bool dirty) { PromptExternalChange(dirty); });
    connect(bridge, &studio::SceneBridge::recoveryAvailable, this,
            [this]() { PromptRecovery(); });

    /* Status/hint lines route to the QML diagnostics log. */
    connect(bridge, &studio::SceneBridge::statusMessage,
            this, [this](const QString& line) { AppendResult(line); });

    /*-----------------------------------------------------*\
    | QML status reporting                                  |
    \*-----------------------------------------------------*/
    auto report_qml = [this](QQuickWidget::Status status)
    {
        if(status == QQuickWidget::Ready && quick_widget->quickWindow() != nullptr)
        {
            QSGRendererInterface* rhi = quick_widget->quickWindow()->rendererInterface();
            const char* api_name = rhi ? GraphicsApiName(rhi->graphicsApi()) : "Unknown";
            error_label->setVisible(false);
            AppendResult(QStringLiteral("Studio ready - RHI backend: %1")
                         .arg(QString::fromLatin1(api_name)));
        }
        else if(status == QQuickWidget::Error)
        {
            QStringList lines;
            for(const QQmlError& error : quick_widget->errors())
            {
                lines << error.toString();
            }
            error_label->setText("QML load failed: " + lines.join(" | "));
            error_label->setVisible(true);
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
    /* Only re-resolve bindings — repopulating the inspection list on
       every resource signal would wipe the diagnostics log. */
    bridge->refreshDevices();
}

/*---------------------------------------------------------*\
||| studioHost seam — workspace file/paint actions.         ||
|\*---------------------------------------------------------*/
void StudioTab::uiPickColor()
{
    PickColor();
}

void StudioTab::uiSave()
{
    bridge->saveScene();
}

void StudioTab::uiSaveCopyAs()
{
    PromptSaveCopy();
}

void StudioTab::uiReload()
{
    PromptReload();
}

void StudioTab::uiRestoreBackup()
{
    if(ConfirmLoseDirty(QStringLiteral("restore the backup")))
    {
        bridge->restoreBackup();
    }
}

void StudioTab::uiReset()
{
    if(ConfirmLoseDirty(QStringLiteral("reset to the default desk")))
    {
        bridge->resetScene();
    }
}

void StudioTab::uiOpenWorkspaceFolder()
{
    QString dir = bridge->workspaceDir();
    if(dir.isEmpty())
    {
        AppendResult(QStringLiteral("workspace unavailable"));
        return;
    }
    QDir().mkpath(dir);
    QDesktopServices::openUrl(
        QUrl::fromLocalFile(QFileInfo(dir).absoluteFilePath()));
}

void StudioTab::uiExportBundle()
{
    /* Pick a destination folder — the export writes studio.json +
       presets/devices/ + assets/ into it. The bundle's own rule
       refuses an occupied dir without an explicit overwrite; the
       prompts below are the overwrite grant the user gives. */
    const QString dir = QFileDialog::getExistingDirectory(this,
        QStringLiteral("Export studio bundle — choose a folder"),
        bridge->workspaceDir());
    if(dir.isEmpty())
    {
        return;
    }
    bool overwrite = false;
    if(QFileInfo::exists(dir + QStringLiteral("/studio.json")))
    {
        const auto choice = QMessageBox::warning(this,
            QStringLiteral("Overwrite bundle?"),
            QStringLiteral("%1 already contains a studio.json bundle.\n"
                           "Replace it?").arg(dir),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if(choice != QMessageBox::Yes)
        {
            return;
        }
        overwrite = true;
    }
    else if(!QDir(dir).entryList(
                QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty())
    {
        /* Non-empty but no studio.json — ExportBundle refuses
           foreign content without overwrite, so ask here rather
           than dead-ending on the export error. */
        const auto choice = QMessageBox::warning(this,
            QStringLiteral("Folder not empty"),
            QStringLiteral("%1 is not empty.\n"
                           "Export the bundle into it anyway?").arg(dir),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if(choice != QMessageBox::Yes)
        {
            return;
        }
        overwrite = true;
    }
    bridge->exportBundle(dir, overwrite);
}

void StudioTab::uiImportBundle()
{
    /* An imported bundle replaces the workspace — same dirty-loss
       confirmation as a reload. */
    if(!ConfirmLoseDirty(QStringLiteral("Importing a bundle")))
    {
        return;
    }
    const QString dir = QFileDialog::getExistingDirectory(this,
        QStringLiteral("Import studio bundle"),
        bridge->workspaceDir());
    if(dir.isEmpty())
    {
        return;
    }
    const QVariantMap info = bridge->inspectBundle(dir);
    if(!info.value(QStringLiteral("ok")).toBool())
    {
        AppendResult(QStringLiteral("import: %1")
            .arg(info.value(QStringLiteral("error")).toString()));
        return;
    }
    for(const QVariant& w : info.value(QStringLiteral("warnings")).toList())
    {
        AppendResult(QStringLiteral("import: %1").arg(w.toString()));
    }

    /* The only conflict resolution: a same-id/different-content
       type imports under a NEW id — the local file is never
       overwritten. One dialog collects all new ids; cancel
       aborts the import cleanly (nothing was written — inspect
       is read-only and apply never runs). */
    const QStringList conflicts =
        info.value(QStringLiteral("conflicts")).toStringList();
    QVariantMap choices;
    if(!conflicts.isEmpty())
    {
        QDialog dlg(this);
        dlg.setWindowTitle(QStringLiteral("Resolve type conflicts"));
        QVBoxLayout* lay = new QVBoxLayout(&dlg);
        QLabel* intro = new QLabel(QStringLiteral(
            "These bundled types differ from your local definitions.\n"
            "Choose a new type id for each — the local file is kept."),
            &dlg);
        intro->setWordWrap(true);
        lay->addWidget(intro);
        std::vector<QLineEdit*> edits;
        for(const QString& id : conflicts)
        {
            QHBoxLayout* row = new QHBoxLayout;
            row->addWidget(new QLabel(id, &dlg));
            QLineEdit* edit = new QLineEdit(&dlg);
            QString cand = id + QStringLiteral("-import");
            for(int n = 2; !bridge->presetIdAvailable(cand); n++)
            {
                cand = id + QStringLiteral("-import") + QString::number(n);
            }
            edit->setText(cand);
            row->addWidget(edit, 1);
            lay->addLayout(row);
            edits.push_back(edit);
        }
        QDialogButtonBox* buttons = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        QObject::connect(buttons, &QDialogButtonBox::accepted,
                         &dlg, &QDialog::accept);
        QObject::connect(buttons, &QDialogButtonBox::rejected,
                         &dlg, &QDialog::reject);
        lay->addWidget(buttons);
        for(;;)
        {
            if(dlg.exec() != QDialog::Accepted)
            {
                AppendResult(QStringLiteral("import cancelled"));
                return;
            }
            choices.clear();
            QStringList bad;
            std::set<QString> seen;
            for(size_t i = 0; i < (size_t)conflicts.size(); i++)
            {
                const QString nid = edits[i]->text().trimmed();
                /* Valid charset, free locally, unique within the
                   dialog — ApplyImport re-checks all of it. */
                if(!bridge->presetIdAvailable(nid)
                   || !seen.insert(nid).second)
                {
                    bad << nid;
                }
                choices.insert(conflicts[i], nid);
            }
            if(bad.isEmpty())
            {
                break;
            }
            QMessageBox::warning(&dlg, QStringLiteral("Invalid id"),
                QStringLiteral("Invalid or already-used id: %1\n\n"
                               "Type ids use letters, digits, '_' and "
                               "'-', and must not collide with an "
                               "existing type.")
                    .arg(bad.join(QStringLiteral(", "))));
        }
    }
    bridge->importBundle(dir, choices);
}

void StudioTab::uiReloadTypes()
{
    /* Type files change shape, never placements — no dirty prompt;
       a failed re-resolve keeps the current scene. */
    bridge->reloadDeviceTypes();
}

QStringList StudioTab::uiScreenNames() const
{
    return studio::ScreenSampler::DisplayNames();
}

/*---------------------------------------------------------*\
||| studioHost seam — diagnostics drawer.                    |
|||                                                          |
|||   Controller/zone lists are snapshots the QML combos      |
|||   display; the probe buttons run the same serialized      |
|||   worker path the old hardware bar used.                  |
|\*---------------------------------------------------------*/
QVariantList StudioTab::diagControllers() const
{
    QVariantList out;
    for(size_t i = 0; i < controllers.size(); i++)
    {
        RGBControllerInterface* ctrl = controllers[i];
        QVariantMap m;
        m["index"] = (int)i;
        m["label"] = QStringLiteral("[%0] %1 - %2")
                     .arg(i)
                     .arg(QString::fromStdString(ctrl->GetName()))
                     .arg(QString::fromStdString(api->DeviceTypeToString(ctrl->GetDeviceType())));
        out.push_back(m);
    }
    return out;
}

QVariantList StudioTab::diagZones(int controller) const
{
    QVariantList out;
    if(controller < 0 || controller >= (int)controllers.size())
    {
        return out;
    }
    RGBControllerInterface* ctrl = controllers[controller];
    for(unsigned int z = 0; z < ctrl->GetZoneCount(); z++)
    {
        QVariantMap m;
        m["index"] = (int)z;
        m["label"] = QStringLiteral("%0: %1 (%2 LEDs)")
                     .arg(z)
                     .arg(QString::fromStdString(ctrl->GetZoneName(z)))
                     .arg(ctrl->GetZoneLEDsCount(z));
        out.push_back(m);
    }
    return out;
}

void StudioTab::diagRefresh()
{
    RefreshControllers();
    bridge->refreshDevices();
}

void StudioTab::diagFlash(int controller, int zone)
{
    if(controller < 0 || zone < 0 || controller >= (int)controllers.size())
    {
        return;
    }
    RGBControllerInterface* ctrl = controllers[controller];
    if(zone >= (int)ctrl->GetZoneCount())
    {
        return;
    }
    const int zone_idx = zone;

    AppendResult(QStringLiteral("flashing %1 / %2 ...")
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
        QMetaObject::invokeMethod(this, "AppendResult", Qt::QueuedConnection,
                                  Q_ARG(QString, QStringLiteral("flash done")));
    }).detach();
}

/*---------------------------------------------------------*\
||| Measure per-zone UpdateZoneLEDs() wall time on a worker   |
||| thread; reports avg/max ms and implied max update rate.   |
||| Zones not already in a per-LED color mode are switched    |
||| into one for the measurement (mirroring EnsurePerLedMode) |
||| and the previous mode is restored afterwards. Zones with  |
||| no per-LED mode at all are reported with the active mode. |
|\*---------------------------------------------------------*/
void StudioTab::diagMeasure()
{
    if(api == nullptr || controllers.empty())
    {
        AppendResult(QStringLiteral("no controllers detected - run OpenRGB elevated for full detection"));
        return;
    }

    AppendResult(QStringLiteral("measuring write latency..."));

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
        QMetaObject::invokeMethod(this, "AppendResult", Qt::QueuedConnection,
                                  Q_ARG(QString, QStringLiteral("measurement complete")));
    });
    connect(worker, &QThread::finished, worker, &QThread::deleteLater);
    worker->start();
}

void StudioTab::PickColor()
{
    const QColor color = QColorDialog::getColor(
        bridge->paintColor(), this, QStringLiteral("Object color"));
    if(color.isValid())
    {
        bridge->setPaintColor(color);
        bridge->setSelectedColor(color);
    }
}

void StudioTab::RefreshControllers()
{
    controllers.clear();

    if(api == nullptr)
    {
        AppendResult(QStringLiteral("plugin API not available"));
        emit diagnosticsControllersChanged();
        return;
    }

    controllers = api->GetRGBControllers();

    for(size_t i = 0; i < controllers.size(); i++)
    {
        RGBControllerInterface* ctrl = controllers[i];
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
    emit diagnosticsControllersChanged();
}

void StudioTab::AppendResult(const QString& line)
{
    /* Was the C++ results box — now the QML diagnostics log;
       qInfo keeps the lines reachable even if QML is down. */
    emit diagnosticsLine(line);
    qInfo("DesktopLightingStudio: %s", qPrintable(line));
}

/*---------------------------------------------------------*\
|| Workspace file actions.                                   |
||                                                           |
|| The dialogs are the thin edge of the conflict model:      |
|| the store detects, the bridge applies, this tab only      |
|| asks the user which side wins.                            |
|\*---------------------------------------------------------*/
bool StudioTab::ConfirmLoseDirty(const QString& action)
{
    if(!bridge->dirty())
    {
        return true;
    }
    const auto choice = QMessageBox::warning(this,
        QStringLiteral("Unsaved changes"),
        QStringLiteral("You have unsaved changes. %1 will discard them "
                       "(a copy is still autosaved for recovery).\n\n"
                       "Save studio.json first?").arg(action),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);
    if(choice == QMessageBox::Save)
    {
        return bridge->saveScene();
    }
    return choice == QMessageBox::Discard;
}

void StudioTab::PromptReload()
{
    /* Dirty: reloading discards the in-memory edits — confirm. */
    if(!ConfirmLoseDirty(QStringLiteral("Reloading studio.json")))
    {
        return;
    }
    bridge->reloadScene();
}

void StudioTab::PromptSaveCopy()
{
    const QString path = QFileDialog::getSaveFileName(this,
        QStringLiteral("Save workspace copy"),
        bridge->workspaceDir(),
        QStringLiteral("Studio workspace (*.json)"));
    if(path.isEmpty())
    {
        return;
    }
    bridge->saveSceneAs(path);
}

void StudioTab::PromptExternalChange(bool dirty)
{
    /* The watcher fired — studio.json changed on disk. Clean doc:
       just offer reload. Dirty doc: the three-way conflict choice. */
    if(!dirty)
    {
        const auto choice = QMessageBox::information(this,
            QStringLiteral("studio.json changed"),
            QStringLiteral("studio.json was modified outside Studio.\n"
                           "Reload it now?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if(choice == QMessageBox::Yes)
        {
            bridge->reloadScene();
        }
        return;
    }

    QMessageBox box(QMessageBox::Warning,
        QStringLiteral("studio.json changed"),
        QStringLiteral("studio.json was modified outside Studio, but you "
                       "also have unsaved changes."),
        QMessageBox::NoButton, this);
    QPushButton* keep = box.addButton(QStringLiteral("Keep current"),
                                    QMessageBox::RejectRole);
    QPushButton* reload = box.addButton(QStringLiteral("Reload disk"),
                                        QMessageBox::AcceptRole);
    QPushButton* copy = box.addButton(QStringLiteral("Save copy…"),
                                      QMessageBox::ActionRole);
    box.setDefaultButton(keep);
    box.exec();

    if(box.clickedButton() == reload)
    {
        bridge->reloadScene();
    }
    else if(box.clickedButton() == copy)
    {
        PromptSaveCopy();
    }
    /* "Keep current" does nothing — the in-memory doc stays and the
       next save overwrites the disk file. */
}

void StudioTab::PromptRecovery()
{
    /* An autosave exists and is newer than studio.json — either an
       interrupted save or unsaved edits from last session. */
    const auto choice = QMessageBox::information(this,
        QStringLiteral("Recover unsaved changes?"),
        QStringLiteral("An autosaved workspace newer than studio.json "
                       "was found — the last session may have closed "
                       "with unsaved changes.\n\n"
                       "Recover those changes?"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if(choice == QMessageBox::Yes)
    {
        bridge->recoverAutosave();
    }
    else
    {
        bridge->discardRecovery();
    }
}

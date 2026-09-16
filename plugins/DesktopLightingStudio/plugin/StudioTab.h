/*---------------------------------------------------------*\
||| StudioTab.h                                               |
|||                                                           |
|||   Studio tab widget — hosts a QQuickWidget running the   |
|||   unified QML workspace (ui/StudioWorkspace.qml) and     |
|||   exposes itself to it as the `studioHost` context       |
|||   property: file dialogs / confirm prompts stay in C++   |
|||   (QFileDialog/QMessageBox), and the diagnostics drawer  |
|||   drives the same serialized probe worker as before.     |
|||                                                           |
|||   SPDX-License-Identifier: GPL-2.0-or-later               |
|\*---------------------------------------------------------*/

#pragma once

#include <QWidget>
#include <QStringList>
#include <QVariantList>
#include <vector>

class QLabel;
class QQuickWidget;
class OpenRGBPluginAPIInterface;
class RGBControllerInterface;

namespace studio { class SceneBridge; }

class StudioTab : public QWidget
{
    Q_OBJECT

public:
    explicit StudioTab(OpenRGBPluginAPIInterface* plugin_api, QWidget* parent = nullptr);

    /* Called by the plugin on device-list changes — re-resolves
       scene bindings and refreshes the inspection list. */
    void        OnDevicesChanged();

    /*-----------------------------------------------------*\
    | QML workspace seam (`studioHost` context property).  |
    | Every entry delegates to the existing prompt/worker  |
    | paths — nothing here touches hardware directly on    |
    | the GUI thread.                                      |
    \*-----------------------------------------------------*/
    Q_INVOKABLE void        uiPickColor();
    Q_INVOKABLE void        uiSave();
    Q_INVOKABLE void        uiSaveCopyAs();
    Q_INVOKABLE void        uiReload();
    Q_INVOKABLE void        uiRestoreBackup();
    Q_INVOKABLE void        uiReset();
    Q_INVOKABLE void        uiOpenWorkspaceFolder();
    /* Task 4.4 — portable bundles + explicit type reload. The
       dialogs (dir picks, the same-id conflict choices) live
       here; the bridge owns the semantics. */
    Q_INVOKABLE void        uiExportBundle();
    Q_INVOKABLE void        uiImportBundle();
    Q_INVOKABLE void        uiReloadTypes();
    Q_INVOKABLE QStringList uiScreenNames() const;

    /* Diagnostics drawer — controller/zone pickers + the probe
       buttons. diagFlash/diagMeasure run on serialized workers
       under bridge->pausePushes(), exactly like the old bar. */
    Q_INVOKABLE QVariantList diagControllers() const;
    Q_INVOKABLE QVariantList diagZones(int controller) const;
    Q_INVOKABLE void         diagRefresh();
    Q_INVOKABLE void         diagFlash(int controller, int zone);
    Q_INVOKABLE void         diagMeasure();

signals:
    /* Lines for the QML diagnostics log (replaces results_box). */
    void diagnosticsLine(const QString& line);
    /* Controller list changed — the drawer re-queries. */
    void diagnosticsControllersChanged();

private slots:
    void        AppendResult(const QString& line);

private:
    void        RefreshControllers();
    void        PickColor();
    /* Workspace file actions — minimal wrappers; the store/bridge
       owns the semantics. */
    bool        ConfirmLoseDirty(const QString& action);
    void        PromptReload();
    void        PromptSaveCopy();
    void        PromptExternalChange(bool dirty);
    void        PromptRecovery();

    OpenRGBPluginAPIInterface*      api              = nullptr;
    std::vector<RGBControllerInterface*> controllers;

    studio::SceneBridge*    bridge          = nullptr;
    QQuickWidget*           quick_widget    = nullptr;
    /* Slim strip shown ONLY on QML load failure — with the shell
       living in QML, an engine error would otherwise be silent. */
    QLabel*                 error_label     = nullptr;
};

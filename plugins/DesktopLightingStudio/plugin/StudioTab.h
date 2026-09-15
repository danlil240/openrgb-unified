/*---------------------------------------------------------*\
|| StudioTab.h                                               |
||                                                           |
||   Studio tab widget — hosts a QQuickWidget running the   |
||   desk scene (Stage 1) plus device-inspection controls   |
||   kept from the Stage 0 probe for calibration.           |
||                                                           |
||   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include <QWidget>
#include <vector>

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QQuickWidget;
class QSlider;
class OpenRGBPluginAPIInterface;
class RGBControllerInterface;

namespace studio { class SceneBridge; }

class StudioTab : public QWidget
{
    Q_OBJECT

public:
    explicit StudioTab(OpenRGBPluginAPIInterface* plugin_api, QWidget* parent = nullptr);

    /* Called by the plugin on device-list changes — re-resolves
       scene bindings and refreshes the inspection bar. */
    void        OnDevicesChanged();

private slots:
    void        AppendResult(const QString& line);

private:
    void        RefreshControllers();
    void        FlashSelectedZone();
    void        MeasureWriteLatency();
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
    QComboBox*              controller_combo = nullptr;
    QComboBox*              zone_combo      = nullptr;
    QPlainTextEdit*         results_box     = nullptr;
    QLabel*                 status_label    = nullptr;
    QLabel*                 selection_label = nullptr;
    QPushButton*            color_btn       = nullptr;
    QSlider*                brightness_slider = nullptr;
    QCheckBox*              live_check      = nullptr;
    QLabel*                 dirty_label     = nullptr;

    /* Stage 2 — scene cards + playback strip */
    QButtonGroup*           preset_group    = nullptr;
    QPushButton*            play_btn        = nullptr;
    QPushButton*            stop_btn        = nullptr;
    QPushButton*            remix_btn       = nullptr;
    QSlider*                speed_slider    = nullptr;
    QSlider*                intensity_slider = nullptr;
    QLabel*                 speed_label     = nullptr;
    QLabel*                 intensity_label = nullptr;

    /* Stage 3 — input sources row */
    QCheckBox*              audio_check     = nullptr;
    QCheckBox*              key_check       = nullptr;
    QCheckBox*              screen_check    = nullptr;
    QComboBox*              screen_combo    = nullptr;
    QSlider*                sens_slider     = nullptr;
    QSlider*                decay_slider    = nullptr;
    QLabel*                 sens_label      = nullptr;
    QLabel*                 decay_label     = nullptr;
};

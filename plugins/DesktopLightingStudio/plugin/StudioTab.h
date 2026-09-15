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

private slots:
    void        AppendResult(const QString& line);

private:
    void        RefreshControllers();
    void        FlashSelectedZone();
    void        MeasureWriteLatency();
    void        PickColor();

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
};

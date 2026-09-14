/*---------------------------------------------------------*\
|| StudioTab.h                                               |
||                                                           |
||   Studio tab widget — hosts a QQuickWidget running the    |
||   Stage 0 Qt Quick 3D probe scene inside a QWidget tab,   |
||   plus minimal device-inspection controls used to gather  |
||   the Stage 0 capability/rate snapshot.                   |
||                                                           |
||   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include <QWidget>
#include <vector>

class QComboBox;
class QLabel;
class QPlainTextEdit;
class QQuickWidget;
class OpenRGBPluginAPIInterface;
class RGBControllerInterface;

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

    OpenRGBPluginAPIInterface*      api              = nullptr;
    std::vector<RGBControllerInterface*> controllers;

    QQuickWidget*   quick_widget    = nullptr;
    QComboBox*      controller_combo = nullptr;
    QComboBox*      zone_combo      = nullptr;
    QPlainTextEdit* results_box     = nullptr;
    QLabel*         status_label    = nullptr;
};

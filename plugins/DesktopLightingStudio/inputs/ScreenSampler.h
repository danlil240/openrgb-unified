/*---------------------------------------------------------*\
||| ScreenSampler.h                                           |
|||                                                           |
|||   Periodic screen capture for the "screenfield"         |
|||   primitive. Grabs the selected display (~25 Hz via a   |
|||   GDI StretchBlt straight into the 12x6 grid), applies  |
|||   smoothing and a luminance cap, and posts it to the    |
|||   InputBus. A failed grab leaves the grid empty — the   |
|||   primitive contributes nothing and the scene falls     |
|||   back to its base layers (plan fallback rule).         |
|||                                                           |
|||   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

#include <vector>

#include "InputBus.h"

namespace studio
{

class ScreenSampler : public QObject
{
    Q_OBJECT

public:
    explicit ScreenSampler(InputBus* bus, QObject* parent = nullptr);

    void        Start(int screen_index);
    void        Stop();
    bool        Running() const;
    void        SetScreenIndex(int index);
    QString     Status() const { return status; }

    /* Display names for the selection combo. */
    static QStringList DisplayNames();

private slots:
    void        Grab();

private:
    static const int COLS = 12;
    static const int ROWS = 6;

    InputBus*            bus;
    QTimer*              timer;
    int                  screen_index = 0;
    std::vector<ColorF>  cells;               /* smoothed grid        */
    QString              status;
    float                max_lum    = 0.85f;  /* brightness limit     */
    float                smoothing  = 0.35f;  /* new-sample weight    */
};

} /* namespace studio */

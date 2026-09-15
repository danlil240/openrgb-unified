/*---------------------------------------------------------*\
||| ScreenSampler.cpp                                         |
|||                                                           |
|||   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "ScreenSampler.h"

#include <QGuiApplication>
#include <QImage>
#include <QPixmap>
#include <QScreen>

namespace studio
{

ScreenSampler::ScreenSampler(InputBus* b, QObject* parent)
    : QObject(parent)
    , bus(b)
{
    timer = new QTimer(this);
    timer->setInterval(100);
    connect(timer, &QTimer::timeout, this, &ScreenSampler::Grab);
    cells.assign(COLS * ROWS, ColorF{});
}

QStringList ScreenSampler::DisplayNames()
{
    QStringList out;
    const auto screens = QGuiApplication::screens();
    for(int i = 0; i < screens.size(); i++)
    {
        out << QStringLiteral("%1 — %2x%3")
                   .arg(screens[i]->name())
                   .arg(screens[i]->size().width())
                   .arg(screens[i]->size().height());
    }
    return out;
}

void ScreenSampler::Start(int index)
{
    screen_index = index;
    status.clear();
    timer->start();
    Grab();
}

void ScreenSampler::Stop()
{
    timer->stop();
    if(bus != nullptr)
    {
        bus->ClearScreenGrid();
    }
    status.clear();
}

bool ScreenSampler::Running() const
{
    return timer->isActive();
}

void ScreenSampler::SetScreenIndex(int index)
{
    screen_index = index;
    if(timer->isActive())
    {
        Grab();
    }
}

void ScreenSampler::Grab()
{
    const auto screens = QGuiApplication::screens();
    if(screen_index < 0 || screen_index >= screens.size())
    {
        status = QStringLiteral("screen: display %1 gone").arg(screen_index);
        if(bus != nullptr)
        {
            bus->ClearScreenGrid();
        }
        return;
    }

    QPixmap pm = screens[screen_index]->grabWindow(0);
    if(pm.isNull())
    {
        status = QStringLiteral("screen: capture unavailable");
        if(bus != nullptr)
        {
            bus->ClearScreenGrid();
        }
        return;
    }

    const QImage img = pm.toImage()
        .scaled(COLS, ROWS, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
        .convertToFormat(QImage::Format_RGB32);

    if(cells.size() != (size_t)(COLS * ROWS))
    {
        cells.assign(COLS * ROWS, ColorF{});
    }

    for(int y = 0; y < ROWS; y++)
    {
        const QRgb* row = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for(int x = 0; x < COLS; x++)
        {
            ColorF c;
            c.r = qRed(row[x])   / 255.0f;
            c.g = qGreen(row[x]) / 255.0f;
            c.b = qBlue(row[x])  / 255.0f;
            c.a = 1.0f;

            /* Brightness limit: scale saturated cells down so the
               ambient layer never drives LEDs at full tilt. */
            const float lum = 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
            if(lum > max_lum)
            {
                const float k = max_lum / lum;
                c.r *= k; c.g *= k; c.b *= k;
            }

            ColorF& cell = cells[y * COLS + x];
            cell.r += (c.r - cell.r) * smoothing;
            cell.g += (c.g - cell.g) * smoothing;
            cell.b += (c.b - cell.b) * smoothing;
        }
    }

    status = QStringLiteral("screen: sampling %1").arg(screens[screen_index]->name());
    if(bus != nullptr)
    {
        bus->SetScreenGrid(COLS, ROWS, cells);
    }
}

} /* namespace studio */

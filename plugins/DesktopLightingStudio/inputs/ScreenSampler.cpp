/*---------------------------------------------------------*\
||| ScreenSampler.cpp                                         |
|||                                                           |
|||   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "ScreenSampler.h"

#include <QGuiApplication>
#include <QImage>
#include <QPixmap>
#include <QRect>
#include <QScreen>

#include <cmath>
#include <cstring>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <CoreGraphics/CoreGraphics.h>
#endif

namespace studio
{

#ifdef _WIN32
/* Grab `scr` straight into a cols x rows 32-bit DIB via StretchBlt —
   the display driver downsamples, so only ~72 pixels cross to user
   space. QScreen::grabWindow instead readbacks the full desktop and
   downscales in software (~15-40 ms on the GUI thread — enough to
   stall effect ticks); this path is ~2-4 ms, cheap enough for
   ~25 Hz beside a 60 Hz tick. Screen coordinates are Qt logical
   geometry * devicePixelRatio = physical pixels in the virtual
   screen DC — the same mapping grabWindow uses. */
static bool GrabGdi(const QScreen* scr, int cols, int rows, unsigned int* px)
{
    const QRect g   = scr->geometry();
    const qreal dpr = scr->devicePixelRatio();
    const int sx = (int)std::lround(g.x() * dpr);
    const int sy = (int)std::lround(g.y() * dpr);
    const int sw = (int)std::lround(g.width()  * dpr);
    const int sh = (int)std::lround(g.height() * dpr);
    if(sw <= 0 || sh <= 0)
    {
        return false;
    }

    bool ok = false;
    HDC hdc_screen = GetDC(nullptr);   /* virtual screen: all monitors */
    if(hdc_screen == nullptr)
    {
        return false;
    }
    HDC hdc_mem = CreateCompatibleDC(hdc_screen);
    if(hdc_mem != nullptr)
    {
        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth       = cols;
        bmi.bmiHeader.biHeight      = -rows;   /* top-down row order */
        bmi.bmiHeader.biPlanes      = 1;
        bmi.bmiHeader.biBitCount    = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        HBITMAP bmp = CreateDIBSection(hdc_mem, &bmi, DIB_RGB_COLORS,
                                     &bits, nullptr, 0);
        if(bmp != nullptr && bits != nullptr)
        {
            HGDIOBJ prev = SelectObject(hdc_mem, bmp);
            SetStretchBltMode(hdc_mem, HALFTONE);
            SetBrushOrgEx(hdc_mem, 0, 0, nullptr);
            ok = StretchBlt(hdc_mem, 0, 0, cols, rows,
                            hdc_screen, sx, sy, sw, sh, SRCCOPY) != FALSE;
            if(ok)
            {
                std::memcpy(px, bits, (size_t)cols * rows * sizeof(unsigned int));
            }
            SelectObject(hdc_mem, prev);
            DeleteObject(bmp);
        }
        DeleteDC(hdc_mem);
    }
    ReleaseDC(nullptr, hdc_screen);
    return ok;
}
#endif

ScreenSampler::ScreenSampler(InputBus* b, QObject* parent)
    : QObject(parent)
    , bus(b)
{
    timer = new QTimer(this);
    timer->setTimerType(Qt::PreciseTimer);
    timer->setInterval(40);   /* ~25 Hz — GDI path is cheap */
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
#if defined(__APPLE__)
    /* Trigger the TCC prompt once up front; Grab() then reports
       the grant state honestly each tick. */
    CGRequestScreenCaptureAccess();
#endif
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
#if defined(__linux__)
    if(QGuiApplication::platformName().contains(QStringLiteral("wayland"),
                                               Qt::CaseInsensitive))
    {
        status = QStringLiteral("screen: unsupported on Wayland");
        if(bus != nullptr) { bus->ClearScreenGrid(); }
        return;
    }
#elif defined(__APPLE__)
    if(!CGPreflightScreenCaptureAccess())
    {
        status = QStringLiteral(
            "screen: grant Screen Recording to OpenRGB "
            "(System Settings > Privacy & Security)");
        if(bus != nullptr) { bus->ClearScreenGrid(); }
        return;
    }
#endif

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

    /* Shared pixel block — 0x..RRGGBB per cell in both grab paths
       (GDI DIB words are B,G,R,X little-endian; Format_RGB32 QRgb
       words are 0xAARRGGBB — same channel positions). */
    unsigned int px[COLS * ROWS];
    bool ok = false;
#ifdef _WIN32
    ok = GrabGdi(screens[screen_index], COLS, ROWS, px);
#else
    const QPixmap pm = screens[screen_index]->grabWindow(0);
    if(!pm.isNull())
    {
        const QImage img = pm.toImage()
            .scaled(COLS, ROWS, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
            .convertToFormat(QImage::Format_RGB32);
        if(img.width() == COLS && img.height() == ROWS)
        {
            std::memcpy(px, img.constBits(), sizeof(px));
            ok = true;
        }
    }
#endif
    if(!ok)
    {
        status = QStringLiteral("screen: capture unavailable");
        if(bus != nullptr)
        {
            bus->ClearScreenGrid();
        }
        return;
    }

    if(cells.size() != (size_t)(COLS * ROWS))
    {
        cells.assign(COLS * ROWS, ColorF{});
    }

    for(int y = 0; y < ROWS; y++)
    {
        for(int x = 0; x < COLS; x++)
        {
            const unsigned int v = px[y * COLS + x];
            ColorF c;
            c.r = ((v >> 16) & 0xFF) / 255.0f;
            c.g = ((v >> 8)  & 0xFF) / 255.0f;
            c.b = ( v        & 0xFF) / 255.0f;
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

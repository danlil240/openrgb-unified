/*---------------------------------------------------------*\
||| AudioLoopback.h                                           |
|||                                                           |
|||   WASAPI shared-mode loopback capture of the default    |
|||   render endpoint, on its own thread. Computes chunk    |
|||   energy, feeds the OnsetDetect, and pushes onset       |
|||   events + a smoothed level into the InputBus.          |
|||                                                           |
|||   Silence buffers feed zero energy (the level decays    |
|||   and effects fall back to their base layer). Default-  |
|||   device changes (IMMNotificationClient) and invalidated|
|||   streams both trigger a reinit. No captured audio is   |
|||   stored — only energies and derived events cross the   |
|||   boundary (plan privacy rule).                         |
|||                                                           |
|||   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include "InputBus.h"
#include "OnsetDetect.h"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

namespace studio
{

class AudioLoopback
{
public:
    AudioLoopback() = default;
    ~AudioLoopback();

    bool        Start(InputBus* bus);   /* false if already running  */
    void        Stop();                 /* joins the capture thread  */
    bool        Running() const { return running.load(); }

    void        SetSensitivity(float s) { sens.store(s); }
    std::string Status() const;         /* last error/state for UI   */

private:
    void        ThreadMain();
    bool        Pump();                 /* one capture session; false=stop */
    void        SetStatus(const std::string& s);

    InputBus*          bus      = nullptr;
    std::thread        th;
    std::atomic<bool>  running  { false };
    std::atomic<bool>  reinit   { false };
    std::atomic<float> sens     { 1.0f };
    OnsetDetect        onset;                   /* capture thread only */
    mutable std::mutex status_mu;
    std::string        status;
};

} /* namespace studio */

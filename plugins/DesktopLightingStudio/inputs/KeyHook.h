/*---------------------------------------------------------*\
||| KeyHook.h                                                 |
|||                                                           |
|||   WH_KEYBOARD_LL low-level keyboard hook on a dedicated |
|||   thread with its own message loop (LL hooks require    |
|||   the installing thread to pump messages). Pushes       |
|||   transient "key" events carrying only the VK code —    |
|||   the bridge resolves them to scene positions and the   |
|||   code is then discarded. No typed-text history is      |
|||   retained (plan privacy rule).                         |
|||                                                           |
|||   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include "InputBus.h"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

namespace studio
{

class KeyHook
{
public:
    KeyHook() = default;
    ~KeyHook();

    bool        Start(InputBus* bus);   /* false if already running   */
    void        Stop();                 /* unhooks on its own thread  */
    bool        Running() const { return running.load(); }
    std::string Status() const;

private:
    void        ThreadMain();
    void        SetStatus(const std::string& s);

    InputBus*          bus     = nullptr;
    std::thread        th;
    std::atomic<bool>  running { false };
    std::atomic<unsigned long> tid { 0 };
    mutable std::mutex status_mu;
    std::string        status;
};

} /* namespace studio */

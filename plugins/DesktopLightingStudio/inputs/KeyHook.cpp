/*---------------------------------------------------------*\
||| KeyHook.cpp                                               |
|||                                                           |
|||   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "KeyHook.h"

#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <chrono>

namespace studio
{

/*---------------------------------------------------------*\
||| Hook state — file-local, touched only on the hook      |
||| thread (the LL callback runs inside that thread's      |
||| message pump). One hook exists per process.            |
\*---------------------------------------------------------*/
struct HookState
{
    InputBus* bus = nullptr;
    bool      down[256] = {};       /* auto-repeat suppression */
};

static HookState g_hook;

static LRESULT CALLBACK LowLevelKeyProc(int code, WPARAM w, LPARAM l)
{
    if(code == HC_ACTION)
    {
        const KBDLLHOOKSTRUCT* kb = reinterpret_cast<const KBDLLHOOKSTRUCT*>(l);
        const int vk = (int)kb->vkCode;
        if(vk >= 0 && vk <= 255)
        {
            if(w == WM_KEYDOWN || w == WM_SYSKEYDOWN)
            {
                /* Only the first edge of a held key spawns a
                   ripple — auto-repeat repeats keydown. */
                if(!g_hook.down[vk])
                {
                    g_hook.down[vk] = true;
                    if(g_hook.bus != nullptr)
                    {
                        g_hook.bus->PushEvent("key", 1.0f, vk);
                    }
                }
            }
            else if(w == WM_KEYUP || w == WM_SYSKEYUP)
            {
                g_hook.down[vk] = false;
            }
        }
    }
    return CallNextHookEx(nullptr, code, w, l);
}

KeyHook::~KeyHook()
{
    Stop();
}

std::string KeyHook::Status() const
{
    std::lock_guard<std::mutex> lock(status_mu);
    return status;
}

void KeyHook::SetStatus(const std::string& s)
{
    std::lock_guard<std::mutex> lock(status_mu);
    status = s;
}

bool KeyHook::Start(InputBus* b)
{
    if(running.load())
    {
        return false;
    }
    bus     = b;
    running = true;
    th = std::thread(&KeyHook::ThreadMain, this);
    return true;
}

void KeyHook::Stop()
{
    if(!running.load())
    {
        return;
    }
    running = false;
    /* The hook thread may not have published its id yet — give it a
       moment before posting, otherwise the quit message is lost and
       join() would hang on a live message loop. */
    unsigned long id = 0;
    for(int i = 0; i < 200 && (id = tid.load()) == 0 && th.joinable(); i++)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if(id != 0)
    {
        /* Wakes GetMessage; the hook is uninstalled on its own
           thread before it exits. */
        PostThreadMessage(id, WM_QUIT, 0, 0);
    }
    if(th.joinable())
    {
        th.join();
    }
    tid = 0;
    bus = nullptr;
}

void KeyHook::ThreadMain()
{
    /* Force the thread's message queue into existence before
       publishing the id, so Stop()'s WM_QUIT can never land before
       the queue exists and get dropped. */
    MSG msg;
    PeekMessage(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
    tid = GetCurrentThreadId();

    HHOOK hook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyProc,
                                   GetModuleHandleW(nullptr), 0);
    if(hook == nullptr)
    {
        SetStatus("keys: hook install failed");
        running = false;
        return;
    }
    g_hook.bus = bus;
    SetStatus("keys: listening");

    while(GetMessage(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    UnhookWindowsHookEx(hook);
    g_hook = HookState{};
    SetStatus("keys: off");
    running = false;
}

} /* namespace studio */

#else /* non-Windows stub */

namespace studio
{

KeyHook::~KeyHook() { Stop(); }
std::string KeyHook::Status() const { return "keys: unsupported platform"; }
void KeyHook::SetStatus(const std::string&) {}
bool KeyHook::Start(InputBus*) { return false; }
void KeyHook::Stop() {}
void KeyHook::ThreadMain() {}

} /* namespace studio */

#endif

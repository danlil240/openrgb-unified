/*---------------------------------------------------------*\
||| KeyHook.cpp                                               |
|||                                                           |
|||   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "KeyHook.h"

/*---------------------------------------------------------*\
||| Shared methods — identical on every platform.         |
|||   Start/Stop stay inside the per-OS blocks: the        |
|||   worker-wake and permission-preflight steps differ.   |
\*---------------------------------------------------------*/
namespace studio
{

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

} /* namespace studio */

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

#elif defined(__linux__)

#include "KeyTranslate.h"

#include <linux/input.h>
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include <vector>
#include <cstring>
#include <cerrno>
#include <cstdio>
#include <chrono>
#include <thread>

namespace studio
{

/* A node counts as a keyboard if EV_KEY is supported and the key
   bitmap spans a real alpha range (KEY_A..KEY_ENTER present). */
static bool IsKeyboard(int fd)
{
    unsigned long types[(EV_MAX + 63) / 64] = {};
    if(ioctl(fd, EVIOCGBIT(0, sizeof(types)), types) < 0) return false;
    if(!(types[EV_KEY / 64] & (1UL << (EV_KEY % 64))))    return false;
    unsigned long keys[(KEY_MAX + 63) / 64] = {};
    if(ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keys)), keys) < 0) return false;
    auto has = [&](int k){ return keys[k/64] & (1UL << (k%64)); };
    return has(KEY_A) && has(KEY_Z) && has(KEY_ENTER);
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
    if(th.joinable())
    {
        th.join();
    }
    bus = nullptr;
}

void KeyHook::ThreadMain()
{
    /* Passive reads of every /dev/input/event* node whose EV_KEY
       bitmap covers a keyboard. No EVIOCGRAB — observe, never steal. */
    std::vector<int> fds;
    bool denied = false;
    for(int i = 0; i < 64 && fds.size() < 8; i++)
    {
        char path[64];
        std::snprintf(path, sizeof(path), "/dev/input/event%d", i);
        const int fd = open(path, O_RDONLY | O_NONBLOCK);
        if(fd < 0)
        {
            if(errno == EACCES || errno == EPERM)
            {
                denied = true;
            }
            continue;
        }
        if(IsKeyboard(fd))
        {
            fds.push_back(fd);
        }
        else
        {
            close(fd);
        }
    }
    if(fds.empty())
    {
        /* Denied nodes may hide real keyboards — name the fix first. */
        SetStatus(denied
            ? "keys: /dev/input denied — add user to 'input' group, relogin"
            : "keys: no evdev keyboard");
        running = false;
        return;
    }

    std::vector<pollfd> pfds(fds.size());
    for(size_t i = 0; i < fds.size(); i++)
    {
        pfds[i].fd     = fds[i];
        pfds[i].events = POLLIN;
    }
    bool down[KEY_MAX + 1] = {};    /* auto-repeat suppression */
    SetStatus("keys: listening (" + std::to_string(fds.size())
              + (fds.size() == 1 ? " device)" : " devices)"));

    while(running.load())
    {
        /* 100ms timeout lets Stop() exit promptly. */
        const int n = poll(pfds.data(), pfds.size(), 100);
        if(n < 0)
        {
            if(errno == EINTR)
            {
                continue;
            }
            break;
        }
        if(n == 0)
        {
            continue;
        }
        for(pollfd& p : pfds)
        {
            if(p.fd < 0 || (p.revents & (POLLIN | POLLERR | POLLHUP)) == 0)
            {
                continue;
            }
            struct input_event ev[32];
            const ssize_t r = read(p.fd, ev, sizeof(ev));
            if(r < 0)
            {
                if(errno == ENODEV || errno == EBADF)
                {
                    close(p.fd);        /* unplugged mid-session */
                    p.fd = -1;
                }
                continue;
            }
            for(int j = 0; j < r / (ssize_t)sizeof(input_event); j++)
            {
                if(ev[j].type != EV_KEY || ev[j].code > KEY_MAX)
                {
                    continue;
                }
                const int code = ev[j].code;
                if(ev[j].value == 0)
                {
                    down[code] = false;
                }
                else if(ev[j].value == 1 && !down[code])
                {
                    down[code] = true;
                    const int vk = VkForEvdevCode(code);
                    if(vk > 0 && bus != nullptr)
                    {
                        bus->PushEvent("key", 1.0f, vk);
                    }
                }
                /* value == 2 (autorepeat) ignored */
            }
            if(p.revents & (POLLERR | POLLHUP))
            {
                close(p.fd);
                p.fd = -1;
            }
        }
    }
    for(int fd : fds)
    {
        close(fd);
    }
    SetStatus("keys: off");
    running = false;
}

} /* namespace studio */

#elif defined(__APPLE__)

#include "KeyTranslate.h"

#include <ApplicationServices/ApplicationServices.h>

#include <chrono>
#include <thread>

namespace studio
{

/* Pure C API — no .mm needed. The tap is listen-only: events are
   observed, never consumed. */
struct MacHook
{
    InputBus*       bus  = nullptr;
    CFRunLoopRef    loop = nullptr;
    CFMachPortRef   tap  = nullptr;
    bool            down[256] = {};   /* auto-repeat suppression */
};
static MacHook g_mac;

static CGEventRef MacKeyProc(CGEventTapProxy, CGEventType type,
                             CGEventRef ev, void*)
{
    if(type == kCGEventTapDisabledByTimeout)
    {   /* macOS may time-out the tap — re-arm instead of dying */
        if(g_mac.tap != nullptr)
        {
            CGEventTapEnable(g_mac.tap, true);
        }
        return ev;
    }
    const int code = (int)CGEventGetIntegerValueField(
                         ev, kCGKeyboardEventKeycode);
    if(code >= 0 && code <= 255)
    {
        if(type == kCGEventKeyDown && !g_mac.down[code])
        {
            g_mac.down[code] = true;
            const int vk = VkForMacCode(code);
            if(vk > 0 && g_mac.bus != nullptr)
            {
                g_mac.bus->PushEvent("key", 1.0f, vk);
            }
        }
        else if(type == kCGEventKeyUp)
        {
            g_mac.down[code] = false;
        }
    }
    return ev;   /* listen-only — event is passed through */
}

bool KeyHook::Start(InputBus* b)
{
    if(running.load())
    {
        return false;
    }
    /* One system prompt up front; the tap itself reports the denied
       status if the grant stays withheld. */
    if(!CGPreflightListenEventAccess())
    {
        CGRequestListenEventAccess();
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
    /* The hook thread may not have published its run loop yet —
       give it a moment, then wake it so CFRunLoopRun returns. */
    for(int i = 0; i < 200 && g_mac.loop == nullptr && th.joinable(); i++)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if(g_mac.loop != nullptr)
    {
        CFRunLoopStop(g_mac.loop);
        CFRunLoopWakeUp(g_mac.loop);
    }
    if(th.joinable())
    {
        th.join();
    }
    bus = nullptr;
}

void KeyHook::ThreadMain()
{
    g_mac.loop = CFRunLoopGetCurrent();
    g_mac.bus  = bus;

    g_mac.tap = CGEventTapCreate(
        kCGSessionEventTap, kCGHeadInsertEventTap,
        kCGEventTapOptionListenOnly,
        CGEventMaskBit(kCGEventKeyDown) | CGEventMaskBit(kCGEventKeyUp),
        MacKeyProc, nullptr);
    if(g_mac.tap == nullptr)
    {
        SetStatus("keys: input-monitoring permission denied — enable in "
                  "System Settings > Privacy");
        g_mac.loop = nullptr;
        running = false;
        return;
    }
    CFRunLoopSourceRef src = CFMachPortCreateRunLoopSource(
        kCFAllocatorDefault, g_mac.tap, 0);
    CFRunLoopAddSource(g_mac.loop, src, kCFRunLoopCommonModes);
    CGEventTapEnable(g_mac.tap, true);
    SetStatus("keys: listening");

    CFRunLoopRun();

    CGEventTapEnable(g_mac.tap, false);
    CFRunLoopRemoveSource(g_mac.loop, src, kCFRunLoopCommonModes);
    CFRelease(src);
    CFRelease(g_mac.tap);
    g_mac = MacHook{};
    SetStatus("keys: off");
    running = false;
}

} /* namespace studio */

#else /* unsupported platform — report honestly, never crash */

namespace studio
{

bool KeyHook::Start(InputBus*)
{
    SetStatus("keys: unsupported platform");
    return false;
}
void KeyHook::Stop() {}
void KeyHook::ThreadMain() {}

} /* namespace studio */

#endif

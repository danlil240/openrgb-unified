#include "../OpenRGB/startup/WindowsLaunchPolicy.h"
#include "../OpenRGB/startup/WindowsLaunchLock.h"
#include <cstdio>
#include <thread>

int main()
{
    using A = WindowsLaunchAction;
    struct Case { const char* name; bool others, server, gui, request_gui; A expected; };
    const Case cases[] = {
        {"nothing running", false, false, false, true, A::Start},
        {"server only", true, true, false, true, A::ConnectGui},
        {"GUI and server", true, true, true, true, A::Restart},
        {"GUI only", true, false, true, true, A::Restart},
        {"server still starting", true, false, false, true, A::Restart},
        {"duplicate headless server", true, true, false, false, A::Restart},
        {"unidentified port owner", false, true, false, true, A::Restart},
    };
    int failures = 0;
    for(const auto& c : cases)
        if(GetWindowsLaunchAction(c.others, c.server, c.gui, c.request_gui) != c.expected)
        {
            std::printf("FAIL: %s\n", c.name);
            ++failures;
        }
    char lock_name[128];
    std::snprintf(lock_name, sizeof(lock_name), "Local\\OpenRGB.LaunchTest.%lu", GetCurrentProcessId());
    WindowsLaunchLock first;
    if(first.Acquire(lock_name) != WAIT_OBJECT_0)
        ++failures;
    DWORD second_result = WAIT_FAILED;
    std::thread contender([&] {
        WindowsLaunchLock second;
        second_result = second.Acquire(lock_name);
    });
    contender.join();
    if(second_result != WAIT_TIMEOUT)
    {
        std::puts("FAIL: simultaneous startup admitted");
        ++failures;
    }
    first.Release();
    std::thread next_launch([&] {
        WindowsLaunchLock next;
        second_result = next.Acquire(lock_name);
    });
    next_launch.join();
    if(second_result != WAIT_OBJECT_0)
    {
        std::puts("FAIL: startup lock was not released");
        ++failures;
    }
    std::printf("7 launch cases + lock contention/release, %d failures\n", failures);
    return failures ? 1 : 0;
}

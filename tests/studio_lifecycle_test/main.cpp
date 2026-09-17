/*---------------------------------------------------------*\
|| studio_lifecycle_test — Task 6.1 lifecycle hardening      ||
||                                                           ||
|| Drives a REAL SceneBridge against a fake OpenRGB plugin   ||
|| API and a fake RGBControllerInterface. No hardware, no    ||
|| QML — the output-lifecycle seams (lane workers, probe     ||
|| pause/resume, hidden-preview gating, destructor joins)    ||
|| all live in SceneBridge + ControllerAdapter.              ||
\*---------------------------------------------------------*/

#include <QtTest>
#include <QTemporaryDir>
#include <QTimer>

#include <atomic>
#include <chrono>
#include <thread>

#include "OpenRGBPluginInterface.h"
#include "../../plugins/DesktopLightingStudio/plugin/SceneBridge.h"

using namespace std::chrono_literals;

/*---------------------------------------------------------*\
|| Fake controller: counts writes, can gate a worker inside  ||
|| UpdateZoneLEDs (destruction-mid-write test) and can throw ||
|| like a flaky driver.                                      ||
\*---------------------------------------------------------*/
class FakeController : public RGBControllerInterface
{
public:
    struct FZone { std::string name; unsigned int leds; };

    std::string              ctrl_name;
    std::string              ctrl_vendor;
    std::string              ctrl_serial;
    std::string              ctrl_location;
    device_type              ctrl_type = DEVICE_TYPE_MOTHERBOARD;
    std::vector<FZone>       fzones;
    std::vector<unsigned int> zone_start;
    std::vector<RGBColor>    led_colors;
    int                      active_mode = 0;
    bool                     hidden      = false;

    std::atomic<int>         update_attempts { 0 };
    std::atomic<int>         updates         { 0 };
    std::atomic<int>         color_writes    { 0 };
    std::atomic<int>         write_delay_ms  { 0 };
    std::atomic<bool>        gate_writes     { false };
    std::atomic<bool>        write_entered   { false };
    std::atomic<bool>        throw_on_write  { false };

    FakeController(const std::string& name, const std::string& vendor,
                   const std::string& location,
                   std::initializer_list<FZone> zones)
        : ctrl_name(name), ctrl_vendor(vendor), ctrl_location(location)
    {
        unsigned int start = 0;
        for(const FZone& z : zones)
        {
            zone_start.push_back(start);
            fzones.push_back(z);
            start += z.leds;
        }
        led_colors.assign(start, 0);
    }

    /*-- identity ------------------------------------------*/
    std::string GetName() override        { return ctrl_name; }
    std::string GetVendor() override      { return ctrl_vendor; }
    std::string GetDescription() override { return "fake"; }
    std::string GetVersion() override     { return "1.0"; }
    std::string GetSerial() override      { return ctrl_serial; }
    std::string GetLocation() override    { return ctrl_location; }
    std::string GetDisplayName() override { return ctrl_name; }
    device_type GetDeviceType() override  { return ctrl_type; }
    controller_flags GetFlags() override  { return 0; }
    bool        GetHidden() override      { return hidden; }
    void        SetHidden(bool h) override { hidden = h; }

    /*-- zones ----------------------------------------------*/
    zone        GetZone(unsigned int z) override            { (void)z; return zone(); }
    int         GetZoneActiveMode(unsigned int z) override  { (void)z; return 0; }
    RGBColor    GetZoneColor(unsigned int z, unsigned int i) override
    {
        (void)z; (void)i; return 0;
    }
    RGBColor*   GetZoneColorsPointer(unsigned int z) override
    {
        (void)z; return nullptr;
    }
    unsigned int GetZoneCount() override            { return (unsigned int)fzones.size(); }
    std::string  GetZoneDisplayName(unsigned int z) override { return GetZoneName(z); }
    zone_flags   GetZoneFlags(unsigned int z) override       { (void)z; return 0; }
    unsigned int GetZoneLEDsCount(unsigned int z) override   { return fzones[z].leds; }
    unsigned int GetZoneLEDsMax(unsigned int z) override     { return fzones[z].leds; }
    unsigned int GetZoneLEDsMin(unsigned int z) override     { return fzones[z].leds; }
    matrix_map_type        GetZoneMatrixMap(unsigned int z) override
    {
        (void)z; return matrix_map_type();
    }
    const unsigned int*    GetZoneMatrixMapData(unsigned int z) override
    {
        (void)z; return nullptr;
    }
    unsigned int GetZoneMatrixMapHeight(unsigned int z) override { (void)z; return 0; }
    unsigned int GetZoneMatrixMapWidth(unsigned int z) override  { (void)z; return 0; }
    unsigned int GetZoneModeCount(unsigned int z) override       { (void)z; return 1; }
    unsigned int GetZoneModeBrightness(unsigned int z, unsigned int m) override
    {
        (void)z; (void)m; return 100;
    }
    unsigned int GetZoneModeBrightnessMax(unsigned int z, unsigned int m) override
    {
        (void)z; (void)m; return 100;
    }
    unsigned int GetZoneModeBrightnessMin(unsigned int z, unsigned int m) override
    {
        (void)z; (void)m; return 0;
    }
    RGBColor     GetZoneModeColor(unsigned int z, unsigned int m, unsigned int i) override
    {
        (void)z; (void)m; (void)i; return 0;
    }
    unsigned int GetZoneModeColorMode(unsigned int z, unsigned int m) override
    {
        (void)z; (void)m; return 0;
    }
    unsigned int GetZoneModeColorsCount(unsigned int z, unsigned int m) override
    {
        (void)z; (void)m; return 0;
    }
    unsigned int GetZoneModeColorsMax(unsigned int z, unsigned int m) override
    {
        (void)z; (void)m; return 0;
    }
    unsigned int GetZoneModeColorsMin(unsigned int z, unsigned int m) override
    {
        (void)z; (void)m; return 0;
    }
    unsigned int GetZoneModeDirection(unsigned int z, unsigned int m) override
    {
        (void)z; (void)m; return 0;
    }
    unsigned int GetZoneModeFlags(unsigned int z, unsigned int m) override
    {
        (void)z; (void)m; return MODE_FLAG_HAS_PER_LED_COLOR;
    }
    std::string  GetZoneModeName(unsigned int z, unsigned int m) override
    {
        (void)z; (void)m; return "Direct";
    }
    unsigned int GetZoneModeSpeed(unsigned int z, unsigned int m) override
    {
        (void)z; (void)m; return 0;
    }
    unsigned int GetZoneModeSpeedMax(unsigned int z, unsigned int m) override
    {
        (void)z; (void)m; return 0;
    }
    unsigned int GetZoneModeSpeedMin(unsigned int z, unsigned int m) override
    {
        (void)z; (void)m; return 0;
    }
    std::string  GetZoneName(unsigned int z) override       { return fzones[z].name; }
    unsigned int GetZoneSegmentCount(unsigned int z) override      { (void)z; return 0; }
    segment_flags GetZoneSegmentFlags(unsigned int z, unsigned int s) override
    {
        (void)z; (void)s; return 0;
    }
    unsigned int GetZoneSegmentLEDsCount(unsigned int z, unsigned int s) override
    {
        (void)z; (void)s; return 0;
    }
    matrix_map_type GetZoneSegmentMatrixMap(unsigned int z, unsigned int s) override
    {
        (void)z; (void)s; return matrix_map_type();
    }
    const unsigned int* GetZoneSegmentMatrixMapData(unsigned int z, unsigned int s) override
    {
        (void)z; (void)s; return nullptr;
    }
    unsigned int GetZoneSegmentMatrixMapHeight(unsigned int z, unsigned int s) override
    {
        (void)z; (void)s; return 0;
    }
    unsigned int GetZoneSegmentMatrixMapWidth(unsigned int z, unsigned int s) override
    {
        (void)z; (void)s; return 0;
    }
    std::string  GetZoneSegmentName(unsigned int z, unsigned int s) override
    {
        (void)z; (void)s; return "";
    }
    unsigned int GetZoneSegmentStartIndex(unsigned int z, unsigned int s) override
    {
        (void)z; (void)s; return 0;
    }
    unsigned int GetZoneSegmentType(unsigned int z, unsigned int s) override
    {
        (void)z; (void)s; return 0;
    }
    unsigned int GetZoneStartIndex(unsigned int z) override { return zone_start[z]; }
    zone_type    GetZoneType(unsigned int z) override       { (void)z; return ZONE_TYPE_LINEAR; }
    unsigned int GetLEDsInZone(unsigned int z) override     { return fzones[z].leds; }

    void SetZoneActiveMode(unsigned int z, int m) override { (void)z; (void)m; }
    void SetZoneColor(unsigned int z, unsigned int i, RGBColor c) override
    {
        SetColor(zone_start[z] + i, c);
    }
    void SetZoneModeBrightness(unsigned int z, unsigned int m, unsigned int v) override
    {
        (void)z; (void)m; (void)v;
    }
    void SetZoneModeColor(unsigned int z, unsigned int m, unsigned int i, RGBColor c) override
    {
        (void)z; (void)m; (void)i; (void)c;
    }
    void SetZoneModeColorMode(unsigned int z, unsigned int m, unsigned int cm) override
    {
        (void)z; (void)m; (void)cm;
    }
    void SetZoneModeColorsCount(unsigned int z, unsigned int m, unsigned int n) override
    {
        (void)z; (void)m; (void)n;
    }
    void SetZoneModeDirection(unsigned int z, unsigned int m, unsigned int d) override
    {
        (void)z; (void)m; (void)d;
    }
    void SetZoneModeSpeed(unsigned int z, unsigned int m, unsigned int s) override
    {
        (void)z; (void)m; (void)s;
    }
    bool SupportsPerZoneModes() override { return true; }

    /*-- device modes ----------------------------------------*/
    unsigned int GetModeCount() override                          { return 1; }
    unsigned int GetModeBrightness(unsigned int m) override       { (void)m; return 100; }
    unsigned int GetModeBrightnessMax(unsigned int m) override    { (void)m; return 100; }
    unsigned int GetModeBrightnessMin(unsigned int m) override    { (void)m; return 0; }
    RGBColor     GetModeColor(unsigned int m, unsigned int i) override
    {
        (void)m; (void)i; return 0;
    }
    unsigned int GetModeColorMode(unsigned int m) override        { (void)m; return 0; }
    unsigned int GetModeColorsCount(unsigned int m) override      { (void)m; return 0; }
    unsigned int GetModeColorsMax(unsigned int m) override        { (void)m; return 0; }
    unsigned int GetModeColorsMin(unsigned int m) override        { (void)m; return 0; }
    unsigned int GetModeDirection(unsigned int m) override        { (void)m; return 0; }
    unsigned int GetModeFlags(unsigned int m) override
    {
        (void)m; return MODE_FLAG_HAS_PER_LED_COLOR;
    }
    std::string  GetModeName(unsigned int m) override      { (void)m; return "Direct"; }
    unsigned int GetModeSpeed(unsigned int m) override     { (void)m; return 0; }
    unsigned int GetModeSpeedMax(unsigned int m) override  { (void)m; return 0; }
    unsigned int GetModeSpeedMin(unsigned int m) override  { (void)m; return 0; }
    void SetModeBrightness(unsigned int m, unsigned int v) override { (void)m; (void)v; }
    void SetModeColor(unsigned int m, unsigned int i, RGBColor c) override
    {
        (void)m; (void)i; (void)c;
    }
    void SetModeColorMode(unsigned int m, unsigned int cm) override { (void)m; (void)cm; }
    void SetModeColorsCount(unsigned int m, unsigned int n) override { (void)m; (void)n; }
    void SetModeDirection(unsigned int m, unsigned int d) override  { (void)m; (void)d; }
    void SetModeSpeed(unsigned int m, unsigned int s) override      { (void)m; (void)s; }
    int  GetActiveMode() override            { return active_mode; }
    void SetActiveMode(int m) override       { active_mode = m; }
    void SetCustomMode() override            {}

    /*-- LEDs ------------------------------------------------*/
    unsigned int GetLEDCount() override { return (unsigned int)led_colors.size(); }
    std::string  GetLEDName(unsigned int l) override
    {
        return "LED " + std::to_string(l);
    }
    std::string  GetLEDDisplayName(unsigned int l) override { return GetLEDName(l); }
    RGBColor     GetColor(unsigned int l) override          { return led_colors[l]; }
    RGBColor*    GetColorsPointer() override                { return led_colors.data(); }
    void         SetColor(unsigned int l, RGBColor c) override
    {
        color_writes.fetch_add(1);
        if(l < led_colors.size())
        {
            led_colors[l] = c;
        }
    }
    void SetAllColors(RGBColor c) override
    {
        for(RGBColor& v : led_colors) { v = c; }
        color_writes.fetch_add(1);
    }
    void SetAllZoneColors(int z, RGBColor c) override
    {
        if(z >= 0 && (unsigned int)z < fzones.size())
        {
            for(unsigned int i = 0; i < fzones[z].leds; i++)
            {
                led_colors[zone_start[z] + i] = c;
            }
            color_writes.fetch_add(1);
        }
    }

    /*-- config / callbacks -----------------------------------*/
    nlohmann::json GetDeviceSpecificConfigurationSchema() override { return {}; }
    nlohmann::json GetDeviceSpecificConfiguration() override       { return {}; }
    void SetDeviceSpecificConfiguration(nlohmann::json j) override { (void)j; }
    nlohmann::json GetDeviceSpecificZoneConfigurationSchema(int z) override
    {
        (void)z; return {};
    }
    nlohmann::json GetDeviceSpecificZoneConfiguration(int z) override
    {
        (void)z; return {};
    }
    void SetDeviceSpecificZoneConfiguration(int z, nlohmann::json j) override
    {
        (void)z; (void)j;
    }
    void RegisterUpdateCallback(RGBControllerCallback cb, void* arg) override
    {
        (void)cb; (void)arg;
    }
    void UnregisterUpdateCallback(void* arg) override { (void)arg; }
    void ClearCallbacks() override                    {}
    void SignalUpdate(unsigned int r) override        { (void)r; }

    /*-- the write seam under test -----------------------------*/
    void UpdateLEDs() override { WriteCommon(); }
    void UpdateZoneLEDs(int z) override { (void)z; WriteCommon(); }
    void UpdateSingleLED(int l) override { (void)l; WriteCommon(); }
    void UpdateMode() override          {}
    void UpdateZoneMode(int z) override { (void)z; }
    void SaveMode() override            {}
    void ClearSegments(int z) override  { (void)z; }
    void AddSegment(int z, segment s) override { (void)z; (void)s; }
    void ConfigureZone(int z, zone nz) override { (void)z; (void)nz; }
    void ResizeZone(int z, int n) override { (void)z; (void)n; }
    void ConfigureDevice(controller_flags f, std::string n) override
    {
        (void)f; (void)n;
    }

private:
    void WriteCommon()
    {
        update_attempts.fetch_add(1);
        write_entered = true;
        /* Gate with a hard cap — a broken test must not hang the
           suite forever. */
        for(int i = 0; gate_writes.load() && i < 10000; i++)
        {
            std::this_thread::sleep_for(1ms);
        }
        if(throw_on_write.load())
        {
            throw std::runtime_error("simulated driver fault");
        }
        const int d = write_delay_ms.load();
        if(d > 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(d));
        }
        updates.fetch_add(1);
    }
};

/*---------------------------------------------------------*\
|| Fake plugin API. GetConfigurationDirectory lands in a     ||
|| QTemporaryDir so nothing touches the real config store.   ||
\*---------------------------------------------------------*/
class FakeApi : public OpenRGBPluginAPIInterface
{
public:
    filesystem::path                     cfg_dir;
    std::vector<RGBControllerInterface*> ctrls;

    void LogEntry(const char*, int, unsigned int, const char*, ...) override {}

    RGBControllerInterface* CreateVirtualRGBController(RGBController_Setup*) override
    {
        return nullptr;
    }
    void DeleteVirtualRGBController(RGBControllerInterface*) override           {}
    void RegisterVirtualRGBController(RGBControllerInterface*) override         {}
    void RegisterVirtualRGBControllerInThread(RGBControllerInterface*) override {}
    void UnregisterVirtualRGBController(RGBControllerInterface*) override       {}
    void UpdateVirtualRGBController(RGBControllerInterface*,
                                    RGBController_Setup*) override              {}
    void UnregisterVirtualRGBControllerInThread(RGBControllerInterface*) override {}

    void ClearActiveProfile() override                          {}
    std::vector<std::string> GetProfileList() override          { return {}; }
    bool LoadProfile(std::string) override                      { return false; }
    bool SaveProfileFromPlugin(std::string, std::string,
                               nlohmann::json) override         { return false; }

    filesystem::path GetConfigurationDirectory() override       { return cfg_dir; }
    bool GetDetectionEnabled() override                         { return false; }
    unsigned int GetDetectionPercent() override                 { return 100; }
    std::string GetDetectionString() override                   { return ""; }
    void RescanDevices() override                               {}
    void WaitForDetection() override                            {}
    std::vector<RGBControllerInterface*> GetRGBControllers() override
    {
        return ctrls;
    }

    nlohmann::json GetDeviceDescriptionJSON(RGBControllerInterface*) override { return {}; }
    nlohmann::json GetLEDDescriptionJSON(led) override                        { return {}; }
    nlohmann::json GetMatrixMapDescriptionJSON(matrix_map_type) override      { return {}; }
    nlohmann::json GetModeDescriptionJSON(mode) override                      { return {}; }
    nlohmann::json GetSegmentDescriptionJSON(segment) override                { return {}; }
    nlohmann::json GetZoneDescriptionJSON(zone) override                      { return {}; }

    RGBControllerInterface* SetDeviceDescriptionJSON(nlohmann::json) override
    {
        return nullptr;
    }
    led            SetLEDDescriptionJSON(nlohmann::json) override        { return led(); }
    matrix_map_type SetMatrixMapDescriptionJSON(nlohmann::json) override
    {
        return matrix_map_type();
    }
    mode           SetModeDescriptionJSON(nlohmann::json) override       { return mode(); }
    segment        SetSegmentDescriptionJSON(nlohmann::json) override    { return segment(); }
    zone           SetZoneDescriptionJSON(nlohmann::json) override       { return zone(); }

    bool CompareControllers(RGBControllerInterface* a,
                            RGBControllerInterface* b) override          { return a == b; }
    std::string DeviceTypeToString(device_type) override                 { return "fake"; }
    bool SetModeValuesFromMode(mode& dst, mode& src) override
    {
        dst = src; return true;
    }

    nlohmann::json GetSettings(std::string) override                     { return {}; }
    void SaveSettings() override                                         {}
    void SetSettings(std::string, nlohmann::json) override               {}
};

/*---------------------------------------------------------*\
|| Environment: temp config dir + a Gigabyte-board fake      ||
|| matching the default workspace's argb_v2_* bindings.      ||
\*---------------------------------------------------------*/
struct Env
{
    QTemporaryDir                   dir;
    FakeApi                         api;
    std::unique_ptr<FakeController> mb;

    Env()
    {
        api.cfg_dir = dir.path().toStdWString();
        MakeBoard();
    }

    std::unique_ptr<FakeController> MakeBoard()
    {
        /* Same name/vendor/zones the default workspace binds. */
        return std::make_unique<FakeController>(
            "X870E AORUS ELITE", "Gigabyte", "USB: test-board",
            std::initializer_list<FakeController::FZone>{
                { "ARGB_V2_1", 8 }, { "ARGB_V2_2", 16 }, { "ARGB_V2_3", 8 } });
    }
};

/* The bridge's 16 ms play timer — a direct child. */
static QTimer* PlayTimer(studio::SceneBridge* b)
{
    const QList<QTimer*> timers =
        b->findChildren<QTimer*>(QString(), Qt::FindDirectChildrenOnly);
    for(QTimer* t : timers)
    {
        if(t->interval() == 16)
        {
            return t;
        }
    }
    return nullptr;
}

class LifecycleTest : public QObject
{
    Q_OBJECT

private slots:
    void typeLibraryLoaded();
    void livePushReachesHardware();
    void liveStopRestart();
    void probePauseResumeWhileLive();
    void overlappingProbesRestoreLive();
    void hiddenPreviewKeepsPushing();
    void idleTimerStopsWhenHidden();
    void destructionWithWorkerInFlight();
    void pauseFailsDuringShutdown();
    void driverThrowDoesNotWedge();
    void rescanSwapsControllers();
    void repeatedCreateDestroy();
};

void LifecycleTest::typeLibraryLoaded()
{
    /* Without the plugin qrc the workspace resolves on the
       desk-only fallback set — verify the harness really sees the
       packaged types so the push tests below are meaningful. */
    Env env;
    env.api.ctrls = { env.mb.get() };
    studio::SceneBridge bridge(&env.api);
    QVERIFY(!bridge.objectList().isEmpty());
    QVERIFY(bridge.objectList().size() > 5);
}

void LifecycleTest::livePushReachesHardware()
{
    Env env;
    env.api.ctrls = { env.mb.get() };
    studio::SceneBridge bridge(&env.api);
    QVERIFY(!bridge.live());
    bridge.setLive(true);
    QTRY_VERIFY_WITH_TIMEOUT(env.mb->updates.load() > 0, 5000);
    bridge.setLive(false);
}

void LifecycleTest::liveStopRestart()
{
    Env env;
    env.api.ctrls = { env.mb.get() };
    studio::SceneBridge bridge(&env.api);

    bridge.setLive(true);
    QTRY_VERIFY_WITH_TIMEOUT(env.mb->updates.load() > 0, 5000);
    bridge.setLive(false);
    QVERIFY(!bridge.live());

    /* Drain the in-flight worker, then prove no new writes land. */
    std::this_thread::sleep_for(300ms);
    const int settled = env.mb->updates.load();
    std::this_thread::sleep_for(300ms);
    QCOMPARE(env.mb->updates.load(), settled);

    bridge.setLive(true);
    QTRY_VERIFY_WITH_TIMEOUT(env.mb->updates.load() > settled, 5000);
    bridge.setLive(false);
}

void LifecycleTest::probePauseResumeWhileLive()
{
    Env env;
    env.api.ctrls = { env.mb.get() };
    studio::SceneBridge bridge(&env.api);
    bridge.setLive(true);
    QTRY_VERIFY_WITH_TIMEOUT(env.mb->updates.load() > 0, 5000);

    /* The diagnostics seam: pause on a worker thread, confirm the
       live flag drops, writes freeze, and resume restores. */
    std::atomic<bool> inside{ false };
    std::atomic<int>  writes_during{ -1 };
    std::atomic<bool> pause_ok{ false };
    std::thread probe([&]()
    {
        pause_ok = bridge.pausePushes();
        if(!pause_ok.load())
        {
            return;
        }
        inside = true;
        const int before = env.mb->updates.load();
        std::this_thread::sleep_for(300ms);
        writes_during = env.mb->updates.load() - before;
        bridge.resumePushes();
    });
    QTRY_VERIFY_WITH_TIMEOUT(inside.load(), 5000);
    QVERIFY(!bridge.live());                 /* flag flips synchronously */
    probe.join();
    QCOMPARE(writes_during.load(), 0);       /* lane locks held — no writes */
    QVERIFY(pause_ok.load());
    QVERIFY(bridge.live());                  /* restored synchronously */
    QTRY_VERIFY_WITH_TIMEOUT(env.mb->updates.load() > 0, 5000);
    bridge.setLive(false);
}

void LifecycleTest::overlappingProbesRestoreLive()
{
    Env env;
    env.api.ctrls = { env.mb.get() };
    studio::SceneBridge bridge(&env.api);
    bridge.setLive(true);
    QTRY_VERIFY_WITH_TIMEOUT(env.mb->updates.load() > 0, 5000);

    /* Two serialized probes: B must record live==true AFTER A's
       resume restored it — a queued restore used to leave live
       stuck off. Also prove the critical sections never overlap. */
    std::atomic<int>  in_probe{ 0 };
    std::atomic<int>  max_overlap{ 0 };
    std::atomic<bool> a_inside{ false };

    std::thread a([&]()
    {
        if(!bridge.pausePushes()) { return; }
        const int n = in_probe.fetch_add(1) + 1;
        if(n > max_overlap.load()) { max_overlap = n; }
        a_inside = true;
        std::this_thread::sleep_for(250ms);
        in_probe.fetch_sub(1);
        bridge.resumePushes();
    });
    QTRY_VERIFY_WITH_TIMEOUT(a_inside.load(), 5000);
    std::thread b([&]()
    {
        if(!bridge.pausePushes()) { return; }
        const int n = in_probe.fetch_add(1) + 1;
        if(n > max_overlap.load()) { max_overlap = n; }
        std::this_thread::sleep_for(50ms);
        in_probe.fetch_sub(1);
        bridge.resumePushes();
    });
    a.join();
    b.join();

    QCOMPARE(max_overlap.load(), 1);
    QVERIFY(bridge.live());                  /* both restores landed */
    const int settled = env.mb->updates.load();
    QTRY_VERIFY_WITH_TIMEOUT(env.mb->updates.load() > settled, 5000);
    bridge.setLive(false);
}

void LifecycleTest::hiddenPreviewKeepsPushing()
{
    Env env;
    env.api.ctrls = { env.mb.get() };
    studio::SceneBridge bridge(&env.api);

    /* An effect look gives the tick a non-empty frame — pushes then
       ride the per-lane path. */
    bridge.playPreset(QStringLiteral("aurora"));
    QVERIFY(bridge.playing());
    bridge.setLive(true);
    QTRY_VERIFY_WITH_TIMEOUT(env.mb->updates.load() > 0, 5000);

    bridge.setPreviewVisible(false);
    QSignalSpy spy(&bridge, &studio::SceneBridge::emittersChanged);
    const int before = env.mb->updates.load();
    QTest::qWait(300);
    /* Hidden: no emittersChanged repaint churn... */
    QCOMPARE(spy.count(), 0);
    /* ...while requested live playback keeps pushing hardware. */
    QVERIFY(env.mb->updates.load() > before);

    bridge.setPreviewVisible(true);
    QTRY_VERIFY_WITH_TIMEOUT(spy.count() > 0, 5000);  /* repaint resumes */
    bridge.setLive(false);
    bridge.setPlaying(false);
}

void LifecycleTest::idleTimerStopsWhenHidden()
{
    Env env;
    env.api.ctrls = { env.mb.get() };
    studio::SceneBridge bridge(&env.api);
    QTimer* pt = PlayTimer(&bridge);
    QVERIFY(pt != nullptr);

    bridge.playPreset(QStringLiteral("aurora"));
    QVERIFY(bridge.playing());
    QVERIFY(pt->isActive());                 /* visible preview ticks */

    /* Hidden + live off = no frame consumer: the timer must idle. */
    bridge.setPreviewVisible(false);
    QVERIFY(!pt->isActive());

    bridge.setPreviewVisible(true);
    QVERIFY(pt->isActive());

    /* Hidden + live ON must keep ticking — live is a consumer. */
    bridge.setLive(true);
    bridge.setPreviewVisible(false);
    QVERIFY(pt->isActive());
    bridge.setLive(false);
    QVERIFY(!pt->isActive());                /* last consumer gone */
    bridge.setPlaying(false);
}

void LifecycleTest::destructionWithWorkerInFlight()
{
    Env env;
    env.api.ctrls = { env.mb.get() };
    auto* bridge = new studio::SceneBridge(&env.api);

    /* Park a push worker inside the driver's write call, then delete
       the bridge — the dtor must join it, not abandon it. A helper
       releases the write gate so the join can finish. */
    env.mb->gate_writes = true;
    bridge->setLive(true);
    QTRY_VERIFY_WITH_TIMEOUT(env.mb->write_entered.load(), 5000);

    std::thread releaser([&]()
    {
        std::this_thread::sleep_for(250ms);
        env.mb->gate_writes = false;
    });
    QElapsedTimer t;
    t.start();
    delete bridge;                  /* blocks in ~SceneBridge's join */
    releaser.join();
    QVERIFY(t.elapsed() >= 200);    /* it really waited on the worker */
    QVERIFY(t.elapsed() < 5000);    /* but did not wedge */
}

void LifecycleTest::pauseFailsDuringShutdown()
{
    Env env;
    env.api.ctrls = { env.mb.get() };
    auto* bridge = new studio::SceneBridge(&env.api);

    env.mb->gate_writes = true;
    bridge->setLive(true);
    QTRY_VERIFY_WITH_TIMEOUT(env.mb->write_entered.load(), 5000);

    /* Once ~SceneBridge begins (shutting_down set), a probe arriving
       from another thread must get pausePushes()==false — never a
       pause that outlives the bridge. The gate guarantees the dtor
       is still inside its join when the probe calls in, and the
       probe never touches the bridge after pause_done. */
    std::atomic<bool> pause_done{ false };
    std::atomic<bool> pause_ok{ true };
    std::thread prober([&]()
    {
        while(!bridge->closing())
        {
            std::this_thread::sleep_for(1ms);
        }
        pause_ok = bridge->pausePushes();
        pause_done = true;
    });
    std::thread releaser([&]()
    {
        while(!pause_done.load())
        {
            std::this_thread::sleep_for(1ms);
        }
        env.mb->gate_writes = false;
    });
    delete bridge;
    prober.join();
    releaser.join();
    QVERIFY(pause_done.load());
    QVERIFY(!pause_ok.load());
}

void LifecycleTest::driverThrowDoesNotWedge()
{
    Env env;
    env.api.ctrls = { env.mb.get() };
    studio::SceneBridge bridge(&env.api);

    /* A throw inside a detached worker must be caught, must not be
       terminate, and must release the coalescing flags — the next
       push has to flow. */
    env.mb->throw_on_write = true;
    bridge.setLive(true);
    QTRY_VERIFY_WITH_TIMEOUT(env.mb->update_attempts.load() > 0, 5000);
    bridge.setLive(false);
    std::this_thread::sleep_for(200ms);

    env.mb->throw_on_write = false;
    bridge.setLive(true);
    QTRY_VERIFY_WITH_TIMEOUT(env.mb->updates.load() > 0, 5000);

    /* Same guarantee on the lane path (effect frame up). */
    env.mb->throw_on_write = true;
    const int attempts = env.mb->update_attempts.load();
    bridge.playPreset(QStringLiteral("aurora"));
    QTRY_VERIFY_WITH_TIMEOUT(env.mb->update_attempts.load() > attempts, 5000);
    env.mb->throw_on_write = false;
    const int settled = env.mb->updates.load();
    QTRY_VERIFY_WITH_TIMEOUT(env.mb->updates.load() > settled, 5000);
    bridge.setLive(false);
    bridge.setPlaying(false);
}

void LifecycleTest::rescanSwapsControllers()
{
    Env env;
    env.api.ctrls = { env.mb.get() };
    studio::SceneBridge bridge(&env.api);
    bridge.setLive(true);
    QTRY_VERIFY_WITH_TIMEOUT(env.mb->updates.load() > 0, 5000);
    bridge.setLive(false);
    std::this_thread::sleep_for(200ms);      /* drain in-flight worker */

    /* Disconnect/reconnect: the list now holds a NEW object — the
       adapter must not write through the stale pointer. */
    auto mb2 = env.MakeBoard();
    env.api.ctrls = { mb2.get() };
    bridge.refreshDevices();

    bridge.setLive(true);
    QTRY_VERIFY_WITH_TIMEOUT(mb2->updates.load() > 0, 5000);
    const int old_writes = env.mb->updates.load();
    QTest::qWait(200);
    QCOMPARE(env.mb->updates.load(), old_writes);

    /* The old controller is freed while live is on — the adapter
       snapshot dropped it at refreshDevices(), so this must be a
       non-event for the bridge. */
    bridge.setLive(false);
    std::this_thread::sleep_for(200ms);
    env.mb.reset();                          /* freed, like a disconnect */
    bridge.setLive(true);
    const int settled = mb2->updates.load();
    QTRY_VERIFY_WITH_TIMEOUT(mb2->updates.load() > settled, 5000);
    bridge.setLive(false);
}

void LifecycleTest::repeatedCreateDestroy()
{
    Env env;
    env.api.ctrls = { env.mb.get() };
    for(int i = 0; i < 8; i++)
    {
        auto* bridge = new studio::SceneBridge(&env.api);
        bridge->setLive(true);
        if(i % 2 == 0)
        {
            bridge->playPreset(QStringLiteral("aurora"));
        }
        QTest::qWait(50);                    /* let a few pushes run */
        delete bridge;                       /* must join, not hang */
    }
    QVERIFY(true);
}

QTEST_GUILESS_MAIN(LifecycleTest)
#include "main.moc"

/*---------------------------------------------------------*\
|| lianli_hw_tool.cpp                                        ||
||                                                           ||
||   On-hardware check for the Lian Li wireless runtime:     ||
||   detect the TX/RX pair, list bound fan groups, upload a  ||
||   static color until the device confirms it, and cycle    ||
||   colors with a restore step. Uses the same               ||
||   LianLiWirelessService + USB link as the driver.         ||
||                                                           ||
||   Requires the Lian Li dongles on a WinUSB-bound driver   ||
||   and L-Connect stopped. RGB only — never sends fan/PWM   ||
||   or binding commands.                                    ||
\*---------------------------------------------------------*/

#include "LianLiWirelessProtocol.h"
#include "LianLiWirelessCodec.h"
#include "LianLiWirelessRuntime.h"
#include "LianLiWirelessTransport.h"
#include "LianLiWirelessService.h"
#include "LianLiWirelessFanType.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

using namespace LianLiWireless;
using namespace std::chrono_literals;

static void PrintMac(const Mac& m)
{
    printf("%02x:%02x:%02x:%02x:%02x:%02x",
           m[0], m[1], m[2], m[3], m[4], m[5]);
}

static bool ParseMac(const char* s, Mac& out)
{
    unsigned b[6];
    if(sscanf(s, "%x:%x:%x:%x:%x:%x",
              &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6)
    {
        return false;
    }
    for(int i = 0; i < 6; i++)
    {
        if(b[i] > 0xFF) return false;
        out[i] = (uint8_t)b[i];
    }
    return IsValidUnicastMac(out);
}

static bool ParseColor(const char* s, uint8_t rgb[3])
{
    unsigned r, g, b;
    if(sscanf(s, "%02x%02x%02x", &r, &g, &b) != 3 || strlen(s) != 6)
    {
        return false;
    }
    rgb[0] = (uint8_t)r; rgb[1] = (uint8_t)g; rgb[2] = (uint8_t)b;
    return true;
}

static std::shared_ptr<const RgbUpload> SolidColor(const uint8_t rgb[3],
                                                   uint8_t led_count,
                                                   uint8_t variant = 0)
{
    std::vector<uint8_t> payload(led_count * 3);
    for(size_t i = 0; i < led_count; i++)
    {
        payload[i * 3 + 0] = rgb[0];
        payload[i * 3 + 1] = rgb[1];
        payload[i * 3 + 2] = rgb[2];
    }
    RgbTiming timing;                  /* static frame; 96 ticks like the    */
    timing.interval_ticks = 96;        /* validated smoke-test upload        */
    return std::make_shared<RgbUpload>(
        LianLiWirelessCodec::Compress(payload.data(), payload.size()),
        led_count, 1, timing, variant);
}

/*-------------------------------------------------------------*\
| Poll discovery for `window_ms` and merge sightings by MAC.    |
\*-------------------------------------------------------------*/
static std::vector<Sighting> ScanGroups(IWirelessLink& link, uint32_t window_ms)
{
    std::vector<Sighting> merged;
    auto deadline = std::chrono::steady_clock::now()
                  + std::chrono::milliseconds(window_ms);
    while(std::chrono::steady_clock::now() < deadline)
    {
        std::vector<uint8_t> resp;
        if(link.PollDiscovery(resp))
        {
            try
            {
                for(const Sighting& s :
                        ParseDiscoveryResponse(resp.data(), resp.size()))
                {
                    bool known = false;
                    for(Sighting& m : merged)
                    {
                        if(m.mac == s.mac) { m = s; known = true; }
                    }
                    if(!known) merged.push_back(s);
                }
            }
            catch(const std::invalid_argument&) {}
        }
        std::this_thread::sleep_for(200ms);
    }
    return merged;
}

static const char* StateName(WirelessState s)
{
    switch(s)
    {
        case WirelessState::Boot:          return "Boot";
        case WirelessState::Searching:     return "Searching";
        case WirelessState::Uploading:     return "Uploading";
        case WirelessState::Holding:       return "Holding";
        case WirelessState::Lost:          return "Lost";
        case WirelessState::ForeignMaster: return "ForeignMaster";
        case WirelessState::Failed:        return "Failed";
        case WirelessState::Cancelled:     return "Cancelled";
    }
    return "?";
}

static int CmdDetect()
{
    unsigned tx_count = 0, rx_count = 0;
    if(!CountWirelessDongles(tx_count, rx_count))
    {
        printf("USB enumeration failed\n");
        return 1;
    }
    printf("dongles: TX=%u RX=%u\n", tx_count, rx_count);
    if(tx_count != 1 || rx_count != 1)
    {
        printf("expected exactly one TX and one RX dongle; not opening\n");
        return 1;
    }

    LianLiWirelessUsbLink link;
    Mac master{};
    if(!link.ReadMasterMac(master))
    {
        printf("cannot read master MAC: %s\n", link.LastError());
        return 1;
    }
    printf("master MAC: "); PrintMac(master); printf("\n");

    printf("polling discovery for 3s...\n");

    /* raw probe: show whether the dongle replies at all */
    for(int i = 0; i < 5; i++)
    {
        std::vector<uint8_t> resp;
        if(!link.PollDiscovery(resp))
        {
            printf("  poll failed: %s\n", link.LastError());
        }
        else if(!resp.empty())
        {
            printf("  reply %zu bytes:", resp.size());
            for(size_t j = 0; j < resp.size() && j < 16; j++)
            {
                printf(" %02x", resp[j]);
            }
            printf("\n");
            break;
        }
        std::this_thread::sleep_for(200ms);
    }

    auto groups = ScanGroups(link, 3000);
    if(groups.empty())
    {
        printf("no fan groups sighted (last error: %s)\n", link.LastError());
        return 1;
    }

    int bound_supported = 0;
    for(const Sighting& s : groups)
    {
        bool bound = (s.master == master);
        printf("group "); PrintMac(s.mac);
        printf("  ch=%u rx=%u fans=%u%s bound=%s",
               s.channel, s.receiver, s.fan_count,
               s.right_attach ? " right-attach" : "",
               bound ? "yes" : "NO (foreign master)");
        printf("  effect=%02x%02x%02x%02x  ticks=%u  rpm=[%u %u %u %u]\n",
               s.effect_id[0], s.effect_id[1], s.effect_id[2], s.effect_id[3],
               s.device_ticks, s.rpm[0], s.rpm[1], s.rpm[2], s.rpm[3]);
        printf("    types=[%u %u %u]", s.fan_types[0], s.fan_types[1], s.fan_types[2]);
        WirelessFanInfo info = ClassifyFanType(s.fan_types[0]);
        printf("  -> %s (%u leds/fan)%s\n", info.name, info.leds_per_fan,
               info.supported ? "" : "  [unsupported]");
        if(s.mb_rgb_sync) printf("    note: motherboard RGB sync reported\n");
        if(bound && info.supported) bound_supported++;
    }
    printf("%d bound supported group(s)\n", bound_supported);
    return bound_supported > 0 ? 0 : 1;
}

/*-------------------------------------------------------------*\
| Shared: pick a target group, start the service, upload, wait. |
\*-------------------------------------------------------------*/
static bool PickTarget(LianLiWirelessUsbLink& link, const Mac& master,
                       const char* mac_arg, Sighting& out)
{
    auto groups = ScanGroups(link, 4000);
    for(const Sighting& s : groups)
    {
        if(s.master != master) continue;
        if(mac_arg != nullptr)
        {
            Mac want;
            if(!ParseMac(mac_arg, want)) { printf("bad --mac\n"); return false; }
            if(!(s.mac == want)) continue;
        }
        WirelessFanInfo info = ClassifyFanType(s.fan_types[0]);
        if(!info.supported)
        {
            printf("group "); PrintMac(s.mac);
            printf(" is %s — not supported yet, skipping\n", info.name);
            continue;
        }
        if(s.mb_rgb_sync)
        {
            printf("motherboard RGB sync enabled on target; refusing writes\n");
            return false;
        }
        out = s;
        return true;
    }
    printf("no bound supported fan group found\n");
    return false;
}

static bool UploadAndConfirm(LianLiWirelessService& svc, const Mac& mac,
                             std::shared_ptr<const RgbUpload> upload,
                             uint32_t timeout_ms)
{
    svc.SetDesired(mac, upload);
    auto deadline = std::chrono::steady_clock::now()
                  + std::chrono::milliseconds(timeout_ms);
    LianLiWirelessService::GroupStatus st;
    while(std::chrono::steady_clock::now() < deadline)
    {
        if(svc.GetStatus(mac, st))
        {
            if(st.state == WirelessState::Failed
            || st.state == WirelessState::ForeignMaster)
            {
                printf("  state=%s error=%s\n", StateName(st.state),
                       st.last_error.c_str());
                return false;
            }
            if(st.confirmed)
            {
                printf("  confirmed (resends=%zu)\n", st.resends);
                return true;
            }
        }
        std::this_thread::sleep_for(200ms);
    }
    svc.GetStatus(mac, st);
    printf("  timeout: state=%s resends=%zu sightings=%s\n",
           StateName(st.state), st.resends,
           st.have_sighting ? "yes" : "no");
    return false;
}

static int CmdSet(int argc, char** argv)
{
    uint8_t rgb[3];
    if(argc < 1 || !ParseColor(argv[0], rgb))
    {
        printf("usage: lianli_hw_tool set RRGGBB [--mac aa:bb:cc:dd:ee:ff] [--variant N]\n");
        return 2;
    }
    const char* mac_arg = nullptr;
    uint8_t     variant = 0;
    for(int i = 1; i + 1 < argc; i++)
    {
        if(!strcmp(argv[i], "--mac"))     mac_arg = argv[i + 1];
        if(!strcmp(argv[i], "--variant")) variant = (uint8_t)atoi(argv[i + 1]);
    }

    LianLiWirelessUsbLink link;
    Mac master{};
    if(!link.ReadMasterMac(master))
    {
        printf("cannot read master MAC: %s\n", link.LastError());
        return 1;
    }
    Sighting target;
    if(!PickTarget(link, master, mac_arg, target)) return 1;

    WirelessFanInfo info = ClassifyFanType(target.fan_types[0]);
    uint8_t led_count = (uint8_t)(target.fan_count * info.leds_per_fan);
    printf("target "); PrintMac(target.mac);
    printf("  %s  fans=%u leds=%u\n", info.name, target.fan_count, led_count);

    SystemWirelessClock clock;
    LianLiWirelessService svc(link, clock);
    svc.AddGroup(target.mac, target.fan_count);
    svc.Start();

    int rc = UploadAndConfirm(svc, target.mac,
                              SolidColor(rgb, led_count, variant), 30000)
           ? 0 : 1;
    svc.Stop();
    return rc;
}

static int CmdCycle(int argc, char** argv)
{
    /* dim R/G/B like the smoke test, then a restore color */
    const uint8_t colors[3][3] = { {48,0,0}, {0,48,0}, {0,0,48} };
    const char*   names[3]     = { "red", "green", "blue" };
    uint8_t       restore[3]   = { 24, 24, 24 };
    const char*   mac_arg      = nullptr;
    unsigned      rounds       = 1;

    for(int i = 0; i + 1 < argc; i++)
    {
        if(!strcmp(argv[i], "--mac"))     mac_arg = argv[i + 1];
        if(!strcmp(argv[i], "--rounds"))  rounds  = (unsigned)atoi(argv[i + 1]);
        if(!strcmp(argv[i], "--restore") && !ParseColor(argv[i + 1], restore))
        {
            printf("bad --restore color\n");
            return 2;
        }
    }

    LianLiWirelessUsbLink link;
    Mac master{};
    if(!link.ReadMasterMac(master))
    {
        printf("cannot read master MAC: %s\n", link.LastError());
        return 1;
    }
    Sighting target;
    if(!PickTarget(link, master, mac_arg, target)) return 1;

    WirelessFanInfo info = ClassifyFanType(target.fan_types[0]);
    uint8_t led_count = (uint8_t)(target.fan_count * info.leds_per_fan);
    printf("target "); PrintMac(target.mac);
    printf("  %s  fans=%u leds=%u\n", info.name, target.fan_count, led_count);

    SystemWirelessClock clock;
    LianLiWirelessService svc(link, clock);
    svc.AddGroup(target.mac, target.fan_count);
    svc.Start();

    int failures = 0;
    for(unsigned r = 0; r < rounds; r++)
    {
        for(int c = 0; c < 3; c++)
        {
            printf("round %u %s\n", r + 1, names[c]);
            if(!UploadAndConfirm(svc, target.mac,
                                 SolidColor(colors[c], led_count), 20000))
            {
                failures++;
            }
        }
    }

    printf("restore %02x%02x%02x\n", restore[0], restore[1], restore[2]);
    /* variant salt: a repeated identical payload needs a fresh effect ID */
    if(!UploadAndConfirm(svc, target.mac,
                         SolidColor(restore, led_count, 1), 20000))
    {
        failures++;
    }

    svc.Stop();
    printf("%s (%d failed step(s))\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}

int main(int argc, char** argv)
{
    if(argc < 2)
    {
        printf("usage:\n"
               "  lianli_hw_tool detect\n"
               "  lianli_hw_tool set RRGGBB [--mac ..] [--variant N]\n"
               "  lianli_hw_tool cycle [--rounds N] [--restore RRGGBB] [--mac ..]\n"
               "\nStop L-Connect before running. Lighting stays at the last\n"
               "uploaded color; 'cycle' ends with a restore step.\n");
        return 2;
    }
    if(!strcmp(argv[1], "detect")) return CmdDetect();
    if(!strcmp(argv[1], "set"))    return CmdSet(argc - 2, argv + 2);
    if(!strcmp(argv[1], "cycle"))  return CmdCycle(argc - 2, argv + 2);
    printf("unknown command '%s'\n", argv[1]);
    return 2;
}

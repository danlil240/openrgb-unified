/*---------------------------------------------------------*\
|| scene_fixture_gen.cpp                                     |
||                                                           |
||   Writes the two spec workload fixtures (design §8):     |
||     tests/fixtures/scenes/normal/studio.json             |
||       30 device instances / ~1,000 emitters              |
||     tests/fixtures/scenes/stress/studio.json             |
||       100 device instances / ~5,000 emitters (see note)  |
||                                                           |
||   Both are compact v3 documents built only from the      |
||   packaged device types (presets/devices/*.device.json) |
||   — the same compact doc a workspace saves. Placement    |
||   is a fixed deterministic grid: same build, same bytes. |
||                                                           |
||   NOTE on the stress emitter count: the packaged type    |
||   library tops out at 40 emitters/instance (fan-slw),    |
||   so 100 instances cap at ~4,000 emitters. The fixture   |
||   uses the densest realistic mix and lands at ~3.8k —   |
||   the shortfall vs the spec's ~5k is recorded in the    |
||   task 6.2 report rather than invented with a fixture-  |
||   only type.                                             |
||                                                           |
||   The files double as the perf-harness input           |
||   (tests/studio_perf.cpp) and as load/validation        |
||   fixtures (studio_config_test). Re-run this generator   |
||   only when the recipe intentionally changes — the      |
||   generated JSON is committed.                           |
||                                                           |
||   Usage: scene_fixture_gen.exe [out_root] [preset_dir]   |
||     out_root   default: fixtures/scenes (cwd = tests/)   |
||     preset_dir default: ../plugins/DesktopLightingStudio |
||                         /presets/devices                 |
||                                                           |
||   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "scene/SceneTypes.h"
#include "scene/SceneResolver.h"
#include "config/StudioConfig.h"
#include "presets/PresetRegistry.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace studio;
using nlohmann::json;

static int failures = 0;

#define CHECK(cond, name)                                              \
    do {                                                               \
        if(!(cond)) { ++failures; std::printf("FAIL: %s\n", name); }   \
    } while(0)

struct Recipe
{
    const char* type;
    int         count;
};

/* Instance recipe per fixture. Ids are <type>-NN — the charset
   IsPresetId allows — so the generated map keys are stable and
   deterministic across runs/platforms (std::map ordering). */
static const Recipe NORMAL[] = {
    { "fan-slw",       23 },   /* 23 x 40 = 920 emitters   */
    { "ram-stick",      2 },   /*  2 x 10 =  20            */
    { "fan-120",        2 },   /*  2 x  8 =  16            */
    { "pump-360",       1 },   /*  8                       */
    { "mouse-3zone",    1 },   /* 13                       */
    { "keyboard-104",   1 },   /* dynamic matrix -> 0      */
};                             /* 30 instances, ~977 emitters */

static const Recipe STRESS[] = {
    { "fan-slw",       94 },   /* 94 x 40 = 3760 emitters  */
    { "ram-stick",      2 },   /* 20                       */
    { "fan-120",        1 },   /*  8                       */
    { "pump-360",       1 },   /*  8                       */
    { "mouse-3zone",    1 },   /* 13                       */
    { "keyboard-104",   1 },   /* dynamic matrix -> 0      */
};                             /* 100 instances, ~3809 emitters */

/* Deterministic layout: a 10-column grid on the desk plane, a
   fixed per-instance yaw, and every third row lifted as a
   "case wall" row. Every constant is an exact binary fraction
   so the serialized decimals are identical for any conforming
   shortest-round-trip writer (nlohmann dump, python repr) —
   same recipe, same bytes, no matter what regenerates it. */
static DeviceInstance Placement(int index)
{
    DeviceInstance d;
    const int col = index % 10;
    const int row = index / 10;
    d.position.x = -0.6875f + 0.15625f * (float)col;
    d.position.z = -0.375f + 0.140625f * (float)row;
    d.position.y = (row % 3 == 2) ? 0.25f : 0.0f;
    d.rotation_deg.y = (float)((index * 37) % 360) - 180.0f;
    return d;
}

static StudioDocument BuildFixture(const char* name,
                                   const Recipe* recipe, size_t n)
{
    StudioDocument w;
    w.meta.name            = name;
    /* Exact-binary-fraction values only — see Placement. */
    w.meta.camera.view         = "desk";
    w.meta.camera.target       = { 0.125f, 0.1875f, 0.0625f };
    w.meta.camera.pitch_deg    = -38.0f;
    w.meta.camera.distance     = 1.25f;
    w.meta.camera.span         = 0.875f;
    w.meta.controls.move_snap_m = 0.0125f;
    w.meta.render.quality      = "balanced";
    w.brightness               = 0.75f;
    w.effect.preset            = "aurora";
    w.effect.seed              = 1337;
    w.effect.playing           = true;
    /* live_on_startup stays false — a fixture must never arm
       output. */
    int index = 0;
    for(size_t r = 0; r < n; r++)
    {
        for(int i = 0; i < recipe[r].count; i++, index++)
        {
            DeviceInstance d = Placement(index);
            d.type = recipe[r].type;
            char id[64];
            std::snprintf(id, sizeof(id), "%s-%02d",
                          recipe[r].type, i + 1);
            w.devices[id] = d;
        }
    }
    return w;
}

static size_t CountEmitters(const SceneDocument& doc)
{
    size_t n = 0;
    for(const SceneObject& o : doc.objects)
    {
        n += o.emitters.size();
    }
    return n;
}

int main(int argc, char** argv)
{
    namespace fs = std::filesystem;
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    const fs::path out_root   = argc > 1 ? argv[1] : "fixtures/scenes";
    const fs::path preset_dir = argc > 2
        ? argv[2]
        : fs::path("..") / "plugins" / "DesktopLightingStudio"
              / "presets" / "devices";

    PresetRegistry reg;
    std::vector<std::string> errs;
    CHECK(reg.LoadDirectory(preset_dir.string(), &errs),
          "packaged type library loads");
    if(!errs.empty())
    {
        std::printf("  (registry: %s)\n", errs.front().c_str());
    }

    struct { const char* id; const Recipe* recipe; size_t n; }
    fixtures[] = {
        { "normal", NORMAL, sizeof(NORMAL) / sizeof(NORMAL[0]) },
        { "stress", STRESS, sizeof(STRESS) / sizeof(STRESS[0]) },
    };

    for(const auto& fx : fixtures)
    {
        StudioDocument w = BuildFixture(fx.id, fx.recipe, fx.n);

        /* The committed fixture must be a doc that survives the
           real pipeline: validate -> resolve. */
        StudioDocument check;
        std::vector<std::string> verrs;
        CHECK(FromJson(ToJson(w), check, &verrs),
              "fixture validates as v3");
        SceneDocument resolved;
        std::vector<std::string> rerrs;
        CHECK(ResolveScene(w, reg, resolved, &rerrs),
              "fixture resolves on packaged types");
        if(!rerrs.empty())
        {
            std::printf("  (%s resolve: %s)\n",
                        fx.id, rerrs.front().c_str());
        }

        const fs::path dir = out_root / fx.id;
        std::error_code ec;
        fs::create_directories(dir, ec);
        const fs::path path = dir / "studio.json";
        {
            std::ofstream f(path, std::ios::binary | std::ios::trunc);
            f << ToJson(w).dump(2) << "\n";
        }
        CHECK(fs::exists(path), "fixture file written");

        std::printf("{\"fixture\":\"%s\",\"devices\":%zu,"
                    "\"objects\":%zu,\"emitters\":%zu,"
                    "\"file\":\"%s\"}\n",
                    fx.id, w.devices.size(), resolved.objects.size(),
                    CountEmitters(resolved),
                    path.generic_string().c_str());
    }

    std::printf("%s\n", failures ? "FIXTURE GEN FAILED" : "OK");
    return failures ? 1 : 0;
}

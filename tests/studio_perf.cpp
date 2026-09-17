/*---------------------------------------------------------*\
|| studio_perf.cpp — Desktop Lighting Studio perf harness   ||
||                                                           |
||   The reproducible release measurement (design §8,       |
||   task 6.2). For each scene fixture under                |
||   tests/fixtures/scenes/<name>/studio.json:              |
||                                                           |
||   (a) frame-eval cost — load + resolve the compact doc   |
||       against the packaged device-type library, resolve  |
||       the persisted look through the effect registry     |
||       (the shipping path — same JSON the plugin loads    |
||       from the qrc), then EffectEngine::Evaluate at a    |
||       fixed dt over a fixed sample count. Per-eval wall  |
||       time -> p50/p95/max.                               |
||                                                           |
||   (b) transform-commit cost — the drag-latency proxy:    |
||       EditorController gesture commit (Begin + Preview   |
||       + Commit, one undo record — the same ops the       |
||       bridge drives) followed by the full workspace      |
||       re-resolve the bridge performs per edit            |
||       (SceneBridge::applyEdit: workspace -> resolve ->   |
||       doc swap). Reported as commit_ms and resolve_ms.   |
||                                                           |
||   Deterministic input, wall-clock output: fixed look,    |
||   fixed seed, fixed dt and fixed instance order — only   |
||   the measured times vary between runs. Machine-readable |
||   JSON lines on stdout; non-zero exit on any load/       |
||   resolve failure so the harness doubles as a fixture    |
||   validation gate.                                       |
||                                                           |
||   Usage:                                                 |
||     studio_perf.exe [fixture_root] [device_dir]          |
||                     [effect_dir] [frames] [commits]      |
||   Defaults (cwd = tests/):                               |
||     fixtures/scenes  ../plugins/DesktopLightingStudio/   |
||     presets/devices  ../plugins/DesktopLightingStudio/   |
||     presets/effects  600  60                             |
||                                                           |
||   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "scene/SceneTypes.h"
#include "scene/SceneResolver.h"
#include "config/StudioConfig.h"
#include "presets/PresetRegistry.h"
#include "presets/EffectRegistry.h"
#include "effects/EffectEngine.h"
#include "editor/EditorController.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace studio;
using Clock = std::chrono::steady_clock;

static int failures = 0;

#define CHECK(cond, name)                                              \
    do {                                                               \
        if(!(cond)) { ++failures; std::printf("FAIL: %s\n", name); }   \
    } while(0)

static double Ms(const Clock::time_point a, const Clock::time_point b)
{
    return std::chrono::duration<double, std::milli>(b - a).count();
}

struct Stats
{
    double p50, p95, max, mean;
};

static Stats Compute(std::vector<double> v /* by value: sorted in place */)
{
    std::sort(v.begin(), v.end());
    Stats s;
    s.p50  = v[v.size() / 2];
    s.p95  = v[(size_t)((v.size() - 1) * 0.95)];
    s.max  = v.back();
    double sum = 0.0;
    for(double d : v) { sum += d; }
    s.mean = sum / (double)v.size();
    return s;
}

static std::string Esc(const std::string& s)
{
    std::string out;
    for(char c : s)
    {
        if(c == '"' || c == '\\') { out += '\\'; }
        out += c;
    }
    return out;
}

int main(int argc, char** argv)
{
    namespace fs = std::filesystem;
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    const fs::path fixture_root = argc > 1 ? argv[1] : "fixtures/scenes";
    const fs::path device_dir   = argc > 2
        ? argv[2]
        : fs::path("..") / "plugins" / "DesktopLightingStudio"
              / "presets" / "devices";
    const fs::path effect_dir   = argc > 3
        ? argv[3]
        : fs::path("..") / "plugins" / "DesktopLightingStudio"
              / "presets" / "effects";
    const int frames  = argc > 4 ? std::atoi(argv[4]) : 600;
    const int commits = argc > 5 ? std::atoi(argv[5]) : 60;

    std::printf("{\"kind\":\"meta\",\"tool\":\"studio_perf\","
                "\"fixture_root\":\"%s\",\"device_dir\":\"%s\","
                "\"effect_dir\":\"%s\",\"frames\":%d,\"commits\":%d,"
                "\"timer\":\"steady_clock\",\"units\":\"ms\","
                "\"build\":\"cl /O2 release\"}\n",
                Esc(fixture_root.generic_string()).c_str(),
                Esc(device_dir.generic_string()).c_str(),
                Esc(effect_dir.generic_string()).c_str(),
                frames, commits);

    /* Packaged libraries — the same JSON the plugin loads from
       the qrc; the file layer is the shipping artifact. */
    PresetRegistry reg;
    std::vector<std::string> errs;
    CHECK(reg.LoadDirectory(device_dir.string(), &errs),
          "perf: device library loads");
    EffectRegistry fx;
    std::vector<std::string> ferrs;
    CHECK(fx.LoadDefaultsDirectory(effect_dir.string(), &ferrs),
          "perf: effect library loads");
    if(failures)
    {
        return 1;
    }

    /* One fixture per subdirectory holding a studio.json. */
    std::vector<fs::path> fixtures;
    std::error_code ec;
    for(const auto& e : fs::directory_iterator(fixture_root, ec))
    {
        if(e.is_directory() && fs::exists(e.path() / "studio.json"))
        {
            fixtures.push_back(e.path());
        }
    }
    std::sort(fixtures.begin(), fixtures.end());
    CHECK(!fixtures.empty(), "perf: fixtures found");

    for(const fs::path& dir : fixtures)
    {
        const std::string fid = dir.filename().string();
        const fs::path doc_path = dir / "studio.json";

        /* ---- load + validate + resolve (the file-load path) ---- */
        const Clock::time_point l0 = Clock::now();
        std::ifstream f(doc_path, std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        const std::string bytes = ss.str();
        nlohmann::json j;
        try
        {
            j = nlohmann::json::parse(bytes);
        }
        catch(const std::exception& e)
        {
            ++failures;
            std::printf("FAIL: %s: parse: %s\n", fid.c_str(), e.what());
            continue;
        }
        StudioDocument ws;
        std::vector<std::string> verrs;
        if(!FromJson(j, ws, &verrs))
        {
            ++failures;
            std::printf("FAIL: %s: validate: %s\n", fid.c_str(),
                        verrs.empty() ? "?" : verrs.front().c_str());
            continue;
        }
        SceneDocument scene;
        std::vector<std::string> rerrs;
        if(!ResolveScene(ws, reg, scene, &rerrs))
        {
            ++failures;
            std::printf("FAIL: %s: resolve: %s\n", fid.c_str(),
                        rerrs.empty() ? "?" : rerrs.front().c_str());
            continue;
        }
        const double load_ms = Ms(l0, Clock::now());

        size_t emitters = 0;
        for(const SceneObject& o : scene.objects)
        {
            emitters += o.emitters.size();
        }

        /* ---- (a) frame-eval ----
           The persisted look resolved through the registry at the
           persisted seed — identical input every run. */
        std::vector<EffectLayer> layers;
        std::vector<std::string> berrs;
        if(!fx.Build(ws.effect.preset, ws.effect.seed, layers, &berrs))
        {
            ++failures;
            std::printf("FAIL: %s: look '%s': %s\n", fid.c_str(),
                        ws.effect.preset.c_str(),
                        berrs.empty() ? "?" : berrs.front().c_str());
            continue;
        }
        EffectEngine engine;
        engine.SetLayers(layers);
        FrameColors frame;
        const double dt = 1.0 / 60.0;
        /* warmup — first-call effects (allocator growth, branch
           caches) are not steady-state frame cost */
        for(int i = 0; i < 30; i++)
        {
            engine.Evaluate(scene, i * dt, frame);
        }
        std::vector<double> eval_ms;
        eval_ms.reserve((size_t)frames);
        for(int i = 0; i < frames; i++)
        {
            const Clock::time_point t0 = Clock::now();
            engine.Evaluate(scene, i * dt, frame);
            eval_ms.push_back(Ms(t0, Clock::now()));
        }
        const Stats es = Compute(eval_ms);
        std::printf("{\"kind\":\"frame_eval\",\"fixture\":\"%s\","
                    "\"devices\":%zu,\"objects\":%zu,\"emitters\":%zu,"
                    "\"look\":\"%s\",\"seed\":%u,\"layers\":%zu,"
                    "\"samples\":%d,\"load_resolve_ms\":%.3f,"
                    "\"p50_ms\":%.4f,\"p95_ms\":%.4f,\"max_ms\":%.4f,"
                    "\"mean_ms\":%.4f,\"fps_at_p95\":%.1f}\n",
                    fid.c_str(), ws.devices.size(),
                    scene.objects.size(), emitters,
                    Esc(ws.effect.preset).c_str(), ws.effect.seed,
                    layers.size(), frames, load_ms,
                    es.p50, es.p95, es.max, es.mean,
                    es.p95 > 0.0 ? 1000.0 / es.p95 : 0.0);

        /* ---- (b) transform-commit (drag-latency proxy) ----
           One single-instance translate gesture per sample: the
           ops are the bridge's exact commit path — Commit folds
           the gesture into one edit record, then applyEdit
           re-resolves the whole workspace. */
        EditorController ed(ws);
        std::vector<std::string> ids;
        for(const auto& kv : ws.devices) { ids.push_back(kv.first); }
        std::vector<double> op_ms, resolve_ms, total_ms;
        SceneDocument scratch;
        for(int k = 0; k < commits && !ids.empty(); k++)
        {
            ed.SetSelection({ ids[(size_t)k % ids.size()] });
            const Clock::time_point c0 = Clock::now();
            ed.BeginTransform();
            ed.PreviewTranslate({ 0.005f * (float)(k % 7 + 1),
                                  0.0f, 0.0f });
            std::optional<EditorEdit> rec = ed.Commit();
            const Clock::time_point c1 = Clock::now();
            std::vector<std::string> cerrs;
            ResolveScene(ws, reg, scratch, &cerrs);
            const Clock::time_point c2 = Clock::now();
            if(!rec.has_value())
            {
                continue;      /* a locked/no-op commit isn't a sample */
            }
            op_ms.push_back(Ms(c0, c1));
            resolve_ms.push_back(Ms(c1, c2));
            total_ms.push_back(Ms(c0, c2));
        }
        const Stats os = Compute(op_ms);
        const Stats rs = Compute(resolve_ms);
        const Stats ts = Compute(total_ms);
        std::printf("{\"kind\":\"transform_commit\",\"fixture\":\"%s\","
                    "\"devices\":%zu,\"emitters\":%zu,\"samples\":%zu,"
                    "\"op_p50_ms\":%.4f,\"op_p95_ms\":%.4f,"
                    "\"resolve_p50_ms\":%.4f,\"resolve_p95_ms\":%.4f,"
                    "\"resolve_max_ms\":%.4f,"
                    "\"total_p50_ms\":%.4f,\"total_p95_ms\":%.4f,"
                    "\"total_max_ms\":%.4f}\n",
                    fid.c_str(), ws.devices.size(), emitters,
                    total_ms.size(),
                    os.p50, os.p95,
                    rs.p50, rs.p95, rs.max,
                    ts.p50, ts.p95, ts.max);
    }

    std::printf("%s\n", failures ? "PERF HARNESS FAILED" : "PERF OK");
    return failures ? 1 : 0;
}

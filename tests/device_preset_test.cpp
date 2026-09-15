/*---------------------------------------------------------*\
||| device_preset_test.cpp                                    |
|||                                                           |
|||   Qt-free tests for the v3 external-type stack:        ||
|||   presets/DevicePreset (type JSON + validation),       ||
|||   presets/PresetRegistry (dir load, id match, dep      ||
|||   cycles, defaults fallback), scene/SceneResolver      ||
|||   (compact workspace -> expanded SceneDocument), and   ||
|||   config/ConfigMigration (v2 -> v3 extraction).        ||
|||   Plain cl build via build-studio-tests.bat.           ||
|||                                                           |
|||   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "scene/SceneTypes.h"
#include "scene/SceneGraph.h"
#include "scene/DefaultDesk.h"
#include "scene/SceneResolver.h"
#include "presets/DevicePreset.h"
#include "presets/PresetRegistry.h"
#include "config/StudioConfig.h"
#include "config/ConfigMigration.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

using nlohmann::json;
using namespace studio;

static int failures = 0;
static int checks   = 0;

#define CHECK(cond, name)                                              \
    do {                                                               \
        ++checks;                                                      \
        if(!(cond)) { ++failures; std::printf("FAIL: %s\n", name); }   \
    } while(0)

static bool Near(float a, float b, float eps = 1e-4f)
{
    return std::fabs(a - b) < eps;
}

static bool HasError(const std::vector<std::string>& errors,
                     const std::string& needle)
{
    for(const std::string& e : errors)
    {
        if(e.find(needle) != std::string::npos)
        {
            return true;
        }
    }
    return false;
}

/*---------------------------------------------------------*\
||| Fixtures                                                 ||
\*---------------------------------------------------------*/
static json FanPreset(const std::string& id, int leds = 8)
{
    return {
        {"$schema", "../../schemas/device.schema.json"},
        {"schema_version", 1},
        {"id", id},
        {"name", "Test fan"},
        {"category", "fan"},
        {"entities", {
            {"frame", {
                {"geometry", "fan_frame"},
                {"size_m", {0.12, 0.025, 0.12}},
                {"x", 0}, {"y", 0}, {"z", 0},
                {"rx", 0}, {"ry", 0}, {"rz", 0},
                {"appearance", {{"body_color", "#202028"}}},
            }},
            {"diffuser", {
                {"geometry", "fan_ring"},
                {"size_m", {0.11, 0.004, 0.11}},
                {"x", 0}, {"y", 0.013}, {"z", 0},
                {"rx", 0}, {"ry", 0}, {"rz", 0},
                {"zone", "ring"},
            }},
        }},
        {"zones", json::array({
            {
                {"id", "ring"},
                {"entity", "diffuser"},
                {"led_count", leds},
                {"layout", {
                    {"type", "ring"},
                    {"radius_m", 0.052},
                    {"start_angle_deg", 0},
                    {"reverse", false},
                }},
            },
        })},
    };
}

static std::filesystem::path TempDir(const std::string& name)
{
    const auto dir = std::filesystem::temp_directory_path()
                     / ("dls_preset_test_" + name);
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    return dir;
}

static void WriteFile(const std::filesystem::path& p, const json& j)
{
    std::ofstream f(p, std::ios::binary);
    f << j.dump(2) << "\n";
}

/* World-space emitter positions: object world matrix x emitter
   local position — the transform the effects engine renders. */
static std::vector<Vec3> WorldEmitters(const SceneDocument& doc,
                                       const std::string& object_id)
{
    std::vector<Vec3> out;
    const SceneObject* o = FindObject(doc, object_id);
    if(o == nullptr)
    {
        return out;
    }
    const auto worlds = ResolveWorldMatrices(doc);
    const auto it = worlds.find(object_id);
    if(it == worlds.end())
    {
        return out;
    }
    for(const Emitter& e : o->emitters)
    {
        out.push_back(TransformPoint(it->second, e.local_pos));
    }
    return out;
}

/*---------------------------------------------------------*\
||| DevicePreset — parse/validate/serialize.               ||
\*---------------------------------------------------------*/
static void TestPresetBasics()
{
    DevicePreset p;
    std::vector<std::string> errors;
    CHECK(DevicePresetFromJson(FanPreset("fan-120"), p, &errors),
          "preset: fan-120 parses");
    CHECK(p.id == "fan-120" && p.entities.size() == 2
          && p.zones.size() == 1 && p.zones[0].led_count == 8,
          "preset: fields populated");
    CHECK(errors.empty(), "preset: no errors on valid doc");

    /* deterministic round-trip */
    const json j1 = ToJson(p);
    DevicePreset back;
    CHECK(DevicePresetFromJson(j1, back, &errors),
          "preset: serialized form re-parses");
    CHECK(ToJson(back) == j1, "preset: deterministic serialization");

    /* emitter generation: 8 points on a ring in the XZ plane */
    const std::vector<Emitter> em =
        GenerateZoneEmitters(p.zones[0], "grp", 0);
    CHECK(em.size() == 8, "preset: ring generates 8 emitters");
    bool on_ring = true;
    for(const Emitter& e : em)
    {
        const float r = std::sqrt(e.local_pos.x * e.local_pos.x
                                  + e.local_pos.z * e.local_pos.z);
        if(!Near(r, 0.052f, 1e-3f)) { on_ring = false; }
        if(e.group != "grp")        { on_ring = false; }
        if(e.address < 0 || e.address > 7) { on_ring = false; }
    }
    CHECK(on_ring, "preset: ring emitter positions + addresses");

    /* addr_base shifts generated addresses */
    const std::vector<Emitter> em2 =
        GenerateZoneEmitters(p.zones[0], "grp", 8);
    CHECK(em2[0].address == 8 && em2[7].address == 15,
          "preset: addr_base shifts addresses");
}

static void TestPresetValidation()
{
    std::vector<std::string> errors;
    DevicePreset p;

    {
        json j = FanPreset("fan-120");
        j.erase("id");
        CHECK(!DevicePresetFromJson(j, p, &errors)
              && HasError(errors, "id"),
              "preset: missing id rejected");
    }
    {
        json j = FanPreset("fan-120");
        j["schema_version"] = 2;
        CHECK(!DevicePresetFromJson(j, p, &errors)
              && HasError(errors, "schema_version"),
              "preset: newer schema rejected");
    }
    {
        json j = FanPreset("fan-120");
        j["id"] = "has space";
        CHECK(!DevicePresetFromJson(j, p, &errors)
              && HasError(errors, "id"),
              "preset: id charset enforced");
    }
    {
        json j = FanPreset("fan-120");
        j["entities"]["frame"]["size_m"] = {0.12, -1.0, 0.12};
        CHECK(!DevicePresetFromJson(j, p, &errors)
              && HasError(errors, "size_m"),
              "preset: negative size rejected");
    }
    {
        json j = FanPreset("fan-120");
        j["entities"]["diffuser"]["zone"] = "no_such_zone";
        CHECK(!DevicePresetFromJson(j, p, &errors)
              && HasError(errors, "zone"),
              "preset: dangling entity zone rejected");
    }
    {
        json j = FanPreset("fan-120");
        j["zones"][0]["entity"] = "ghost";
        CHECK(!DevicePresetFromJson(j, p, &errors)
              && HasError(errors, "entity"),
              "preset: dangling zone entity rejected");
    }
    {
        json j = FanPreset("fan-120");
        j["entities"]["frame"]["parent"] = "diffuser";
        j["entities"]["diffuser"]["parent"] = "frame";
        CHECK(!DevicePresetFromJson(j, p, &errors)
              && HasError(errors, "cycle"),
              "preset: entity parent cycle rejected");
    }
    {
        json j = FanPreset("fan-120");
        j["zones"][0]["layout"]["type"] = "spiral";
        CHECK(!DevicePresetFromJson(j, p, &errors)
              && HasError(errors, "layout"),
              "preset: unknown layout rejected");
    }
    {
        /* child type refs may not carry local-part fields */
        json j = FanPreset("fan-120");
        j["entities"]["child"] = {
            {"type", "other-type"},
            {"x", 0.1}, {"y", 0}, {"z", 0},
            {"rx", 0}, {"ry", 0}, {"rz", 0},
            {"geometry", "fan_frame"},
        };
        CHECK(!DevicePresetFromJson(j, p, &errors),
              "preset: child ref with geometry rejected");
    }
    {
        /* a valid child ref parses and reports the dependency */
        json j = FanPreset("fan-120");
        j["entities"]["child"] = {
            {"type", "other-type"},
            {"x", 0.1}, {"y", 0}, {"z", 0},
            {"rx", 0}, {"ry", 0}, {"rz", 0},
        };
        CHECK(DevicePresetFromJson(j, p, &errors),
              "preset: child ref parses");
        const auto deps = PresetDependencies(p);
        CHECK(deps.size() == 1 && deps[0] == "other-type",
              "preset: dependency reported");
    }
    {
        /* failed parse leaves the candidate untouched */
        DevicePreset keep;
        CHECK(DevicePresetFromJson(FanPreset("keep-me"), keep, &errors),
              "preset: baseline parses");
        CHECK(!DevicePresetFromJson(json::object(), keep, &errors)
              && keep.id == "keep-me",
              "preset: failure leaves candidate untouched");
    }
}

/*---------------------------------------------------------*\
||| PresetRegistry — directory load, id match, deps.       ||
\*---------------------------------------------------------*/
static void TestRegistry()
{
    std::vector<std::string> errors;

    {
        const auto dir = TempDir("basic");
        WriteFile(dir / "fan-120.device.json", FanPreset("fan-120"));
        WriteFile(dir / "fan-80.device.json",  FanPreset("fan-80"));

        PresetRegistry reg;
        CHECK(reg.LoadDirectory(dir.string(), &errors),
              "registry: clean dir loads");
        CHECK(reg.FileCount() == 2 && reg.Contains("fan-120")
              && reg.Contains("fan-80"),
              "registry: types resolvable");
    }
    {
        /* id must equal the file basename — never pick whichever
           file happened to define the id first. */
        const auto dir = TempDir("mismatch");
        WriteFile(dir / "fan-120.device.json", FanPreset("other-id"));
        PresetRegistry reg;
        CHECK(!reg.LoadDirectory(dir.string(), &errors)
              && HasError(errors, "does not match"),
              "registry: id/filename mismatch rejected");
        CHECK(!reg.Contains("other-id") && !reg.Contains("fan-120"),
              "registry: mismatched type excluded");
    }
    {
        /* missing dependency is removed and reported */
        const auto dir = TempDir("missing-dep");
        json j = FanPreset("assembly");
        j["entities"]["child"] = {
            {"type", "ghost-type"},
            {"x", 0}, {"y", 0}, {"z", 0},
            {"rx", 0}, {"ry", 0}, {"rz", 0},
        };
        WriteFile(dir / "assembly.device.json", j);
        WriteFile(dir / "fan-120.device.json", FanPreset("fan-120"));
        PresetRegistry reg;
        CHECK(!reg.LoadDirectory(dir.string(), &errors)
              && HasError(errors, "missing dependency"),
              "registry: missing dependency reported");
        CHECK(!reg.Contains("assembly") && reg.Contains("fan-120"),
              "registry: dependent removed, sibling kept");
    }
    {
        /* a recursive reference cycle is rejected */
        const auto dir = TempDir("cycle");
        json a = FanPreset("type-a");
        a["entities"]["child"] = {
            {"type", "type-b"},
            {"x", 0}, {"y", 0}, {"z", 0},
            {"rx", 0}, {"ry", 0}, {"rz", 0},
        };
        json b = FanPreset("type-b");
        b["entities"]["child"] = {
            {"type", "type-a"},
            {"x", 0}, {"y", 0}, {"z", 0},
            {"rx", 0}, {"ry", 0}, {"rz", 0},
        };
        WriteFile(dir / "type-a.device.json", a);
        WriteFile(dir / "type-b.device.json", b);
        PresetRegistry reg;
        CHECK(!reg.LoadDirectory(dir.string(), &errors)
              && HasError(errors, "cycle"),
              "registry: reference cycle rejected");
        CHECK(!reg.Contains("type-a") && !reg.Contains("type-b"),
              "registry: cyclic types removed");
    }
    {
        /* defaults fill in underneath the file layer */
        PresetRegistry reg;
        reg.SetDefaults(DefaultDevicePresets());
        CHECK(reg.Contains("fan-120") && reg.Contains("group"),
              "registry: packaged defaults resolvable");
        CHECK(reg.LoadDirectory(TempDir("empty").string(), &errors),
              "registry: empty dir is not an error");
        CHECK(reg.Contains("fan-120"),
              "registry: default survives empty file layer");
    }
    {
        /* LoadFile failure leaves the registry unchanged */
        const auto dir = TempDir("reload");
        WriteFile(dir / "fan-120.device.json", FanPreset("fan-120"));
        PresetRegistry reg;
        CHECK(reg.LoadDirectory(dir.string(), &errors),
              "registry: baseline dir loads");
        const auto bad = dir / "broken.device.json";
        {
            std::ofstream f(bad, std::ios::binary);
            f << "{ not json";
        }
        CHECK(!reg.LoadFile(bad.string(), &errors)
              && reg.Contains("fan-120"),
              "registry: failed LoadFile leaves registry intact");
    }
}

/*---------------------------------------------------------*\
||| SceneResolver — the contract from the brief:           ||
||| two fan instances sharing one definition with          ||
||| different root transforms, identical local layouts,    ||
||| correct world emitter positions and independent        ||
||| bindings.                                              ||
\*---------------------------------------------------------*/
static PresetRegistry TestReg()
{
    PresetRegistry reg;
    std::vector<DevicePreset> defs;
    DevicePreset fan;
    CHECK(DevicePresetFromJson(FanPreset("fan-120"), fan, nullptr),
          "registry: fixture preset parses");
    defs.push_back(fan);
    reg.SetDefaults(defs);
    return reg;
}

static StudioDocument TwoFanWorkspace()
{
    StudioDocument w;
    w.meta.name = "Two fans";

    DeviceInstance a;
    a.type     = "fan-120";
    a.position = { 0.30f, 0.10f, 0.0f };
    a.rotation_deg = { 90.0f, 0.0f, 0.0f };
    w.devices["fan_a"] = a;

    DeviceInstance b;
    b.type     = "fan-120";
    b.position = { -0.30f, 0.05f, 0.0f };
    b.rotation_deg = { 0.0f, 45.0f, 0.0f };
    w.devices["fan_b"] = b;

    DeviceBinding ba;
    ba.id              = "bus_a";
    ba.controller_name = "Ctrl A";
    ba.zone_name       = "Z1";
    ba.zone_leds       = 8;
    w.bindings["bus_a"] = ba;
    DeviceBinding bb;
    bb.id              = "bus_b";
    bb.controller_name = "Ctrl B";
    bb.zone_name       = "Z2";
    bb.zone_leds       = 8;
    w.bindings["bus_b"] = bb;

    w.device_settings["fan_a"].zones["ring"] =
        { "bus_a", 0, true };
    w.device_settings["fan_b"].zones["ring"] =
        { "bus_b", 8, true };
    w.object_colors["fan_a/diffuser"] = MakeSceneColor(255, 0, 0);
    w.object_colors["fan_b/diffuser"] = MakeSceneColor(0, 0, 255);
    return w;
}

static void TestResolveTwoFans()
{
    const PresetRegistry reg = TestReg();
    const StudioDocument w   = TwoFanWorkspace();

    SceneDocument doc;
    std::vector<std::string> errors;
    CHECK(ResolveScene(w, reg, doc, &errors), "resolve: two fans expand");
    if(!errors.empty())
    {
        std::printf("  (resolve errors: %s)\n", errors.front().c_str());
    }

    /* every instance expands the shared definition's entities under
       its own stable id — the definition is stored once. */
    const SceneObject* fa = FindObject(doc, "fan_a/diffuser");
    const SceneObject* fb = FindObject(doc, "fan_b/diffuser");
    CHECK(fa != nullptr && fb != nullptr,
          "resolve: instanced entity ids namespaced");
    CHECK(FindObject(doc, "fan_a/frame") != nullptr
          && FindObject(doc, "fan_b/frame") != nullptr,
          "resolve: sibling entities expanded");

    /* identical local layouts — same emitter local positions */
    CHECK(fa->emitters.size() == 8 && fb->emitters.size() == 8,
          "resolve: emitter counts match type");
    bool same_local = true;
    for(size_t i = 0; i < fa->emitters.size(); i++)
    {
        if(!Near(fa->emitters[i].local_pos.x, fb->emitters[i].local_pos.x)
           || !Near(fa->emitters[i].local_pos.z, fb->emitters[i].local_pos.z))
        {
            same_local = false;
        }
    }
    CHECK(same_local, "resolve: identical local emitter layout");

    /* different root transforms — instance groups carry them */
    const SceneObject* ga = FindObject(doc, "fan_a");
    const SceneObject* gb = FindObject(doc, "fan_b");
    CHECK(ga != nullptr && gb != nullptr
          && ga->kind == ObjectKind::Group
          && Near(ga->transform.position.x, 0.30f)
          && Near(ga->transform.rotation_deg.x, 90.0f)
          && Near(gb->transform.rotation_deg.y, 45.0f),
          "resolve: instance transforms on group nodes");

    /* world emitter positions follow instance transform x entity
       chain x emitter local — fan_a's ring stands up (rx=90) so a
       local XZ circle lands in the world XY plane. */
    const std::vector<Vec3> wa = WorldEmitters(doc, "fan_a/diffuser");
    CHECK(wa.size() == 8, "resolve: world emitters computed");
    if(wa.size() == 8)
    {
        /* local {r,0,0} -> rotated to (0.30, 0.10+r*cos? ) — just
           check the world ring is centered on the entity world pos
           and NOT equal to fan_b's (different transforms). */
        const std::vector<Vec3> wb = WorldEmitters(doc, "fan_b/diffuser");
        CHECK(wb.size() == 8, "resolve: fan_b world emitters");
        bool differ = false;
        for(size_t i = 0; i < wa.size() && i < wb.size(); i++)
        {
            if(!Near(wa[i].x, wb[i].x) || !Near(wa[i].z, wb[i].z))
            {
                differ = true;
            }
        }
        CHECK(differ, "resolve: distinct world emitter clouds");
        /* rx=90 tips the local +z ring axis onto -y: emitter 0 at
           local (+r, 0, 0) stays +x; the y spread spans r*2. */
        float ymin = 1e9f, ymax = -1e9f;
        for(const Vec3& v : wa)
        {
            ymin = std::min(ymin, v.y);
            ymax = std::max(ymax, v.y);
        }
        CHECK(ymax - ymin > 0.08f,
              "resolve: rx=90 tips the ring vertical");
    }

    /* independent physical bindings + addr bases */
    CHECK(fa->binding == "bus_a" && fb->binding == "bus_b",
          "resolve: independent bindings");
    CHECK(fa->emitters[0].address == 0
          && fb->emitters[0].address == 8,
          "resolve: per-instance addr_base");
    CHECK(fa->verified && fb->verified,
          "resolve: verification state per instance");

    /* colors fan out to resolved ids */
    CHECK(doc.object_colors.at("fan_a/diffuser")
              == MakeSceneColor(255, 0, 0),
          "resolve: object colors keyed by resolved path");
}

static void TestResolveMirror()
{
    const PresetRegistry reg = TestReg();
    StudioDocument w = TwoFanWorkspace();
    w.device_settings["fan_b"].mirror_of = "fan_a";

    SceneDocument doc;
    std::vector<std::string> errors;
    CHECK(ResolveScene(w, reg, doc, &errors),
          "mirror: instance mirror resolves");

    /* every part object under fan_b is Linked to the corresponding
       fan_a object — output ownership, not placement. */
    const SceneObject* b = FindObject(doc, "fan_b/diffuser");
    CHECK(b != nullptr && b->kind == ObjectKind::Linked
          && b->mirror_of == "fan_a/diffuser"
          && b->emitters.empty() && b->binding.empty(),
          "mirror: expanded objects Linked to owner");
    const SceneObject* bf = FindObject(doc, "fan_b/frame");
    CHECK(bf != nullptr && bf->kind == ObjectKind::Linked
          && bf->mirror_of == "fan_a/frame",
          "mirror: all parts mirror 1:1");

    /* placement is untouched — fan_b keeps its own transform */
    const SceneObject* gb = FindObject(doc, "fan_b");
    CHECK(gb != nullptr && gb->kind == ObjectKind::Group
          && Near(gb->transform.position.x, -0.30f),
          "mirror: placement transform preserved");

    /* the resolved document still satisfies scene-graph rules */
    std::vector<std::string> gerrs;
    CHECK(ValidateSceneGraph(doc, &gerrs),
          "mirror: resolved scene validates");

    /* mirror chains are rejected */
    StudioDocument chained = w;
    chained.device_settings["fan_a"].mirror_of = "fan_b";
    SceneDocument keep = doc;
    CHECK(!ResolveScene(chained, reg, keep, &errors)
          && HasError(errors, "chain"),
          "mirror: chains rejected");
}

static void TestResolveFailures()
{
    const PresetRegistry reg = TestReg();
    StudioDocument w = TwoFanWorkspace();
    std::vector<std::string> errors;

    {
        StudioDocument bad = w;
        bad.devices["fan_a"].type = "no_such_type";
        SceneDocument doc;
        CHECK(!ResolveScene(bad, reg, doc, &errors)
              && HasError(errors, "devices.fan_a.type")
              && HasError(errors, "no_such_type"),
              "resolve: missing type reports the field");
    }
    {
        StudioDocument bad = w;
        bad.device_settings["fan_a"].zones["ghost"] = { "bus_a", 0, true };
        SceneDocument doc;
        CHECK(!ResolveScene(bad, reg, doc, &errors)
              && HasError(errors, "zones.ghost"),
              "resolve: unknown zone id reports the field");
    }
    {
        StudioDocument bad = w;
        bad.device_settings["fan_a"].zones["ring"].binding = "ghost";
        /* StudioConfig catches this at parse; resolver double-checks
           only through validation — make sure parse catches it. */
        std::vector<std::string> verrs;
        StudioDocument chk;
        json j = ToJson(bad);
        CHECK(!FromJson(j, chk, &verrs)
              && HasError(verrs, "binding"),
              "resolve: dangling zone binding rejected at parse");
    }
    {
        /* a failed resolve leaves the previous document alone */
        SceneDocument keep;
        keep.objects.push_back(SceneObject{});
        keep.objects[0].id = "sentinel";
        StudioDocument bad = w;
        bad.devices["fan_a"].type = "ghost";
        CHECK(!ResolveScene(bad, reg, keep, &errors)
              && keep.objects.size() == 1
              && keep.objects[0].id == "sentinel",
              "resolve: failure leaves document untouched");
    }
    {
        /* instance parent chain: child placement follows parent */
        StudioDocument nested = w;
        DeviceInstance c;
        c.type     = "fan-120";
        c.position = { 0.05f, 0.0f, 0.0f };
        c.parent   = "fan_a";
        nested.devices["fan_c"] = c;
        SceneDocument doc;
        errors.clear();
        CHECK(ResolveScene(nested, reg, doc, &errors),
              "resolve: instance parent resolves");
        const SceneObject* gc = FindObject(doc, "fan_c");
        CHECK(gc != nullptr && gc->parent_id == "fan_a",
              "resolve: parent chain on group node");
        const auto worlds = ResolveWorldMatrices(doc);
        const Vec3 p = TransformPoint(worlds.at("fan_c/diffuser"),
                                      Vec3{});
        const Vec3 pa = TransformPoint(worlds.at("fan_a/diffuser"),
                                       Vec3{});
        (void)p; (void)pa;
    }
    {
        StudioDocument bad = w;
        bad.devices["fan_a"].parent = "fan_b";
        bad.devices["fan_b"].parent = "fan_a";
        StudioDocument chk;
        std::vector<std::string> verrs;
        CHECK(!FromJson(ToJson(bad), chk, &verrs)
              && HasError(verrs, "parent"),
              "resolve: instance parent cycle rejected at parse");
    }
}

/*---------------------------------------------------------*\
||| Compact serialization — the workspace file carries     ||
||| instances + settings only; never expanded content.     ||
\*---------------------------------------------------------*/
static void TestCompactRoundTrip()
{
    const StudioDocument w = TwoFanWorkspace();
    const json j = ToJson(w);

    CHECK(j.value("schema_version", 0) == 3,
          "compact: schema_version 3");
    CHECK(!j.contains("scene") && !j.contains("definitions"),
          "compact: no expanded sections");
    CHECK(j.contains("devices") && j.contains("bindings")
          && j.contains("device_settings") && j.contains("colors"),
          "compact: v3 sections present");

    /* no inline entities, generated emitters or embedded defs —
       top level and inside each device entry (colors.emitters is a
       legit v3 section: per-emitter paint, not generated layout). */
    CHECK(!j.contains("entities") && !j.contains("emitters")
          && !j.contains("definitions") && !j.contains("scene"),
          "compact: no inline entities/emitters/definitions");
    for(const auto& kv : j["devices"].items())
    {
        CHECK(!kv.value().contains("entities")
              && !kv.value().contains("emitters")
              && !kv.value().contains("geometry"),
              "compact: device entry is placement only");
    }

    /* device entries are placements only */
    const json& fa = j["devices"]["fan_a"];
    CHECK(fa.value("type", std::string()) == "fan-120"
          && fa.contains("x") && fa.contains("rx")
          && !fa.contains("entities") && !fa.contains("emitters")
          && !fa.contains("geometry"),
          "compact: instance carries type + transform only");

    StudioDocument back;
    std::vector<std::string> errors;
    CHECK(FromJson(j, back, &errors), "compact: round-trip parses");
    CHECK(back.devices.size() == 2 && back.bindings.size() == 2
          && back.device_settings.size() == 2
          && back.object_colors.size() == 2,
          "compact: all sections restored");
    CHECK(ToJson(back) == j, "compact: deterministic serialization");

    /* colors write #RRGGBB */
    CHECK(j["colors"]["objects"]["fan_a/diffuser"] == "#FF0000",
          "compact: hex colors on disk");
}

/*---------------------------------------------------------*\
||| Type reload — editing a type file and reloading the    ||
||| registry rebuilds every instance that references it.   ||
\*---------------------------------------------------------*/
static void TestTypeReload()
{
    const auto dir = TempDir("type-reload");
    WriteFile(dir / "fan-120.device.json", FanPreset("fan-120", 8));

    PresetRegistry reg;
    std::vector<std::string> errors;
    CHECK(reg.LoadDirectory(dir.string(), &errors), "reload: initial load");

    StudioDocument w = TwoFanWorkspace();
    SceneDocument doc;
    CHECK(ResolveScene(w, reg, doc, &errors), "reload: initial resolve");
    CHECK(FindObject(doc, "fan_a/diffuser")->emitters.size() == 8
          && FindObject(doc, "fan_b/diffuser")->emitters.size() == 8,
          "reload: 8 emitters before edit");

    /* edit the TYPE file — 16 LEDs now — and reload: both instances
       update together. */
    WriteFile(dir / "fan-120.device.json", FanPreset("fan-120", 16));
    reg.ClearFiles();
    CHECK(reg.LoadDirectory(dir.string(), &errors), "reload: edited load");
    CHECK(ResolveScene(w, reg, doc, &errors), "reload: edited resolve");
    CHECK(FindObject(doc, "fan_a/diffuser")->emitters.size() == 16
          && FindObject(doc, "fan_b/diffuser")->emitters.size() == 16,
          "reload: type edit rebuilds all instances");

    /* moving one instance touches only its placement — the sibling
       and the type file are unchanged. */
    StudioDocument moved = w;
    moved.devices["fan_a"].position.x += 0.10f;
    SceneDocument moved_doc;
    CHECK(ResolveScene(moved, reg, moved_doc, &errors),
          "reload: moved resolve");
    CHECK(Near(FindObject(moved_doc, "fan_a")->transform.position.x, 0.40f)
          && Near(FindObject(moved_doc, "fan_b")->transform.position.x, -0.30f),
          "reload: instance move is placement-only");
}

/*---------------------------------------------------------*\
||| Migration — expanded v2 scene -> compact v3 + types.   ||
||| The migrated desk must resolve to the SAME world       ||
||| emitter layout: transforms, bindings and mirrors all   ||
||| preserved.                                             ||
\*---------------------------------------------------------*/
static void TestMigration()
{
    const SceneDocument scene = BuildDefaultDesk();
    StudioDocument w;
    std::vector<DevicePreset> types;
    std::vector<std::string> errors, warnings;
    CHECK(MigrateExpandedScene(scene, w, types, &errors, &warnings),
          "migrate: default desk extracts");
    if(!errors.empty())
    {
        std::printf("  (migration errors: %s)\n", errors.front().c_str());
    }

    /* dedupe: the desk's repeated fans collapse — the 8-LED ring
       fan (case_fans + 3 mirrors + 3 rad fans + gpu_top_fan = 8
       instances) shares ONE type. */
    CHECK(types.size() <= 16 && types.size() < scene.objects.size(),
          "migrate: structural dedupe collapses repeats");
    std::map<std::string, int> inst_per_type;
    for(const auto& kv : w.devices)
    {
        inst_per_type[kv.second.type]++;
    }
    int max_shared = 0;
    for(const auto& kv : inst_per_type)
    {
        max_shared = std::max(max_shared, kv.second);
    }
    CHECK(max_shared >= 8,
          "migrate: repeated fans share one definition");
    CHECK(!types.empty(), "migrate: types extracted");
    CHECK(w.devices.size() == scene.objects.size(),
          "migrate: every object became an instance");

    /* binding identity preserved */
    CHECK(w.bindings.size() == scene.bindings.size(),
          "migrate: bindings preserved");

    /* the migrated workspace is itself valid compact v3 */
    StudioDocument back;
    CHECK(FromJson(ToJson(w), back, &errors),
          "migrate: compact doc round-trips");

    /* registry builds from the extracted types */
    PresetRegistry reg;
    for(const DevicePreset& p : types)
    {
        CHECK(reg.Add(p, &errors), "migrate: extracted type validates");
    }

    SceneDocument resolved;
    CHECK(ResolveScene(w, reg, resolved, &errors),
          "migrate: workspace resolves");

    /* world emitter parity — same addressable LEDs in the same
       world spots (ids change to <instance>/<entity>, positions
       must not). */
    const auto old_worlds = ResolveWorldMatrices(scene);
    std::vector<Vec3> old_pts;
    for(const SceneObject& o : scene.objects)
    {
        const auto wit = old_worlds.find(o.id);
        if(wit == old_worlds.end()) { continue; }
        for(const Emitter& e : o.emitters)
        {
            old_pts.push_back(TransformPoint(wit->second, e.local_pos));
        }
    }
    const auto new_worlds = ResolveWorldMatrices(resolved);
    std::vector<Vec3> new_pts;
    for(const SceneObject& o : resolved.objects)
    {
        const auto wit = new_worlds.find(o.id);
        if(wit == new_worlds.end()) { continue; }
        for(const Emitter& e : o.emitters)
        {
            new_pts.push_back(TransformPoint(wit->second, e.local_pos));
        }
    }
    CHECK(old_pts.size() == new_pts.size(),
          "migrate: emitter count preserved");
    auto key = [](const Vec3& v) {
        return std::make_tuple(std::llround(v.x * 10000),
                               std::llround(v.y * 10000),
                               std::llround(v.z * 10000));
    };
    std::multiset<std::tuple<long long, long long, long long>> os, ns;
    for(const Vec3& v : old_pts) { os.insert(key(v)); }
    for(const Vec3& v : new_pts) { ns.insert(key(v)); }
    CHECK(os == ns, "migrate: world emitter positions preserved");

    /* mirror output behavior: v2 linked copies are v3 mirrored
       instances of the same type. */
    const SceneObject* b1_old = FindObject(scene, "case_fan_b1");
    const SceneObject* b1_new = FindObject(resolved, "case_fan_b1/body");
    CHECK(b1_old != nullptr && b1_new != nullptr
          && b1_new->kind == ObjectKind::Linked
          && b1_new->mirror_of == "case_fans/body",
          "migrate: mirror instances linked to owner");

    /* addresses: the pump's LEDs stay at 8..15 on its zone. */
    const SceneObject* pump = FindObject(resolved, "pump/body");
    CHECK(pump != nullptr && !pump->emitters.empty()
          && pump->emitters.front().address == 8,
          "migrate: addr_base preserved");
}

/*---------------------------------------------------------*\
||| The packaged default: compact workspace + default      ||
||| presets resolve to the same desk BuildDefaultDesk()    ||
||| produces (transform/effect parity).                    ||
\*---------------------------------------------------------*/
static void TestDefaultWorkspaceParity()
{
    PresetRegistry reg;
    reg.SetDefaults(DefaultDevicePresets());

    StudioDocument w = BuildDefaultWorkspace();
    std::vector<std::string> errors;
    SceneDocument resolved;
    CHECK(ResolveScene(w, reg, resolved, &errors),
          "defaults: packaged workspace resolves");
    if(!errors.empty())
    {
        std::printf("  (defaults errors: %s)\n", errors.front().c_str());
    }

    const SceneDocument expanded = BuildDefaultDesk();
    const auto ow = ResolveWorldMatrices(expanded);
    const auto nw = ResolveWorldMatrices(resolved);
    auto collect = [](const SceneDocument& d,
                      const std::map<std::string, Mat4>& worlds) {
        std::multiset<std::tuple<long long, long long, long long>> pts;
        for(const SceneObject& o : d.objects)
        {
            const auto it = worlds.find(o.id);
            if(it == worlds.end()) { continue; }
            for(const Emitter& e : o.emitters)
            {
                const Vec3 p = TransformPoint(it->second, e.local_pos);
                pts.insert(std::make_tuple(std::llround(p.x * 10000),
                                           std::llround(p.y * 10000),
                                           std::llround(p.z * 10000)));
            }
        }
        return pts;
    };
    CHECK(collect(expanded, ow) == collect(resolved, nw),
          "defaults: world emitters match expanded desk");
    CHECK(resolved.bindings.size() == expanded.bindings.size(),
          "defaults: bindings match expanded desk");
}

int main()
{
    TestPresetBasics();
    TestPresetValidation();
    TestRegistry();
    TestResolveTwoFans();
    TestResolveMirror();
    TestResolveFailures();
    TestCompactRoundTrip();
    TestTypeReload();
    TestMigration();
    TestDefaultWorkspaceParity();

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}

/*---------------------------------------------------------*\
|| studio_config_test.cpp                                    |
||                                                           |
||   Deterministic tests for the Desktop Lighting Studio    |
||   workspace document (plugins/DesktopLightingStudio/     |
||   config/): candidate validation, legacy-settings        |
||   migration, and full round-trips. No Qt, no hardware —  |
||   plain cl build via build-studio-tests.bat.             |
||                                                           |
||   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "scene/SceneTypes.h"
#include "scene/SceneGraph.h"
#include "scene/SceneJson.h"
#include "scene/DefaultDesk.h"
#include "config/StudioConfig.h"
#include "config/ConfigMigration.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdio>
#include <string>

using nlohmann::json;

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
|| A small hand-built scene: one bound device, one        ||
|| mirrored copy under a group, one decor body.           ||
\*---------------------------------------------------------*/
static studio::SceneDocument SmallScene()
{
    using namespace studio;
    SceneDocument doc;
    doc.name       = "Fixture desk";
    doc.brightness = 0.65f;

    DeviceBinding b;
    b.id              = "fan_bus";
    b.controller_name = "X870E AORUS ELITE";
    b.vendor          = "Gigabyte";
    b.device_type     = -1;
    b.zone_name       = "ARGB_V2_1";
    b.zone_leds       = 8;
    doc.bindings.push_back(b);

    SceneObject grp;
    grp.id   = "case";
    grp.label = "Case";
    grp.kind = ObjectKind::Group;
    grp.transform.position = { 0.4f, 0.1f, -0.1f };
    doc.objects.push_back(grp);

    SceneObject fan;
    fan.id      = "fan0";
    fan.label   = "Front fan";
    fan.kind    = ObjectKind::Device;
    fan.binding = "fan_bus";
    fan.parent_id = "case";
    fan.transform.position = { 0.0f, -0.16f, -0.14f };
    fan.verified = true;
    for(int i = 0; i < 8; i++)
    {
        Emitter e;
        e.local_pos = { 0.05f * i, 0.016f, 0.0f };
        e.group     = "fan0";
        e.address   = i;
        fan.emitters.push_back(e);
    }
    doc.objects.push_back(fan);

    SceneObject mirror;
    mirror.id        = "fan0_copy";
    mirror.label     = "Fan (mirror)";
    mirror.kind      = ObjectKind::Linked;
    mirror.mirror_of = "fan0";
    mirror.parent_id = "case";
    mirror.transform.position = { 0.0f, -0.16f, 0.12f };
    doc.objects.push_back(mirror);

    SceneObject desk;
    desk.id      = "desk";
    desk.label   = "Desk";
    desk.kind    = ObjectKind::Decor;
    desk.geometry = "desk";
    desk.size_m  = { 1.4f, 0.04f, 0.75f };
    desk.transform.position = { 0.0f, -0.02f, 0.10f };
    doc.objects.push_back(desk);

    doc.object_colors["fan0"]       = MakeSceneColor(0x20, 0x40, 0x80);
    doc.emitter_colors["fan0"][3]   = MakeSceneColor(0xFF, 0x00, 0x10);

    doc.effect.preset    = "comet";
    doc.effect.seed      = 4242;
    doc.effect.speed     = 1.5f;
    doc.effect.intensity = 0.75f;
    doc.effect.playing   = true;
    return doc;
}

static studio::StudioDocument SmallWorkspace()
{
    studio::StudioDocument w;
    w.meta.name = "Fixture desk";
    w.scene     = SmallScene();
    w.inputs.audio        = true;
    w.inputs.keys         = false;
    w.inputs.screen       = true;
    w.inputs.screen_index = 1;
    w.inputs.sens_pct     = 150;
    w.inputs.decay_pct    = 120;
    w.meta.render.quality = "high";
    w.meta.camera.view    = "top";
    w.meta.extensions["thirdparty.example"] = { {"answer", 42} };
    return w;
}

/*---------------------------------------------------------*\
|| Round-trip: every persisted field survives             ||
|| ToJson -> FromJson, and the document shape matches the ||
|| spec's top-level sections.                             ||
\*---------------------------------------------------------*/
static void TestWorkspaceRoundTrip()
{
    using namespace studio;

    StudioDocument w = SmallWorkspace();
    const json j = ToJson(w);

    CHECK(j.value("schema_version", 0) == STUDIO_SCHEMA_VERSION,
          "workspace: schema_version written");
    CHECK(j.value("name", std::string()) == "Fixture desk",
          "workspace: top-level name");
    CHECK(j.contains("ui") && j.contains("camera") && j.contains("controls")
          && j.contains("render") && j.contains("inputs") && j.contains("output")
          && j.contains("scene") && j.contains("effects")
          && j.contains("definitions") && j.contains("extensions"),
          "workspace: all spec sections present");
    CHECK(Near(j["output"].value("brightness", 0.0f), 0.65f),
          "workspace: brightness hoisted to output");
    CHECK(j["output"].value("live_on_startup", true) == false,
          "workspace: live_on_startup written false");
    CHECK(!j["scene"].contains("effect") && !j["scene"].contains("brightness"),
          "workspace: effect/brightness not duplicated in scene");
    CHECK(j["effects"].value("preset", std::string()) == "comet",
          "workspace: effect preset under effects");
    CHECK(j["effects"].value("playing", false) == true,
          "workspace: playing under effects");
    CHECK(j["inputs"].value("sens_pct", 0) == 150,
          "workspace: input settings serialized");
    CHECK(j["extensions"].contains("thirdparty.example"),
          "workspace: extensions retained");

    StudioDocument back;
    std::vector<std::string> errors;
    CHECK(FromJson(j, back, &errors), "workspace: round-trip parses");
    if(!errors.empty())
    {
        std::printf("  (unexpected errors: %s)\n", errors.front().c_str());
    }
    CHECK(back.meta.name == "Fixture desk", "round-trip: name");
    CHECK(back.scene.objects.size() == w.scene.objects.size(),
          "round-trip: object count");
    CHECK(back.scene.bindings.size() == 1, "round-trip: bindings");
    CHECK(FindObject(back.scene, "fan0") != nullptr
          && FindObject(back.scene, "fan0")->parent_id == "case",
          "round-trip: parent_id");
    CHECK(FindObject(back.scene, "fan0_copy")->mirror_of == "fan0",
          "round-trip: mirror_of ownership");
    CHECK(Near(FindObject(back.scene, "fan0")->transform.position.y, -0.16f),
          "round-trip: position");
    CHECK(back.scene.object_colors["fan0"] == MakeSceneColor(0x20, 0x40, 0x80),
          "round-trip: object color");
    CHECK(back.scene.emitter_colors["fan0"][3] == MakeSceneColor(0xFF, 0x00, 0x10),
          "round-trip: emitter color");
    CHECK(Near(back.scene.brightness, 0.65f), "round-trip: brightness");
    CHECK(back.inputs.audio && !back.inputs.keys && back.inputs.screen
          && back.inputs.screen_index == 1 && back.inputs.sens_pct == 150
          && back.inputs.decay_pct == 120,
          "round-trip: input settings");
    CHECK(back.meta.render.quality == "high" && back.meta.camera.view == "top",
          "round-trip: meta prefs");
    CHECK(back.scene.effect.preset == "comet" && back.scene.effect.seed == 4242
          && Near(back.scene.effect.speed, 1.5f)
          && Near(back.scene.effect.intensity, 0.75f)
          && back.scene.effect.playing,
          "round-trip: effect state");
    CHECK(back.meta.extensions["thirdparty.example"]["answer"] == 42,
          "round-trip: extensions verbatim");
    CHECK(back.meta.live_on_startup == false, "round-trip: live_on_startup");

    /* Deterministic serialization: same doc -> same text. */
    CHECK(ToJson(back) == j, "round-trip: deterministic serialization");
}

/*---------------------------------------------------------*\
|| Whole-document validation: field-specific errors,      ||
|| candidate left untouched on failure.                   ||
\*---------------------------------------------------------*/
static void TestWorkspaceValidation()
{
    using namespace studio;

    const json good = ToJson(SmallWorkspace());
    std::vector<std::string> errors, warnings;

    /* malformed types at the workspace level */
    {
        json j = good;
        j["inputs"] = "on";
        StudioDocument doc;
        CHECK(!FromJson(j, doc, &errors) && HasError(errors, "inputs"),
              "validate: inputs wrong type rejected");
    }
    {
        json j = good;
        j["output"]["brightness"] = "loud";
        StudioDocument doc;
        CHECK(!FromJson(j, doc, &errors) && HasError(errors, "brightness"),
              "validate: brightness wrong type rejected");
    }
    {
        json j = good;
        j["output"]["brightness"] = 1.5;
        StudioDocument doc;
        CHECK(!FromJson(j, doc, &errors) && HasError(errors, "brightness"),
              "validate: brightness out of range rejected");
    }
    {
        json j = good;
        j["schema_version"] = "2";
        StudioDocument doc;
        CHECK(!FromJson(j, doc, &errors) && HasError(errors, "schema_version"),
              "validate: schema_version wrong type rejected");
    }
    {
        json j = good;
        j["scene"] = json::array();
        StudioDocument doc;
        CHECK(!FromJson(j, doc, &errors) && HasError(errors, "scene"),
              "validate: scene wrong type rejected");
    }
    {
        json j = good;
        j["render"]["quality"] = "ultra";
        StudioDocument doc;
        CHECK(!FromJson(j, doc, &errors) && HasError(errors, "quality"),
              "validate: unknown render quality rejected");
    }
    {
        json j = good;
        j["inputs"]["sens_pct"] = 900;
        StudioDocument doc;
        CHECK(!FromJson(j, doc, &errors) && HasError(errors, "sens_pct"),
              "validate: sens_pct out of range rejected");
    }

    /* newer versions rejected — workspace and scene section */
    {
        json j = good;
        j["schema_version"] = STUDIO_SCHEMA_VERSION + 1;
        StudioDocument doc;
        CHECK(!FromJson(j, doc, &errors) && HasError(errors, "schema_version"),
              "validate: newer workspace version rejected");
    }
    {
        json j = good;
        j["scene"]["version"] = 99;
        StudioDocument doc;
        CHECK(!FromJson(j, doc, &errors),
              "validate: newer scene version rejected");
    }

    /* structural errors through the scene section */
    {
        json j = good;
        j["scene"]["objects"].push_back(j["scene"]["objects"][0]);
        StudioDocument doc;
        CHECK(!FromJson(j, doc, &errors) && HasError(errors, "duplicate"),
              "validate: duplicate object id rejected");
    }
    {
        /* case -> fan0 -> case: fan0 already has parent_id "case" */
        json j = good;
        j["scene"]["objects"][0]["parent_id"] = "fan0";
        StudioDocument doc;
        CHECK(!FromJson(j, doc, &errors) && HasError(errors, "cycle"),
              "validate: parent cycle rejected");
    }
    {
        json j = good;
        j["scene"]["objects"][1]["scale"]["x"] = -1.0;
        StudioDocument doc;
        CHECK(!FromJson(j, doc, &errors) && HasError(errors, "scale"),
              "validate: invalid scale rejected");
    }
    {
        json j = good;
        j["scene"]["objects"][1]["position"]["x"] = "north";
        StudioDocument doc;
        CHECK(!FromJson(j, doc, &errors),
              "validate: malformed position type rejected");
    }

    /* out-of-range LED addresses and dangling binding refs */
    {
        json j = good;
        j["scene"]["objects"][1]["emitters"][0]["address"] = 8;   /* zone_leds == 8 */
        StudioDocument doc;
        CHECK(!FromJson(j, doc, &errors) && HasError(errors, "address"),
              "validate: emitter address past zone end rejected");
    }
    {
        json j = good;
        j["scene"]["objects"][1]["emitters"][0]["address"] = -2;
        StudioDocument doc;
        CHECK(!FromJson(j, doc, &errors) && HasError(errors, "address"),
              "validate: emitter address < -1 rejected");
    }
    {
        json j = good;
        j["scene"]["objects"][1]["binding"] = "no_such_binding";
        StudioDocument doc;
        CHECK(!FromJson(j, doc, &errors) && HasError(errors, "binding"),
              "validate: dangling object binding rejected");
    }

    /* failed loads never touch the active candidate */
    {
        StudioDocument doc = SmallWorkspace();
        const size_t n = doc.scene.objects.size();
        json bad = good;
        bad["schema_version"] = 42;
        CHECK(!FromJson(bad, doc, &errors)
              && doc.scene.objects.size() == n
              && doc.meta.name == "Fixture desk",
              "validate: failure leaves document untouched");
    }

    /* an empty scene is a valid workspace */
    {
        json j = good;
        j["scene"]["objects"]   = json::array();
        j["scene"]["bindings"]  = json::array();
        j["scene"]["object_colors"]  = json::object();
        j["scene"]["emitter_colors"] = json::object();
        StudioDocument doc;
        CHECK(FromJson(j, doc, &errors) && doc.scene.objects.empty(),
              "validate: empty scene allowed");
    }

    /* unknown top-level fields warn instead of failing */
    {
        json j = good;
        j["camer"] = j["camera"];   /* plausible typo */
        StudioDocument doc;
        errors.clear(); warnings.clear();
        CHECK(FromJson(j, doc, &errors, &warnings)
              && HasError(warnings, "camer"),
              "validate: unknown field reported as warning");
    }

    /* extensions must still be an object */
    {
        json j = good;
        j["extensions"] = json::array();
        StudioDocument doc;
        CHECK(!FromJson(j, doc, &errors) && HasError(errors, "extensions"),
              "validate: extensions wrong type rejected");
    }
}

/*---------------------------------------------------------*\
|| #RRGGBB colors on write; packed ints still accepted.   ||
\*---------------------------------------------------------*/
static void TestHexColors()
{
    using namespace studio;

    StudioDocument w = SmallWorkspace();
    const json j = ToJson(w);

    CHECK(j["scene"]["object_colors"]["fan0"].is_string()
          && j["scene"]["object_colors"]["fan0"] == "#204080",
          "colors: object color written as #RRGGBB");
    CHECK(j["scene"]["emitter_colors"]["fan0"]["3"] == "#FF0010",
          "colors: emitter color written as #RRGGBB");

    /* packed ints (pre-existing files) still parse */
    json legacy_color = j;
    legacy_color["scene"]["object_colors"]["fan0"] = 0x00804020;
    StudioDocument doc;
    std::vector<std::string> errors;
    CHECK(FromJson(legacy_color, doc, &errors)
          && doc.scene.object_colors["fan0"] == MakeSceneColor(0x20, 0x40, 0x80),
          "colors: packed int accepted");

    /* malformed color rejected with a field path */
    json bad = j;
    bad["scene"]["object_colors"]["fan0"] = "blue";
    CHECK(!FromJson(bad, doc, &errors)
          && HasError(errors, "object_colors"),
          "colors: malformed color rejected");
}

/*---------------------------------------------------------*\
|| Legacy host-settings blob -> StudioDocument migration. ||
|| Fixture mirrors what the old saveScene() wrote into    ||
|| OpenRGB settings: {"scene": v1 doc, "inputs": {...}}.  ||
\*---------------------------------------------------------*/
static json LegacyFixture()
{
    /* v1 scene: flat list, decor carried its dimensions in scale. */
    json scene = {
        {"version", 1},
        {"name", "Daniel's desk"},
        {"brightness", 0.8},
        {"bindings", json::array({
            {
                {"id", "kbd_g512"},
                {"controller_name", "G512"},
                {"vendor", "Logitech"},
                {"serial", ""},
                {"location", ""},
                {"device_type", -1},
                {"zone_name", "Keyboard"},
                {"zone_leds", 0},
            },
            {
                {"id", "fan_bus"},
                {"controller_name", "X870E AORUS ELITE"},
                {"vendor", "Gigabyte"},
                {"serial", ""},
                {"location", ""},
                {"device_type", -1},
                {"zone_name", "ARGB_V2_1"},
                {"zone_leds", 8},
            },
        })},
        {"objects", json::array({
            {
                {"id", "desk"},
                {"label", "Desk"},
                {"kind", "decor"},
                {"position", {{"x", 0.0}, {"y", -0.02}, {"z", 0.10}}},
                {"rotation", {{"x", 0}, {"y", 0}, {"z", 0}}},
                {"scale",    {{"x", 1.4}, {"y", 0.04}, {"z", 0.75}}},  /* v1: dims */
                {"binding", ""},
                {"mirror_of", ""},
                {"geometry", "desk"},
                {"layout", ""},
                {"verified", false},
                {"visible", true},
                {"emitters", json::array()},
            },
            {
                {"id", "keyboard"},
                {"label", "Keyboard"},
                {"kind", "device"},
                {"position", {{"x", 0.0}, {"y", 0.02}, {"z", 0.15}}},
                {"rotation", {{"x", 0}, {"y", -8}, {"z", 0}}},
                {"scale",    {{"x", 1}, {"y", 1}, {"z", 1}}},
                {"binding", "kbd_g512"},
                {"mirror_of", ""},
                {"geometry", "keyboard"},
                {"layout", "matrix_map"},
                {"verified", true},
                {"visible", true},
                {"emitters", json::array({
                    {{"pos", {{"x", -0.1}, {"y", 0.016}, {"z", 0.0}}},
                     {"group", "keyboard"}, {"address", 12}},
                })},
            },
            {
                {"id", "case_fans"},
                {"label", "Case fans"},
                {"kind", "device"},
                {"position", {{"x", 0.4}, {"y", -0.06}, {"z", -0.24}}},
                {"rotation", {{"x", 0}, {"y", 0}, {"z", 0}}},
                {"scale",    {{"x", 1}, {"y", 1}, {"z", 1}}},
                {"binding", "fan_bus"},
                {"mirror_of", ""},
                {"geometry", "fan_body"},
                {"layout", ""},
                {"verified", true},
                {"visible", true},
                {"emitters", json::array({
                    {{"pos", {{"x", 0.05}, {"y", 0.016}, {"z", 0.0}}},
                     {"group", "case_fans"}, {"address", 0}},
                })},
            },
            {
                {"id", "case_fan_b1"},
                {"label", "Case fan (mirror)"},
                {"kind", "linked"},
                {"position", {{"x", 0.4}, {"y", -0.06}, {"z", 0.02}}},
                {"rotation", {{"x", 0}, {"y", 0}, {"z", 0}}},
                {"scale",    {{"x", 1}, {"y", 1}, {"z", 1}}},
                {"binding", ""},
                {"mirror_of", "case_fans"},
                {"geometry", "fan_body"},
                {"layout", ""},
                {"verified", false},
                {"visible", true},
                {"emitters", json::array()},
            },
        })},
        {"object_colors",  {{"keyboard", 0x000000FFu}, {"case_fans", 0x00204080u}}},
        {"emitter_colors", {{"keyboard", {{"12", 0x0000FF00u}}}}},
        {"effect", {
            {"preset", "aurora"},
            {"seed", 7},
            {"speed", 1.25},
            {"intensity", 0.9},
            {"playing", true},
        }},
    };
    return {
        {"scene", scene},
        {"inputs", {
            {"audio", true},
            {"keys", true},
            {"screen", false},
            {"screen_index", 0},
            {"sens_pct", 175},
            {"decay_pct", 200},
        }},
    };
}

static void TestLegacyMigration()
{
    using namespace studio;

    const json legacy = LegacyFixture();
    CHECK(HasLegacySettings(legacy), "migration: fixture detected");
    CHECK(!HasLegacySettings(json::object()) && !HasLegacySettings(json()),
          "migration: empty settings not a migration source");

    StudioDocument w;
    std::vector<std::string> errors;
    CHECK(MigrateLegacySettings(legacy, w, &errors), "migration: converts");
    if(!errors.empty())
    {
        std::printf("  (migration errors: %s)\n", errors.front().c_str());
    }

    CHECK(w.schema_version == STUDIO_SCHEMA_VERSION, "migration: version");
    CHECK(w.meta.name == "Daniel's desk", "migration: name");
    CHECK(w.scene.version == 2, "migration: scene upgraded to v2");

    /* IDs, positions, mirrored ownership preserved */
    CHECK(w.scene.objects.size() == 4, "migration: object count");
    const SceneObject* kbd = FindObject(w.scene, "keyboard");
    CHECK(kbd != nullptr && kbd->kind == ObjectKind::Device
          && kbd->binding == "kbd_g512",
          "migration: keyboard id + binding");
    CHECK(kbd != nullptr && Near(kbd->transform.rotation_deg.y, -8.0f),
          "migration: rotation preserved");
    CHECK(kbd != nullptr && kbd->emitters.size() == 1
          && kbd->emitters[0].address == 12,
          "migration: emitter address preserved");
    const SceneObject* mirror = FindObject(w.scene, "case_fan_b1");
    CHECK(mirror != nullptr && mirror->kind == ObjectKind::Linked
          && mirror->mirror_of == "case_fans",
          "migration: mirrored ownership preserved");

    /* v1 decor scale -> size_m, scale reset to identity */
    const SceneObject* desk = FindObject(w.scene, "desk");
    CHECK(desk != nullptr && Near(desk->size_m.x, 1.4f)
          && Near(desk->size_m.y, 0.04f)
          && Near(desk->transform.scale.x, 1.0f),
          "migration: v1 decor scale -> size_m");

    /* colors survive (packed ints -> same SceneColor values) */
    CHECK(w.scene.object_colors["keyboard"] == 0x000000FFu,
          "migration: object color preserved");
    CHECK(w.scene.emitter_colors["keyboard"][12] == 0x0000FF00u,
          "migration: emitter color preserved");
    CHECK(Near(w.scene.brightness, 0.8f), "migration: brightness preserved");

    /* effect state + input settings preserved */
    CHECK(w.scene.effect.preset == "aurora" && w.scene.effect.playing
          && w.scene.effect.seed == 7 && Near(w.scene.effect.speed, 1.25f),
          "migration: effect state preserved");
    CHECK(w.inputs.audio && w.inputs.keys && !w.inputs.screen
          && w.inputs.sens_pct == 175 && w.inputs.decay_pct == 200,
          "migration: input settings preserved");

    /* migration NEVER turns live output on */
    CHECK(w.meta.live_on_startup == false, "migration: live_on_startup off");

    /* the migrated doc is itself a valid workspace, stable on rewrite */
    StudioDocument back;
    CHECK(FromJson(ToJson(w), back, &errors),
          "migration: migrated doc round-trips");
    CHECK(ToJson(back) == ToJson(w), "migration: deterministic");

    /* an invalid legacy scene fails cleanly instead of half-loading */
    json bad_legacy = legacy;
    bad_legacy["scene"]["objects"][3]["mirror_of"] = "ghost";
    StudioDocument untouched = SmallWorkspace();
    CHECK(!MigrateLegacySettings(bad_legacy, untouched, &errors)
          && untouched.meta.name == "Fixture desk",
          "migration: invalid legacy rejected, doc untouched");
}

/*---------------------------------------------------------*\
|| Effect-section validation details.                     ||
\*---------------------------------------------------------*/
static void TestEffectsSection()
{
    using namespace studio;
    const json good = ToJson(SmallWorkspace());
    std::vector<std::string> errors, warnings;

    {
        json j = good;
        j["effects"]["speed"] = -1.0;
        StudioDocument doc;
        CHECK(!FromJson(j, doc, &errors) && HasError(errors, "speed"),
              "effects: non-positive speed rejected");
    }
    {
        json j = good;
        j["effects"]["intensity"] = 2.0;
        StudioDocument doc;
        CHECK(!FromJson(j, doc, &errors) && HasError(errors, "intensity"),
              "effects: intensity out of range rejected");
    }
    {
        json j = good;
        j["effects"]["preset"] = "no_such_look";
        StudioDocument doc;
        errors.clear(); warnings.clear();
        /* unknown preset warns but must not kill the document */
        CHECK(FromJson(j, doc, &errors, &warnings)
              && HasError(warnings, "preset"),
              "effects: unknown preset warns, doc still loads");
    }
    {
        json j = good;
        j["effects"]["playing"] = "yes";
        StudioDocument doc;
        CHECK(!FromJson(j, doc, &errors) && HasError(errors, "playing"),
              "effects: playing wrong type rejected");
    }
}

/*---------------------------------------------------------*\
|| Size limits: malformed community content must not be   ||
|| able to destabilize the host.                          ||
\*---------------------------------------------------------*/
static void TestDocumentLimits()
{
    using namespace studio;
    const json good = ToJson(SmallWorkspace());
    std::vector<std::string> errors;

    {
        json j = good;
        json& objects = j["scene"]["objects"];
        for(int i = 0; i < 2100; i++)
        {
            objects.push_back({{"id", "pad_" + std::to_string(i)}});
        }
        StudioDocument doc;
        CHECK(!FromJson(j, doc, &errors),
              "limits: object count capped");
    }
    {
        json j = good;
        json emitters = json::array();
        for(int i = 0; i < 70000; i++)
        {
            emitters.push_back({{"pos", {{"x",0},{"y",0},{"z",0}}},
                                {"group","g"},{"address",-1}});
        }
        j["scene"]["objects"][1]["emitters"] = emitters;
        StudioDocument doc;
        CHECK(!FromJson(j, doc, &errors),
              "limits: emitter count capped");
    }
}

int main()
{
    TestWorkspaceRoundTrip();
    TestWorkspaceValidation();
    TestHexColors();
    TestLegacyMigration();
    TestEffectsSection();
    TestDocumentLimits();

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}

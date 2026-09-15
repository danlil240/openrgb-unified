/*---------------------------------------------------------*\
|| studio_scene_test.cpp                                     |
||                                                           |
||   Deterministic tests for the Desktop Lighting Studio    |
||   scene core (plugins/DesktopLightingStudio/scene/).     |
||   No Qt, no hardware — plain cl build.                   |
||                                                           |
||   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "scene/SceneTypes.h"
#include "scene/EmitterLayout.h"
#include "scene/SceneJson.h"
#include "scene/BindingResolver.h"
#include "scene/DefaultDesk.h"
#include "effects/EffectTypes.h"
#include "effects/EffectEngine.h"
#include "effects/Presets.h"

#include <cmath>
#include <cstdio>
#include <string>

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

static void TestTransform()
{
    using namespace studio;

    Transform t;
    t.position = { 1.0f, 2.0f, 3.0f };
    Vec3 p = TransformPoint(t, { 0.1f, 0.0f, 0.0f });
    CHECK(Near(p.x, 1.1f) && Near(p.y, 2.0f) && Near(p.z, 3.0f), "transform translate");

    t.scale = { 2.0f, 1.0f, 1.0f };
    p = TransformPoint(t, { 0.1f, 0.0f, 0.0f });
    CHECK(Near(p.x, 1.2f), "transform scale");

    /* +90 deg about X: +Y -> +Z */
    t = Transform{};
    t.rotation_deg = { 90.0f, 0.0f, 0.0f };
    p = TransformPoint(t, { 0.0f, 1.0f, 0.0f });
    CHECK(Near(p.z, 1.0f) && Near(p.y, 0.0f), "transform rotX 90");

    /* +90 deg about Y: +X -> -Z */
    t.rotation_deg = { 0.0f, 90.0f, 0.0f };
    p = TransformPoint(t, { 1.0f, 0.0f, 0.0f });
    CHECK(Near(p.z, -1.0f) && Near(p.x, 0.0f), "transform rotY 90");

    /* +90 deg about Z: +X -> +Y */
    t.rotation_deg = { 0.0f, 0.0f, 90.0f };
    p = TransformPoint(t, { 1.0f, 0.0f, 0.0f });
    CHECK(Near(p.y, 1.0f) && Near(p.x, 0.0f), "transform rotZ 90");
}

static void TestRing()
{
    using namespace studio;

    auto ring = layout::Ring(8, 0.05f, 0.0f, false, "g");
    CHECK(ring.size() == 8, "ring count");
    CHECK(Near(ring[0].local_pos.x, 0.05f) && Near(ring[0].local_pos.z, 0.0f), "ring led0 +X");
    /* address 2 at 90 deg -> +X rotates toward -Z */
    CHECK(Near(ring[2].local_pos.x, 0.0f) && Near(ring[2].local_pos.z, -0.05f), "ring led2 angle");
    CHECK(ring[3].address == 3, "ring sequential addresses");

    auto rev = layout::Ring(8, 0.05f, 0.0f, true, "g");
    CHECK(Near(rev[2].local_pos.z, 0.05f), "ring reversed direction");

    auto rot = layout::Ring(8, 0.05f, 90.0f, false, "g", 4);
    CHECK(Near(rot[0].local_pos.x, 0.0f) && Near(rot[0].local_pos.z, -0.05f), "ring start angle");
    CHECK(rot[0].address == 4, "ring address base");

    /* all emitters on the circle */
    for(const auto& e : ring)
    {
        const float r = std::sqrt(e.local_pos.x * e.local_pos.x + e.local_pos.z * e.local_pos.z);
        CHECK(Near(r, 0.05f), "ring radius");
    }
}

static void TestStripAndMatrix()
{
    using namespace studio;

    auto strip = layout::Strip(10, 0.012f, { -0.05f, 0.01f, 0.0f }, "g");
    CHECK(strip.size() == 10, "strip count");
    CHECK(Near(strip[9].local_pos.x, -0.05f + 9 * 0.012f), "strip spacing");

    unsigned int map[6] = { 0, 1, 99, 2, 3, 4 };
    auto keys = layout::KeyboardMatrix(2, 3, map, 99u, 0.019f, 0.019f, { 0, 0, 0 }, "kbd");
    CHECK(keys.size() == 5, "matrix skips empty cell");
    CHECK(keys[2].address == 2, "matrix address from map");
    CHECK(Near(keys[4].local_pos.z, -0.019f), "matrix row 1 offset");
}

static void TestMirrorSemantics()
{
    using namespace studio;

    SceneDocument doc;
    SceneObject owner;
    owner.id   = "ring";
    owner.kind = ObjectKind::Device;
    doc.objects.push_back(owner);

    SceneObject copy;
    copy.id        = "ring_copy";
    copy.kind      = ObjectKind::Linked;
    copy.mirror_of = "ring";
    doc.objects.push_back(copy);

    doc.object_colors["ring"] = MakeSceneColor(10, 20, 30);
    doc.emitter_colors["ring"][2] = MakeSceneColor(200, 0, 0);

    CHECK(OutputOwner(doc, "ring_copy") != nullptr
          && OutputOwner(doc, "ring_copy")->id == "ring", "mirror follows owner");
    CHECK(EmitterColor(doc, "ring_copy", 0) == MakeSceneColor(10, 20, 30),
          "mirror reads source base color");
    CHECK(EmitterColor(doc, "ring_copy", 2) == MakeSceneColor(200, 0, 0),
          "mirror reads source emitter override");
    CHECK(EmitterColor(doc, "missing", 0) == 0, "missing object -> black");
}

static void TestJsonRoundTrip()
{
    using namespace studio;

    SceneDocument doc = BuildDefaultDesk();
    doc.brightness = 0.7f;
    doc.emitter_colors["keyboard"][42] = MakeSceneColor(1, 2, 3);

    SceneDocument back;
    CHECK(FromJson(ToJson(doc), back), "json round-trip parses");
    CHECK(back.version == 1, "json version");
    CHECK(back.objects.size() == doc.objects.size(), "json object count");
    CHECK(back.bindings.size() == doc.bindings.size(), "json binding count");
    CHECK(Near(back.brightness, 0.7f), "json brightness");
    CHECK(back.emitter_colors["keyboard"][42] == MakeSceneColor(1, 2, 3),
          "json emitter override");

    const SceneObject* kbd = FindObject(back, "keyboard");
    CHECK(kbd != nullptr && kbd->kind == ObjectKind::Device
          && kbd->layout == "matrix_map" && kbd->verified, "json keyboard fields");

    /* unknown fields tolerated */
    nlohmann::json j = ToJson(doc);
    j["future_field"] = { {"nested", 1} };
    j["objects"][0]["future"] = "x";
    SceneDocument tol;
    CHECK(FromJson(j, tol) && tol.objects.size() == doc.objects.size(),
          "json unknown fields tolerated");

    /* newer version rejected */
    j["version"] = 99;
    CHECK(!FromJson(j, tol), "json newer version rejected");

    /* malformed rejected */
    CHECK(!FromJson(nlohmann::json::array(), tol), "json malformed rejected");
}

static std::vector<studio::ControllerSnapshot> FakeControllers()
{
    using namespace studio;
    std::vector<ControllerSnapshot> cs;

    ControllerSnapshot kbd;
    kbd.name = "Logitech G512"; kbd.vendor = "Logitech"; kbd.device_type = 5;
    kbd.zones.push_back({ "Keyboard", 107, 0, 2, true });
    cs.push_back(kbd);

    ControllerSnapshot mb;
    mb.name = "X870E AORUS ELITE WIFI7 ICE"; mb.vendor = "Gigabyte";
    mb.device_type = 0; mb.location = "HID: /dev/0";
    mb.zones.push_back({ "ARGB_V2_1", 8, 0, 3, true });
    mb.zones.push_back({ "ARGB_V2_2", 16, 8, 3, true });
    cs.push_back(mb);

    ControllerSnapshot dimm0;
    dimm0.name = "Corsair Vengeance RGB"; dimm0.vendor = "Corsair";
    dimm0.device_type = 1; dimm0.location = "SMBus: 0x58";
    dimm0.zones.push_back({ "DIMM", 10, 0, 1, true });
    cs.push_back(dimm0);

    ControllerSnapshot dimm1 = dimm0;
    dimm1.location = "SMBus: 0x59";
    dimm1.serial   = "SN123";
    cs.push_back(dimm1);

    return cs;
}

static void TestResolver()
{
    using namespace studio;
    const auto cs = FakeControllers();

    /* exact + substring match, zone by name */
    DeviceBinding b{ "b1", "X870E AORUS ELITE", "Gigabyte", "", "", -1, "ARGB_V2_2", 16 };
    auto r = ResolveBinding(b, cs);
    CHECK(r.status == BindingStatus::Resolved && r.zone_index == 1, "resolve mb zone");

    /* zone LED-count mismatch still resolves but reports */
    b.zone_leds = 8;
    r = ResolveBinding(b, cs);
    CHECK(r.status == BindingStatus::Resolved && !r.reason.empty(), "resolve count mismatch");

    /* identical controllers -> ambiguous without disambiguator */
    DeviceBinding d{ "d0", "Corsair", "Corsair", "", "", 1, "", 0 };
    r = ResolveBinding(d, cs);
    CHECK(r.status == BindingStatus::Ambiguous, "resolve ambiguous dimms");

    /* location narrows to one */
    d.location = "SMBus: 0x59";
    r = ResolveBinding(d, cs);
    CHECK(r.status == BindingStatus::Resolved && r.controller_index == 3,
          "resolve location disambiguation");

    /* serial wins even with a shared name */
    DeviceBinding s{ "s0", "Corsair", "Corsair", "SN123", "", -1, "", 0 };
    r = ResolveBinding(s, cs);
    CHECK(r.status == BindingStatus::Resolved && r.controller_index == 3,
          "resolve serial disambiguation");

    /* missing zone -> unresolved with reason */
    DeviceBinding z{ "z0", "Logitech G512", "", "", "", -1, "NoSuchZone", 0 };
    r = ResolveBinding(z, cs);
    CHECK(r.status == BindingStatus::Unresolved && r.controller_index == 0,
          "resolve zone missing");

    /* missing controller -> unresolved */
    DeviceBinding m{ "m0", "Nonexistent", "", "", "", -1, "", 0 };
    r = ResolveBinding(m, cs);
    CHECK(r.status == BindingStatus::Unresolved && r.controller_index == -1,
          "resolve controller missing");

    /* reordered enumeration still resolves the same identity */
    auto reordered = cs;
    std::swap(reordered[1], reordered[3]);
    r = ResolveBinding(DeviceBinding{ "b1", "X870E AORUS ELITE", "Gigabyte",
                                      "", "", -1, "ARGB_V2_1", 8 }, reordered);
    CHECK(r.status == BindingStatus::Resolved && r.controller_index == 3
          && r.zone_index == 0, "resolve survives reordering");
}

static void TestDefaultDesk()
{
    using namespace studio;
    SceneDocument doc = BuildDefaultDesk();

    CHECK(FindObject(doc, "keyboard") != nullptr, "desk has keyboard");
    CHECK(FindObject(doc, "case_fans") != nullptr, "desk has case fans owner");

    /* mirrored copies exist and point at the owner */
    const SceneObject* mirror = FindObject(doc, "case_fan_b1");
    CHECK(mirror != nullptr && mirror->kind == ObjectKind::Linked
          && mirror->mirror_of == "case_fans", "desk mirror wiring");

    /* unverified GPU fans refuse nothing here but carry the flag */
    const SceneObject* gf = FindObject(doc, "gpu_fan_r");
    CHECK(gf != nullptr && !gf->verified, "gpu fans unverified");

    /* every Device binding id exists */
    bool all_bound = true;
    for(const SceneObject& o : doc.objects)
    {
        if(o.kind == ObjectKind::Device && !o.binding.empty())
        {
            bool found = false;
            for(const DeviceBinding& b : doc.bindings)
            {
                if(b.id == o.binding) { found = true; break; }
            }
            if(!found) all_bound = false;
        }
    }
    CHECK(all_bound, "all device objects have bindings");
}

/*---------------------------------------------------------*\
||| Stage 2 — effects engine                                 |
\*---------------------------------------------------------*/
static studio::SceneObject OneLedDevice(const std::string& id, float x)
{
    using namespace studio;
    SceneObject o;
    o.id   = id;
    o.kind = ObjectKind::Device;
    o.geometry = "test_body";
    o.transform.position = { x, 0.0f, 0.0f };
    o.verified = true;
    Emitter e; e.local_pos = { 0, 0, 0 }; e.group = id; e.address = 0;
    o.emitters.push_back(e);
    return o;
}

static int RedOf(studio::SceneColor c) { return (int)(c & 0xFF); }

static void TestPaletteAndBlend()
{
    using namespace studio;

    Palette p = MakePalette({ MakeSceneColor(0, 0, 0), MakeSceneColor(255, 255, 255) });
    CHECK(Near(p.Sample(0.0f).r, 0.0f), "palette stop 0");
    CHECK(Near(p.Sample(0.5f).r, 1.0f), "palette stop 1");
    CHECK(Near(p.Sample(0.25f).r, 0.5f), "palette midpoint");
    /* wraps: u = 1.25 lands back on the 0.5 segment */
    CHECK(Near(p.Sample(1.5f).r, p.Sample(0.5f).r), "palette wraps");
    CHECK(p.stops.empty() == false && Palette{}.Sample(0.3f).r == 0.0f,
          "empty palette safe");

    const ColorF black = ToColorF(0), white = ToColorF(MakeSceneColor(255, 255, 255));
    const ColorF mid { 0.5f, 0.5f, 0.5f, 1.0f };
    ColorF half = white; half.a = 0.5f;       /* white at 50% coverage */

    ColorF r = BlendOver(black, half, BlendMode::Replace);
    CHECK(Near(r.r, 0.5f), "blend replace");
    r = BlendOver(black, half, BlendMode::Add);
    CHECK(Near(r.r, 0.5f), "blend add");
    r = BlendOver(white, half, BlendMode::Add);
    CHECK(Near(r.r, 1.0f), "blend add clamps");
    r = BlendOver(mid, half, BlendMode::Screen);
    CHECK(Near(r.r, 0.75f), "blend screen");   /* 1-(0.5)(0.5) */
    ColorF zero; zero.a = 0.0f;
    r = BlendOver(white, zero, BlendMode::Add);
    CHECK(Near(r.r, 1.0f), "blend zero coverage keeps dst");
}

static void TestWaveSpatial()
{
    using namespace studio;

    /* Linear ramp black->white->black over 1 m along +X. */
    EffectLayer wave;
    wave.primitive = "wave";
    wave.direction = { 1.0f, 0.0f, 0.0f };
    wave.scale     = 1.0f;
    wave.speed     = 0.0f;
    wave.density   = 0.0f;          /* full coverage, pure color sweep */
    wave.palette   = MakePalette({
        { 0.0f, ToColorF(MakeSceneColor(0, 0, 0))     },
        { 0.5f, ToColorF(MakeSceneColor(255, 255, 255)) },
        { 1.0f, ToColorF(MakeSceneColor(0, 0, 0))     },
    });

    SceneDocument doc;
    doc.objects.push_back(OneLedDevice("near", 0.125f));
    doc.objects.push_back(OneLedDevice("mid",  0.25f));
    doc.objects.push_back(OneLedDevice("far",  0.45f));
    doc.objects.push_back(OneLedDevice("wrap", 1.125f));  /* same u as near (exact) */

    EffectEngine engine;
    engine.SetLayers({ wave });
    FrameColors frame;
    engine.Evaluate(doc, 0.0, frame);

    /* gate property: a wave crossing devices lands in spatial order */
    CHECK(RedOf(frame["near"][0]) < RedOf(frame["mid"][0]),  "wave order near<mid");
    CHECK(RedOf(frame["mid"][0])  < RedOf(frame["far"][0]),  "wave order mid<far");
    CHECK(frame["wrap"][0] == frame["near"][0],              "wave periodic wrap");

    /* determinism: same t -> identical frame; later t -> phase moves */
    FrameColors again;
    engine.Evaluate(doc, 0.0, again);
    CHECK(again == frame, "engine deterministic at fixed t");

    EffectLayer moving = wave; moving.speed = 0.1f;
    engine.SetLayers({ moving });
    FrameColors moved;
    engine.Evaluate(doc, 0.0, moved);
    engine.Evaluate(doc, 1.0, frame);
    CHECK(frame["near"][0] != moved["near"][0], "wave advances with t");
}

static void TestPulseCometSpin()
{
    using namespace studio;

    /* pulse: coverage peaks half a spacing ahead of the wavefront */
    EffectLayer pulse;
    pulse.primitive = "pulse";
    pulse.origin    = { 0, 0, 0 };
    pulse.scale     = 0.5f;         /* spacing (m) */
    pulse.speed     = 0.2f;
    pulse.density   = 1.0f;
    pulse.palette   = MakePalette({ MakeSceneColor(255, 255, 255) });
    pulse.opacity   = 1.0f;

    SceneDocument doc;
    /* t=0: wavefront at 0 -> peak coverage at d = 0.25 (u=0.5) */
    doc.objects.push_back(OneLedDevice("front", 0.25f));
    doc.objects.push_back(OneLedDevice("trough", 0.50f));

    EffectEngine engine;
    engine.SetLayers({ pulse });
    FrameColors frame;
    engine.Evaluate(doc, 0.0, frame);
    CHECK(RedOf(frame["front"][0]) > 200 && RedOf(frame["trough"][0]) < 30,
          "pulse ring position");

    /* comet: head bright, tail decays, beyond tail is dark */
    EffectLayer comet;
    comet.primitive = "comet";
    comet.path = { { 0, 0, 0 }, { 1.0f, 0, 0 } };   /* 2 m closed loop  */
    comet.speed   = 0.5f;                           /* m/s              */
    comet.scale   = 0.4f;                           /* tail length (m)  */
    comet.palette = MakePalette({ MakeSceneColor(255, 255, 255),
                                  MakeSceneColor(0, 0, 0) });
    comet.opacity = 1.0f;

    SceneDocument cdoc;
    cdoc.objects.push_back(OneLedDevice("head", 0.5f));    /* t=1s: head at 0.5 */
    cdoc.objects.push_back(OneLedDevice("tail", 0.42f));   /* 0.08 behind head  */
    cdoc.objects.push_back(OneLedDevice("dark", 1.4f));    /* >tail behind      */

    engine.SetLayers({ comet });
    engine.Evaluate(cdoc, 1.0, frame);
    CHECK(RedOf(frame["head"][0]) > 200, "comet head bright");
    CHECK(RedOf(frame["tail"][0]) > 10
          && RedOf(frame["tail"][0]) < RedOf(frame["head"][0]), "comet tail fades");
    CHECK(RedOf(frame["dark"][0]) == 0, "comet beyond tail dark");

    /* spin: N spokes periodic — emitters 2pi/N apart share a color */
    EffectLayer spin;
    spin.primitive = "spin";
    spin.space     = CoordSpace::Local;
    spin.scale     = 2.0f;          /* two spokes */
    spin.speed     = 0.0f;
    spin.palette   = MakePalette({ MakeSceneColor(255, 255, 255),
                                   MakeSceneColor(0, 0, 0) });
    spin.opacity   = 1.0f;

    SceneObject ring;
    ring.id   = "ring"; ring.kind = ObjectKind::Device; ring.verified = true;
    ring.geometry = "fan_body";
    ring.emitters = layout::Ring(8, 0.05f, 0.0f, false, "ring");

    SceneDocument sdoc;
    sdoc.objects.push_back(ring);
    engine.SetLayers({ spin });
    engine.Evaluate(sdoc, 0.0, frame);
    CHECK(frame["ring"].size() == 8, "spin fills ring");
    CHECK(frame["ring"][0] == frame["ring"][4], "spin spoke periodicity");
    CHECK(frame["ring"][0] != frame["ring"][2], "spin spoke contrast");
}

static void TestNoiseAndMasks()
{
    using namespace studio;

    /* noise determinism + range */
    const Vec3 p { 1.25f, -0.5f, 3.75f };
    const float n1 = Noise3(p, 7), n2 = Noise3(p, 7);
    CHECK(n1 == n2, "noise deterministic");
    CHECK(n1 >= 0.0f && n1 <= 1.0f, "noise range");
    bool differs = false;
    for(unsigned int s = 0; s < 8 && !differs; s++)
    {
        differs = !Near(Noise3(p, s), n1);
    }
    CHECK(differs, "noise varies by seed");

    /* mask matching: id, geometry, emitter group */
    EffectLayer L; L.primitive = "static";
    L.palette = MakePalette({ MakeSceneColor(255, 0, 0) });
    L.targets = { "kbd", "fan_body", "ring_group" };

    SceneObject kbd; kbd.id = "kbd";      kbd.geometry = "keyboard_body";
    SceneObject fan; fan.id = "rear_fan"; fan.geometry = "fan_body";
    SceneObject oth; oth.id = "pump";     oth.geometry = "pump_body";
    Emitter e; e.group = "ring_group"; Emitter oe; oe.group = "other";

    CHECK(LayerMatches(L, kbd, oe),  "mask by object id");
    CHECK(LayerMatches(L, fan, oe),  "mask by geometry");
    CHECK(LayerMatches(L, oth, e),   "mask by emitter group");
    CHECK(!LayerMatches(L, oth, oe), "mask rejects unmatched");
    L.targets.clear();
    CHECK(LayerMatches(L, oth, oe),  "empty mask matches all");
}

static void TestEngineMirrorAndJson()
{
    using namespace studio;

    /* Linked copies never get frame entries — they read the owner. */
    SceneDocument doc;
    doc.objects.push_back(OneLedDevice("owner", 0.0f));
    SceneObject copy;
    copy.id = "copy"; copy.kind = ObjectKind::Linked; copy.mirror_of = "owner";
    Emitter ce; ce.local_pos = { 0.5f, 0, 0 }; ce.group = "copy"; ce.address = -1;
    copy.emitters.push_back(ce);
    doc.objects.push_back(copy);
    doc.object_colors["owner"] = MakeSceneColor(9, 9, 9);

    EffectLayer solid; solid.primitive = "static";
    solid.palette = MakePalette({ MakeSceneColor(100, 0, 0) });

    EffectEngine engine;
    engine.SetLayers({ solid });
    FrameColors frame;
    engine.Evaluate(doc, 0.0, frame);
    CHECK(frame.count("owner") == 1 && frame.count("copy") == 0,
          "mirror copies own no frame entries");
    CHECK(frame["owner"][0] == MakeSceneColor(100, 0, 0), "static layer color");

    /* untargeted object keeps painted color via fallback (no entry) */
    solid.targets = { "owner" };
    doc.objects.push_back(OneLedDevice("other", 0.2f));
    doc.object_colors["other"] = MakeSceneColor(1, 2, 3);
    engine.SetLayers({ solid });
    engine.Evaluate(doc, 0.0, frame);
    CHECK(frame.count("other") == 0, "untargeted object has no frame entry");

    /* effect state survives the JSON round trip */
    doc.effect.preset = "aurora"; doc.effect.seed = 4242;
    doc.effect.speed = 1.5f; doc.effect.intensity = 0.6f; doc.effect.playing = true;
    SceneDocument back;
    CHECK(FromJson(ToJson(doc), back), "effect json parses");
    CHECK(back.effect.preset == "aurora" && back.effect.seed == 4242
          && Near(back.effect.speed, 1.5f) && Near(back.effect.intensity, 0.6f)
          && back.effect.playing, "effect state round-trips");
}

static void TestPresetsAndRemix()
{
    using namespace studio;

    CHECK(PresetList().size() == 6, "six presets registered");
    for(const PresetInfo& p : PresetList())
    {
        const auto layers = BuildPreset(p.id, 1);
        CHECK(!layers.empty(), "preset builds layers");
    }
    CHECK(BuildPreset("nope", 0).empty(), "unknown preset -> empty");

    /* remix reproducibility: same seed = same params, new seed varies */
    const auto a1 = BuildPreset("aurora", 42);
    const auto a2 = BuildPreset("aurora", 42);
    const auto b  = BuildPreset("aurora", 43);
    CHECK(a1.size() == a2.size(), "remix same layer count");
    bool same = true, varies = false;
    for(size_t i = 0; i < a1.size() && i < a2.size(); i++)
    {
        same &= a1[i].speed == a2[i].speed && a1[i].scale == a2[i].scale
             && a1[i].phase == a2[i].phase && a1[i].seed == a2[i].seed;
    }
    for(size_t i = 0; i < a1.size() && i < b.size(); i++)
    {
        varies |= a1[i].speed != b[i].speed || a1[i].scale != b[i].scale
               || a1[i].phase != b[i].phase || a1[i].seed != b[i].seed
               || a1[i].direction.x != b[i].direction.x;
    }
    CHECK(same,   "same seed reproduces layers");
    CHECK(varies, "new seed varies params");

    /* global params scale speed + cap intensity */
    auto layers = BuildPreset("portal", 7);
    ApplyGlobalParams(layers, 2.0f, 0.5f);
    bool ok = true;
    for(const EffectLayer& L : layers)
    {
        ok &= L.opacity <= 0.5f + 1e-6f;
    }
    CHECK(ok, "intensity caps layer opacity");

    /* every preset evaluates without touching hardware */
    SceneDocument doc = BuildDefaultDesk();
    EffectEngine engine;
    FrameColors frame;
    for(const PresetInfo& p : PresetList())
    {
        engine.SetLayers(BuildPreset(p.id, 3));
        engine.Evaluate(doc, 1.25, frame);
        CHECK(!frame.empty(), "preset produces a frame");
    }
}

int main()
{
    TestTransform();
    TestRing();
    TestStripAndMatrix();
    TestMirrorSemantics();
    TestJsonRoundTrip();
    TestResolver();
    TestDefaultDesk();
    TestPaletteAndBlend();
    TestWaveSpatial();
    TestPulseCometSpin();
    TestNoiseAndMasks();
    TestEngineMirrorAndJson();
    TestPresetsAndRemix();

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}

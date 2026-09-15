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

int main()
{
    TestTransform();
    TestRing();
    TestStripAndMatrix();
    TestMirrorSemantics();
    TestJsonRoundTrip();
    TestResolver();
    TestDefaultDesk();

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}

/*---------------------------------------------------------*\
|| DefaultDesk.cpp                                           |
||                                                           |
||   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "DefaultDesk.h"
#include "EmitterLayout.h"

namespace studio
{

static SceneObject Group(const std::string& id, const std::string& label,
                         const Vec3& pos)
{
    SceneObject o;
    o.id                 = id;
    o.label              = label;
    o.kind               = ObjectKind::Group;
    o.transform.position = pos;
    return o;
}

static SceneObject Decor(const std::string& id, const std::string& label,
                         const std::string& geometry,
                         const Vec3& pos, const Vec3& rot, const Vec3& size_m)
{
    SceneObject o;
    o.id                   = id;
    o.label                = label;
    o.kind                 = ObjectKind::Decor;
    o.geometry             = geometry;
    o.transform.position   = pos;
    o.transform.rotation_deg = rot;
    o.size_m               = size_m;
    return o;
}

static SceneObject Device(const std::string& id, const std::string& label,
                          const std::string& geometry, const std::string& binding,
                          const Vec3& pos, const Vec3& rot)
{
    SceneObject o;
    o.id                   = id;
    o.label                = label;
    o.kind                 = ObjectKind::Device;
    o.geometry             = geometry;
    o.binding              = binding;
    o.transform.position   = pos;
    o.transform.rotation_deg = rot;
    o.verified             = true;
    return o;
}

static SceneObject Linked(const std::string& id, const std::string& label,
                          const std::string& geometry, const std::string& mirror_of,
                          const Vec3& pos, const Vec3& rot)
{
    SceneObject o;
    o.id                   = id;
    o.label                = label;
    o.kind                 = ObjectKind::Linked;
    o.geometry             = geometry;
    o.mirror_of            = mirror_of;
    o.transform.position   = pos;
    o.transform.rotation_deg = rot;
    o.verified             = false;   /* mirror copies own no addresses */
    return o;
}

SceneDocument BuildDefaultDesk()
{
    SceneDocument doc;
    doc.name = "Default desk";

    /*-----------------------------------------------------*\
    | Persistent bindings — identities, never list indices. |
    | Zone names match the inventory / zone imports.        |
    \*-----------------------------------------------------*/
    doc.bindings = {
        { "kbd_g512",    "G512",              "Logitech",  "", "", -1, "Keyboard",        0 },
        { "mouse_wheel", "Basilisk V3 Pro",   "Razer",     "", "", -1, "Scroll Wheel",    0 },
        { "mouse_logo",  "Basilisk V3 Pro",   "Razer",     "", "", -1, "Logo",            0 },
        { "mouse_strip", "Basilisk V3 Pro",   "Razer",     "", "", -1, "LED Strip",       0 },
        { "argb_v2_1",   "X870E AORUS ELITE", "Gigabyte",  "", "", -1, "ARGB_V2_1",       8 },
        { "argb_v2_2",   "X870E AORUS ELITE", "Gigabyte",  "", "", -1, "ARGB_V2_2",      16 },
        { "argb_v2_3",   "X870E AORUS ELITE", "Gigabyte",  "", "", -1, "ARGB_V2_3",       8 },
        { "gpu_logo",    "MASTER",            "",          "", "",  2, "Side Logo",       0 },
        { "gpu_fan_r",   "MASTER",            "",          "", "",  2, "Right Fan",       0 },
        { "gpu_fan_m",   "MASTER",            "",          "", "",  2, "Middle Fan",      0 },
        { "gpu_fan_l",   "MASTER",            "",          "", "",  2, "Left Fan",        0 },
        { "dimm_0",      "Corsair",           "Corsair",   "",
          "I2C: PawnIO SMBus PIIX4 0, address 0x19",       1, "",   0 },
        { "dimm_1",      "Corsair",           "Corsair",   "",
          "I2C: PawnIO SMBus PIIX4 0, address 0x1B",       1, "",   0 },
        { "slw_fan_0",   "UNI FAN",           "Lian Li",   "", "", -1, "Fan 1",          40 },
        { "slw_fan_1",   "UNI FAN",           "Lian Li",   "", "", -1, "Fan 2",          40 },
        { "slw_fan_2",   "UNI FAN",           "Lian Li",   "", "", -1, "Fan 3",          40 },
    };

    /*-----------------------------------------------------*\
    | Decor + groups. Groups are placement-only parents:    |
    | a group's position equals the old world anchor, so a  |
    | child's local transform is (old world - anchor).      |
    \*-----------------------------------------------------*/
    doc.objects.push_back(Decor("desk", "Desk", "desk",
        { 0.0f, -0.02f, 0.10f }, { 0, 0, 0 }, { 1.4f, 0.04f, 0.75f }));
    doc.objects.push_back(Decor("monitor", "Monitor", "monitor",
        { 0.0f, 0.32f, -0.22f }, { 0, 0, 0 }, { 0.62f, 0.36f, 0.02f }));

    /* The mouse moves as one device — body, wheel, logo, and the
       underglow strip are children of this group. */
    doc.objects.push_back(Group("mouse", "Mouse", { 0.26f, 0.02f, 0.27f }));
    {
        SceneObject body = Decor("mouse_body", "Mouse body", "mouse_body",
            { 0, 0, 0 }, { 0, 0, 0 }, { 0.066f, 0.04f, 0.117f });
        body.parent_id = "mouse";
        doc.objects.push_back(body);
    }

    /* The case and every component mounted inside it form one group
       centered on the shell. */
    doc.objects.push_back(Group("case", "Case", { 0.47f, 0.235f, -0.10f }));
    {
        SceneObject shell = Decor("case_shell", "Case shell", "case_shell",
            { 0, 0, 0 }, { 0, 0, 0 }, { 0.21f, 0.47f, 0.46f });
        shell.parent_id = "case";
        doc.objects.push_back(shell);
    }

    /*-----------------------------------------------------*\
    | Peripherals                                           |
    \*-----------------------------------------------------*/
    {
        /* G512 — emitters are rebuilt from the zone matrix map
           when the binding resolves (layout = "matrix_map").   */
        SceneObject kbd = Device("keyboard", "G512 Keyboard", "keyboard_body",
                                 "kbd_g512", { -0.06f, 0.02f, 0.26f }, { 0, 0, 0 });
        kbd.layout = "matrix_map";
        doc.objects.push_back(kbd);
        doc.object_colors["keyboard"] = MakeSceneColor(40, 60, 200);
    }
    {
        /* Mouse children — local offsets are (old world - group). */
        SceneObject wheel = Device("mouse_wheel", "Mouse wheel", "mouse_zone",
                                   "mouse_wheel", { 0, 0.025f, -0.035f }, { 0, 0, 0 });
        wheel.parent_id = "mouse";
        Emitter e; e.local_pos = { 0, 0, 0 }; e.group = "mouse_wheel"; e.address = 0;
        wheel.emitters.push_back(e);
        doc.objects.push_back(wheel);

        SceneObject logo = Device("mouse_logo", "Mouse logo", "mouse_zone",
                                  "mouse_logo", { 0, 0.025f, 0.02f }, { 0, 0, 0 });
        logo.parent_id = "mouse";
        logo.emitters.push_back(e);
        logo.emitters[0].group = "mouse_logo";
        doc.objects.push_back(logo);

        SceneObject strip = Device("mouse_strip", "Mouse underglow", "mouse_zone",
                                   "mouse_strip", { 0, -0.008f, 0.005f }, { 0, 0, 0 });
        strip.parent_id = "mouse";
        strip.emitters = layout::Ring(11, 0.028f, 90.0f, false, "mouse_strip");
        doc.objects.push_back(strip);

        doc.object_colors["mouse_wheel"] = MakeSceneColor(200, 30, 30);
        doc.object_colors["mouse_logo"]  = MakeSceneColor(200, 30, 30);
        doc.object_colors["mouse_strip"] = MakeSceneColor(200, 30, 30);
    }

    /*-----------------------------------------------------*\
    | Inside the case — all positions are case-local        |
    | (case group anchor: world {0.47, 0.235, -0.10}).      |
    \*-----------------------------------------------------*/
    const float RING_R = 0.052f;

    /* Ring emitters sit on the light-bearing face, not the
       mid-plane: a dot at local y=0 is sealed inside the opaque
       body mesh (fan half-thickness 14 mm, pump 22.5 mm — both
       exceed the 5.5 mm dot radius) and can never be seen.    */
    const float FAN_FACE_Y  = 0.016f;
    const float PUMP_FACE_Y = 0.024f;

    /* ARGB_V2_1 — four case fans on one mirrored 8-LED ring.
       Logical owner + 3 linked copies: bottom intake row x3 running
       front-to-back (back + middle sit on the PSU shroud, front one
       on the floor ahead of it) and a rear exhaust above the GPU. */
    {
        SceneObject f0 = Device("case_fans", "Case fans (x4 shared)", "fan_body",
                                "argb_v2_1", { 0.0f, -0.16f, -0.14f }, { 0, 0, 0 });
        f0.parent_id = "case";
        f0.emitters = layout::Ring(8, RING_R, 0.0f, false, "case_fans", 0, FAN_FACE_Y);
        doc.objects.push_back(f0);
        SceneObject b1 = Linked("case_fan_b1", "Case fan (mirror)", "fan_body",
                                "case_fans", { 0.0f, -0.16f, -0.01f }, { 0, 0, 0 });
        b1.parent_id = "case";
        doc.objects.push_back(b1);
        SceneObject b2 = Linked("case_fan_b2", "Case fan (mirror)", "fan_body",
                                "case_fans", { 0.0f, -0.16f, 0.12f }, { 0, 0, 0 });
        b2.parent_id = "case";
        doc.objects.push_back(b2);
        SceneObject rear = Linked("case_fan_rear", "Rear exhaust (mirror)", "fan_body",
                                  "case_fans", { 0.0f, 0.065f, -0.20f }, { 90, 0, 0 });
        rear.parent_id = "case";
        doc.objects.push_back(rear);
        doc.object_colors["case_fans"] = MakeSceneColor(0, 180, 160);
    }

    /* ARGB_V2_2 — GAMING 360 ICE: 3 mirrored radiator fans on
       addresses 0-7, pump on 8-15. */
    {
        /* Top-mounted 360 rad — the row runs front-to-back (z); an
           x row overflows the 0.21 m-wide case. 90 deg clockwise. */
        SceneObject rad = Device("rad_fans", "Radiator fans (x3 shared)", "fan_body",
                                 "argb_v2_2", { 0.0f, 0.185f, -0.14f }, { 0, -90, 0 });
        rad.parent_id = "case";
        rad.emitters = layout::Ring(8, RING_R, 0.0f, false, "rad_fans", 0, FAN_FACE_Y);
        doc.objects.push_back(rad);
        SceneObject m1 = Linked("rad_fan_m1", "Radiator fan (mirror)", "fan_body",
                                "rad_fans", { 0.0f, 0.185f, -0.01f }, { 0, -90, 0 });
        m1.parent_id = "case";
        doc.objects.push_back(m1);
        SceneObject m2 = Linked("rad_fan_m2", "Radiator fan (mirror)", "fan_body",
                                "rad_fans", { 0.0f, 0.185f, 0.12f }, { 0, -90, 0 });
        m2.parent_id = "case";
        doc.objects.push_back(m2);

        /* Pump cap faces -x like the Lian Li stack — RGB out from
           the motherboard toward the glass. */
        SceneObject pump = Device("pump", "Cooler pump", "pump_body",
                                  "argb_v2_2", { 0.06f, 0.05f, 0.02f }, { 0, 0, 90 });
        pump.parent_id = "case";
        pump.emitters = layout::Ring(8, 0.020f, 0.0f, false, "pump", 8, PUMP_FACE_Y);
        doc.objects.push_back(pump);

        doc.object_colors["rad_fans"] = MakeSceneColor(0, 140, 200);
        doc.object_colors["pump"]     = MakeSceneColor(200, 200, 210);
    }

    /* ARGB_V2_3 — single fan just above the GPU's top edge, near
       the card's front end. */
    {
        SceneObject top = Device("gpu_top_fan", "Top GPU fan", "fan_body",
                                 "argb_v2_3", { 0.0f, 0.05f, 0.10f }, { 0, 0, 0 });
        top.parent_id = "case";
        top.emitters = layout::Ring(8, RING_R, 0.0f, false, "gpu_top_fan", 0, FAN_FACE_Y);
        doc.objects.push_back(top);
        doc.object_colors["gpu_top_fan"] = MakeSceneColor(0, 180, 160);
    }

    /* 3x Lian Li SL Wireless — vertical side-intake stack on the
       right wall near the front; rz=+90 stands the ring up so its
       light face points -x into the case (toward the glass). The UNI
       FAN controller exposes one zone per fan ("Fan 1-3", 40 LEDs
       each), so each scene fan maps 1:1 onto a physical zone. */
    for(int i = 0; i < 3; i++)
    {
        const std::string id = "slw_fan_" + std::to_string(i);
        SceneObject fan = Device(id, "SL Wireless fan " + std::to_string(i + 1),
                                 "fan_body", id,
                                 { 0.08f, -0.135f + 0.125f * (float)i, 0.15f }, { 0, 0, 90 });
        fan.parent_id = "case";
        fan.emitters = layout::Ring(40, RING_R, 0.0f, false, id, 0, FAN_FACE_Y);
        doc.objects.push_back(fan);
        doc.object_colors[id] = MakeSceneColor(0, 180, 160);
    }

    /* Corsair DIMMs — two 10-LED strips. rz=+90 stands the stick up:
       long edge vertical, thin faces toward +/-z (edge-on to the
       glass), and the LED strip ends up running up the glass-facing
       edge like the Dominator light bar. Slots spread along z.
       Identical controllers, so the bindings disambiguate by I2C
       location (0x19/0x1B). Which address is which physical slot needs
       a visual flash check; swap the locations in the bindings if
       dimm_0/dimm_1 are reversed. */
    for(int i = 0; i < 2; i++)
    {
        const std::string id = "dimm_" + std::to_string(i);
        SceneObject dimm = Device(id, "RAM stick " + std::to_string(i + 1),
                                  "ram_body", id,
                                  { 0.05f, 0.09f, -0.07f + 0.035f * (float)i },
                                  { 0, 0, 90 });
        dimm.parent_id = "case";
        dimm.emitters = layout::Strip(10, 0.012f, { -0.054f, 0.03f, 0 }, id);
        doc.objects.push_back(dimm);
        doc.object_colors[id] = MakeSceneColor(220, 140, 30);
    }

    /* GPU — rot {90,90,0} turns the card's 0.30 m length onto z
       (bracket at the back, cooler extending toward the front):
       0.05 m edge to the glass, 0.13 m tall. Side logo verified;
       fan rings rendered but unverified (zero-RPM lighting
       unconfirmed — plan forbids writes). */
    {
        SceneObject gpu = Decor("gpu_body", "GPU", "gpu_body",
            { 0.0f, -0.035f, -0.03f }, { 90, 90, 0 }, { 0.30f, 0.05f, 0.13f });
        gpu.parent_id = "case";
        doc.objects.push_back(gpu);

        /* Side logo on the glass-facing (-x) edge; ry=90 lays the
           strip along the card's z length. */
        SceneObject logo = Device("gpu_logo", "GPU side logo", "gpu_logo",
                                  "gpu_logo", { -0.03f, -0.02f, -0.02f }, { 0, 90, 0 });
        logo.parent_id = "case";
        logo.emitters = layout::Strip(4, 0.02f, { -0.03f, 0, 0 }, "gpu_logo");
        doc.objects.push_back(logo);
        doc.object_colors["gpu_logo"] = MakeSceneColor(230, 230, 240);

        const char* fans[3]  = { "gpu_fan_r", "gpu_fan_m", "gpu_fan_l" };
        const char* names[3] = { "Right", "Middle", "Left" };
        for(int i = 0; i < 3; i++)
        {
            /* Fans hang just below the card's bottom edge (card
               bottom y=-0.10), spread along its z length. Emitters
               face down like the hardware. */
            SceneObject fan = Device(fans[i], std::string("GPU ") + names[i] + " fan",
                                     "fan_body", fans[i],
                                     { 0.0f, -0.115f, -0.13f + 0.10f * (float)i },
                                     { 0, 0, 0 });
            fan.parent_id = "case";
            fan.emitters = layout::Ring(8, 0.04f, 0.0f, false, fans[i], 0, -FAN_FACE_Y);
            fan.verified = false;
            doc.objects.push_back(fan);
        }
    }

    return doc;
}

} /* namespace studio */

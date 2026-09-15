/*---------------------------------------------------------*\
|| DefaultDesk.cpp                                           |
||                                                           |
||   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "DefaultDesk.h"
#include "EmitterLayout.h"

namespace studio
{

static SceneObject Decor(const std::string& id, const std::string& label,
                         const std::string& geometry,
                         const Vec3& pos, const Vec3& rot, const Vec3& scale)
{
    SceneObject o;
    o.id                   = id;
    o.label                = label;
    o.kind                 = ObjectKind::Decor;
    o.geometry             = geometry;
    o.transform.position   = pos;
    o.transform.rotation_deg = rot;
    o.transform.scale      = scale;
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
    | Decor                                                 |
    \*-----------------------------------------------------*/
    doc.objects.push_back(Decor("desk", "Desk", "desk",
        { 0.0f, -0.02f, 0.10f }, { 0, 0, 0 }, { 1.4f, 0.04f, 0.75f }));
    doc.objects.push_back(Decor("case_shell", "Case", "case_shell",
        { 0.47f, 0.235f, -0.10f }, { 0, 0, 0 }, { 0.21f, 0.47f, 0.46f }));
    doc.objects.push_back(Decor("monitor", "Monitor", "monitor",
        { 0.0f, 0.32f, -0.22f }, { 0, 0, 0 }, { 0.62f, 0.36f, 0.02f }));
    doc.objects.push_back(Decor("mouse_body", "Mouse", "mouse_body",
        { 0.26f, 0.02f, 0.27f }, { 0, 0, 0 }, { 0.066f, 0.04f, 0.117f }));

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
        SceneObject wheel = Device("mouse_wheel", "Mouse wheel", "mouse_zone",
                                   "mouse_wheel", { 0.26f, 0.045f, 0.235f }, { 0, 0, 0 });
        Emitter e; e.local_pos = { 0, 0, 0 }; e.group = "mouse_wheel"; e.address = 0;
        wheel.emitters.push_back(e);
        doc.objects.push_back(wheel);

        SceneObject logo = Device("mouse_logo", "Mouse logo", "mouse_zone",
                                  "mouse_logo", { 0.26f, 0.045f, 0.29f }, { 0, 0, 0 });
        logo.emitters.push_back(e);
        logo.emitters[0].group = "mouse_logo";
        doc.objects.push_back(logo);

        SceneObject strip = Device("mouse_strip", "Mouse underglow", "mouse_zone",
                                   "mouse_strip", { 0.26f, 0.012f, 0.275f }, { 0, 0, 0 });
        strip.emitters = layout::Ring(11, 0.028f, 90.0f, false, "mouse_strip");
        doc.objects.push_back(strip);

        doc.object_colors["mouse_wheel"] = MakeSceneColor(200, 30, 30);
        doc.object_colors["mouse_logo"]  = MakeSceneColor(200, 30, 30);
        doc.object_colors["mouse_strip"] = MakeSceneColor(200, 30, 30);
    }

    /*-----------------------------------------------------*\
    | Inside the case — motherboard area centered ~x=0.47   |
    \*-----------------------------------------------------*/
    const float RING_R = 0.052f;

    /* ARGB_V2_1 — four case fans on one mirrored 8-LED ring.
       Logical owner + 3 linked copies: bottom intake x3, rear exhaust. */
    {
        SceneObject f0 = Device("case_fans", "Case fans (x4 shared)", "fan_body",
                                "argb_v2_1", { 0.38f, 0.075f, 0.05f }, { 0, 0, 0 });
        f0.emitters = layout::Ring(8, RING_R, 0.0f, false, "case_fans");
        doc.objects.push_back(f0);
        doc.objects.push_back(Linked("case_fan_b1", "Case fan (mirror)", "fan_body",
                                     "case_fans", { 0.47f, 0.075f, 0.05f }, { 0, 0, 0 }));
        doc.objects.push_back(Linked("case_fan_b2", "Case fan (mirror)", "fan_body",
                                     "case_fans", { 0.56f, 0.075f, 0.05f }, { 0, 0, 0 }));
        doc.objects.push_back(Linked("case_fan_rear", "Rear exhaust (mirror)", "fan_body",
                                     "case_fans", { 0.47f, 0.30f, -0.29f }, { 90, 0, 0 }));
        doc.object_colors["case_fans"] = MakeSceneColor(0, 180, 160);
    }

    /* ARGB_V2_2 — GAMING 360 ICE: 3 mirrored radiator fans on
       addresses 0-7, pump on 8-15. */
    {
        SceneObject rad = Device("rad_fans", "Radiator fans (x3 shared)", "fan_body",
                                 "argb_v2_2", { 0.38f, 0.42f, -0.10f }, { 0, 0, 0 });
        rad.emitters = layout::Ring(8, RING_R, 0.0f, false, "rad_fans", 0);
        doc.objects.push_back(rad);
        doc.objects.push_back(Linked("rad_fan_m1", "Radiator fan (mirror)", "fan_body",
                                     "rad_fans", { 0.47f, 0.42f, -0.10f }, { 0, 0, 0 }));
        doc.objects.push_back(Linked("rad_fan_m2", "Radiator fan (mirror)", "fan_body",
                                     "rad_fans", { 0.56f, 0.42f, -0.10f }, { 0, 0, 0 }));

        SceneObject pump = Device("pump", "Cooler pump", "pump_body",
                                  "argb_v2_2", { 0.47f, 0.28f, -0.08f }, { 0, 0, 0 });
        pump.emitters = layout::Ring(8, 0.020f, 0.0f, false, "pump", 8);
        doc.objects.push_back(pump);

        doc.object_colors["rad_fans"] = MakeSceneColor(0, 140, 200);
        doc.object_colors["pump"]     = MakeSceneColor(200, 200, 210);
    }

    /* ARGB_V2_3 — single top-GPU fan. */
    {
        SceneObject top = Device("gpu_top_fan", "Top GPU fan", "fan_body",
                                 "argb_v2_3", { 0.47f, 0.24f, -0.05f }, { 0, 0, 0 });
        top.emitters = layout::Ring(8, RING_R, 0.0f, false, "gpu_top_fan");
        doc.objects.push_back(top);
        doc.object_colors["gpu_top_fan"] = MakeSceneColor(0, 180, 160);
    }

    /* 3x Lian Li SL Wireless — side intake stack. The UNI FAN
       controller exposes one zone per fan ("Fan 1-3", 40 LEDs each),
       so each scene fan maps 1:1 onto a physical zone. */
    for(int i = 0; i < 3; i++)
    {
        const std::string id = "slw_fan_" + std::to_string(i);
        SceneObject fan = Device(id, "SL Wireless fan " + std::to_string(i + 1),
                                 "fan_body", id,
                                 { 0.40f + 0.09f * (float)i, 0.14f, 0.05f }, { 0, 0, 0 });
        fan.emitters = layout::Ring(40, RING_R, 0.0f, false, id);
        doc.objects.push_back(fan);
        doc.object_colors[id] = MakeSceneColor(0, 180, 160);
    }

    /* Corsair DIMMs — two 10-LED strips. Identical controllers, so the
       bindings disambiguate by I2C location (0x19/0x1B). Which address
       is which physical slot needs a visual flash check; swap the
       locations in the bindings if dimm_0/dimm_1 are reversed. */
    for(int i = 0; i < 2; i++)
    {
        const std::string id = "dimm_" + std::to_string(i);
        SceneObject dimm = Device(id, "RAM stick " + std::to_string(i + 1),
                                  "ram_body", id,
                                  { 0.42f + 0.035f * (float)i, 0.33f, -0.14f },
                                  { 0, 0, 0 });
        dimm.emitters = layout::Strip(10, 0.012f, { -0.054f, 0.03f, 0 }, id);
        doc.objects.push_back(dimm);
        doc.object_colors[id] = MakeSceneColor(220, 140, 30);
    }

    /* GPU — side logo verified; fan rings rendered but unverified
       (zero-RPM lighting unconfirmed — plan forbids writes). */
    {
        doc.objects.push_back(Decor("gpu_body", "GPU", "gpu_body",
            { 0.47f, 0.20f, -0.13f }, { 0, 0, 0 }, { 0.30f, 0.05f, 0.13f }));

        SceneObject logo = Device("gpu_logo", "GPU side logo", "gpu_logo",
                                  "gpu_logo", { 0.47f, 0.21f, -0.062f }, { 0, 0, 0 });
        logo.emitters = layout::Strip(4, 0.02f, { -0.03f, 0, 0 }, "gpu_logo");
        doc.objects.push_back(logo);
        doc.object_colors["gpu_logo"] = MakeSceneColor(230, 230, 240);

        const char* fans[3]  = { "gpu_fan_r", "gpu_fan_m", "gpu_fan_l" };
        const char* names[3] = { "Right", "Middle", "Left" };
        for(int i = 0; i < 3; i++)
        {
            SceneObject fan = Device(fans[i], std::string("GPU ") + names[i] + " fan",
                                     "fan_body", fans[i],
                                     { 0.40f + 0.07f * (float)i, 0.185f, -0.13f },
                                     { 0, 0, 0 });
            fan.emitters = layout::Ring(8, 0.04f, 0.0f, false, fans[i]);
            fan.verified = false;
            doc.objects.push_back(fan);
        }
    }

    return doc;
}

} /* namespace studio */

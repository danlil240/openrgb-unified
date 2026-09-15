/*---------------------------------------------------------*\
||| Presets.cpp                                               |
|||                                                           |
|||   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "Presets.h"

#include <cmath>

namespace studio
{

/*---------------------------------------------------------*\
||| RemixRng — splitmix32                                   |
\*---------------------------------------------------------*/
unsigned int RemixRng::Next()
{
    state += 0x9E3779B9u;
    unsigned int z = state;
    z = (z ^ (z >> 16)) * 0x85EBCA6Bu;
    z = (z ^ (z >> 13)) * 0xC2B2AE35u;
    return z ^ (z >> 16);
}

float RemixRng::Next01() { return (float)(Next() >> 8) / (float)0x1000000; }
float RemixRng::Range(float lo, float hi) { return lo + (hi - lo) * Next01(); }
float RemixRng::Angle() { return Range(0.0f, 6.2831853f); }

Vec3 RotateYaw(const Vec3& dir, float radians)
{
    const float c = std::cos(radians), s = std::sin(radians);
    return { dir.x * c + dir.z * s, dir.y, -dir.x * s + dir.z * c };
}

/*---------------------------------------------------------*\
||| Layer helpers                                           |
\*---------------------------------------------------------*/
static EffectLayer Base(SceneColor c, float opacity = 1.0f)
{
    EffectLayer L;
    L.primitive = "static";
    L.palette   = MakePalette({ c });
    L.opacity   = opacity;
    return L;
}

static EffectLayer& WithTargets(EffectLayer& L, std::initializer_list<const char*> targets)
{
    for(const char* t : targets)
    {
        L.targets.emplace_back(t);
    }
    return L;
}

/*---------------------------------------------------------*\
||| The six presets                                         |
|||                                                           |
|||   Desk layout (meters, Y up): keyboard ~(-0.06, 0.02,    |
|||   0.26), mouse ~(0.26, 0.02, 0.27), case interior        |
|||   centered ~(0.47, 0.24, -0.10).                        |
\*---------------------------------------------------------*/
static std::vector<EffectLayer> Aurora(unsigned int seed)
{
    RemixRng rng(seed);

    /* Flowing ribbons sweeping the desk toward the case. */
    EffectLayer wave;
    wave.primitive = "wave";
    wave.direction = RotateYaw(Normalize({ 0.55f, 0.0f, -0.6f }), rng.Range(-0.4f, 0.4f));
    wave.scale     = rng.Range(0.35f, 0.6f);         /* wavelength (m)   */
    wave.speed     = rng.Range(0.15f, 0.3f);         /* m/s along dir    */
    wave.density   = rng.Range(1.2f, 2.2f);          /* ribbon thinness  */
    wave.phase     = rng.Next01();
    wave.opacity   = 0.95f;
    wave.palette   = MakePalette({
        { 0.00f, ToColorF(MakeSceneColor(0,   200, 180)) },  /* teal    */
        { 0.35f, ToColorF(MakeSceneColor(112, 64,  224)) },  /* violet  */
        { 0.65f, ToColorF(MakeSceneColor(8,   24,  56))  },  /* deep    */
        { 0.85f, ToColorF(MakeSceneColor(0,   90,  110)) },
    });

    /* Sparse white glints drifting across the ribbons. */
    EffectLayer glints;
    glints.primitive = "noise";
    glints.blend     = BlendMode::Screen;
    glints.scale     = 7.0f;
    glints.direction = { 0.12f, 0.05f, 0.08f };
    glints.speed     = 1.0f;
    glints.density   = 0.86f;
    glints.seed      = rng.Next();
    glints.opacity   = 0.5f;
    glints.palette   = MakePalette({ MakeSceneColor(235, 245, 255) });

    return { Base(MakeSceneColor(6, 12, 26)), wave, glints };
}

static std::vector<EffectLayer> Reactor(unsigned int seed)
{
    RemixRng rng(seed);

    /* Rotating spokes on every fan ring + the pump. */
    EffectLayer fans;
    fans.primitive = "spin";
    fans.space     = CoordSpace::Local;
    fans.scale     = rng.Next01() < 0.5f ? 2.0f : 3.0f;   /* spokes      */
    fans.speed     = rng.Range(0.4f, 0.8f);               /* rev/s       */
    fans.opacity   = 1.0f;
    fans.palette   = MakePalette({
        { 0.00f, ToColorF(MakeSceneColor(48, 224, 255)) },
        { 0.35f, ToColorF(MakeSceneColor(6,  32,  40))  },
        { 0.80f, ToColorF(MakeSceneColor(10, 60,  70))  },
    });
    /* Targets resolve against resolved-scene names: object ids are
       namespaced (<instance>/<entity>), geometry tags come from the
       type files, and emitter groups equal their object id. The
       mouse's three zones share geometry "mouse_zone" — only the
       resolved strip id picks out the under-glow alone. */
    WithTargets(fans, { "fan_body", "mouse/strip" });

    EffectLayer pump;
    pump.primitive = "spin";
    pump.space     = CoordSpace::Local;
    pump.scale     = 2.0f;
    pump.speed     = -rng.Range(0.3f, 0.55f);             /* counter-rotates */
    pump.palette   = MakePalette({
        { 0.00f, ToColorF(MakeSceneColor(255, 208, 64)) },
        { 0.40f, ToColorF(MakeSceneColor(64,  32,  0))  },
        { 0.85f, ToColorF(MakeSceneColor(96,  48,  8))  },
    });
    WithTargets(pump, { "pump_body" });

    /* Energy climbing the DIMM strips. */
    EffectLayer dimms;
    dimms.primitive = "wave";
    dimms.space     = CoordSpace::Local;
    dimms.direction = { 1.0f, 0.0f, 0.0f };
    dimms.scale     = 0.10f;
    dimms.speed     = rng.Range(0.06f, 0.12f);
    dimms.density   = 0.0f;                               /* full sweep  */
    dimms.palette   = MakePalette({
        { 0.00f, ToColorF(MakeSceneColor(255, 96,  16))  },
        { 0.45f, ToColorF(MakeSceneColor(255, 208, 64))  },
        { 0.70f, ToColorF(MakeSceneColor(255, 64,  0))   },
        { 0.90f, ToColorF(MakeSceneColor(64,  16,  0))   },
    });
    WithTargets(dimms, { "ram_body" });

    /* Slow synchronized pulse across the keyboard. */
    EffectLayer keys;
    keys.primitive = "wave";
    keys.direction = RotateYaw({ 1.0f, 0.0f, 0.0f }, rng.Range(-0.3f, 0.3f));
    keys.scale     = 0.25f;
    keys.speed     = rng.Range(0.2f, 0.4f);
    keys.density   = 1.4f;
    keys.palette   = MakePalette({
        { 0.00f, ToColorF(MakeSceneColor(0, 224, 192)) },
        { 0.60f, ToColorF(MakeSceneColor(0, 48,  48))  },
    });
    WithTargets(keys, { "keyboard_body" });

    return { Base(MakeSceneColor(10, 8, 6)), fans, pump, dimms, keys };
}

static std::vector<EffectLayer> Comet(unsigned int seed)
{
    RemixRng rng(seed);

    /* Closed desk loop: keyboard -> mouse -> case front -> intake
       stack -> DIMMs -> radiator top -> rear exhaust -> back. */
    EffectLayer comet;
    comet.primitive = "comet";
    comet.path = {
        { -0.06f, 0.05f, 0.26f },   /* keyboard            */
        {  0.26f, 0.05f, 0.27f },   /* mouse               */
        {  0.40f, 0.10f, 0.08f },   /* case front-bottom   */
        {  0.47f, 0.16f, 0.05f },   /* side intake stack   */
        {  0.44f, 0.34f, -0.14f },  /* DIMMs               */
        {  0.47f, 0.43f, -0.10f },  /* radiator top        */
        {  0.47f, 0.30f, -0.29f },  /* rear exhaust        */
        {  0.20f, 0.05f, -0.05f },  /* open desk, left leg */
    };
    comet.speed   = rng.Range(0.35f, 0.65f);              /* m/s along path */
    comet.scale   = rng.Range(0.25f, 0.45f);              /* tail length (m) */
    comet.phase   = rng.Next01();
    comet.opacity = 1.0f;
    comet.palette = MakePalette({
        { 0.00f, ToColorF(MakeSceneColor(255, 255, 255)) },  /* head  */
        { 0.25f, ToColorF(MakeSceneColor(64,  200, 255)) },
        { 0.70f, ToColorF(MakeSceneColor(16,  48,  96))  },
        { 1.00f, ToColorF(MakeSceneColor(0,   8,   16))  },  /* tail  */
    });

    return { Base(MakeSceneColor(4, 6, 12)), comet };
}

static std::vector<EffectLayer> LiquidChrome(unsigned int seed)
{
    RemixRng rng(seed);

    EffectLayer grad;
    grad.primitive = "gradient";
    grad.direction = RotateYaw(Normalize({ 1.0f, 0.0f, 0.25f }), rng.Range(-0.5f, 0.5f));
    grad.scale     = rng.Range(0.6f, 1.0f);               /* cycles per m    */
    grad.speed     = rng.Range(0.02f, 0.05f);             /* slow drift      */
    grad.phase     = rng.Next01();
    grad.opacity   = 0.9f;
    grad.palette   = MakePalette({
        { 0.00f, ToColorF(MakeSceneColor(240, 240, 246)) },
        { 0.30f, ToColorF(MakeSceneColor(160, 176, 200)) },
        { 0.55f, ToColorF(MakeSceneColor(232, 236, 244)) },
        { 0.80f, ToColorF(MakeSceneColor(112, 128, 160)) },
    });

    EffectLayer sheen;
    sheen.primitive = "noise";
    sheen.blend     = BlendMode::Screen;
    sheen.scale     = 9.0f;
    sheen.direction = { 0.06f, 0.02f, 0.10f };
    sheen.speed     = 0.6f;
    sheen.density   = 0.88f;
    sheen.seed      = rng.Next();
    sheen.opacity   = 0.45f;
    sheen.palette   = MakePalette({ MakeSceneColor(255, 255, 255) });

    return { grad, sheen };
}

static std::vector<EffectLayer> Portal(unsigned int seed)
{
    RemixRng rng(seed);

    /* Rings expanding from inside the case over the desk. */
    EffectLayer pulse;
    pulse.primitive = "pulse";
    pulse.origin    = { 0.47f, 0.22f, -0.10f };
    pulse.scale     = rng.Range(0.18f, 0.3f);             /* ring spacing (m) */
    pulse.speed     = rng.Range(0.2f, 0.4f);              /* m/s outward      */
    pulse.density   = rng.Range(1.8f, 3.0f);              /* ring sharpness   */
    pulse.phase     = rng.Next01();
    pulse.opacity   = 1.0f;
    pulse.palette   = MakePalette({
        { 0.00f, ToColorF(MakeSceneColor(64, 240, 224)) },
        { 0.50f, ToColorF(MakeSceneColor(128, 64, 255)) },
    });

    return { Base(MakeSceneColor(5, 10, 18)), pulse };
}

static std::vector<EffectLayer> Embers(unsigned int seed)
{
    RemixRng rng(seed);

    EffectLayer bed;
    bed.primitive = "noise";
    bed.scale     = rng.Range(3.0f, 5.0f);
    bed.direction = { 0.05f, 0.30f, 0.02f };              /* wind drifts up   */
    bed.speed     = rng.Range(0.5f, 0.9f);
    bed.density   = rng.Range(0.35f, 0.55f);
    bed.seed      = rng.Next();
    bed.opacity   = 0.95f;
    bed.palette   = MakePalette({
        { 0.00f, ToColorF(MakeSceneColor(32,  4,   0))   },
        { 0.45f, ToColorF(MakeSceneColor(140, 32,  0))   },
        { 0.75f, ToColorF(MakeSceneColor(255, 96,  16))  },
        { 0.95f, ToColorF(MakeSceneColor(255, 176, 48))  },
    });

    EffectLayer sparks;
    sparks.primitive = "noise";
    sparks.blend     = BlendMode::Screen;
    sparks.scale     = 9.0f;
    sparks.direction = { 0.02f, 0.60f, 0.05f };
    sparks.speed     = rng.Range(0.8f, 1.4f);
    sparks.density   = rng.Range(0.88f, 0.94f);
    sparks.seed      = rng.Next();
    sparks.opacity   = 0.85f;
    sparks.palette   = MakePalette({
        { 0.00f, ToColorF(MakeSceneColor(255, 208, 64))  },
        { 0.60f, ToColorF(MakeSceneColor(255, 240, 160)) },
    });

    return { bed, sparks };
}

/*---------------------------------------------------------*\
||| Stage 3 — reactive presets                              |
|||                                                           |
|||   Ripple params: speed = ring expansion m/s, scale =   |
|||   band half-width (m), density = age decay (1/s),       |
|||   origin = spawn point for position-less events.       |
\*---------------------------------------------------------*/
static std::vector<EffectLayer> BassShockwave(unsigned int seed)
{
    RemixRng rng(seed);

    /* Audio onsets launch expanding rings from inside the case. */
    EffectLayer ring;
    ring.primitive = "ripple";
    ring.source    = "audio";
    ring.origin    = { 0.47f, 0.22f, -0.10f };        /* case interior  */
    ring.speed     = rng.Range(0.55f, 0.85f);         /* m/s outward    */
    ring.scale     = rng.Range(0.05f, 0.09f);         /* band half-width */
    ring.density   = rng.Range(1.6f, 2.4f);           /* decay 1/s      */
    ring.opacity   = 1.0f;
    ring.palette   = MakePalette({
        { 0.00f, ToColorF(MakeSceneColor(255, 240, 200)) },  /* hot hit */
        { 0.35f, ToColorF(MakeSceneColor(255, 90,  60))  },
        { 0.70f, ToColorF(MakeSceneColor(160, 32,  160)) },
        { 1.00f, ToColorF(MakeSceneColor(24,  8,   48))  },  /* cooled  */
    });

    /* Loudness-following teal wash so sustained bass still breathes
       between onsets. */
    EffectLayer glow;
    glow.primitive = "level";
    glow.blend     = BlendMode::Screen;
    glow.opacity   = 0.5f;
    glow.palette   = MakePalette({
        { 0.0f,  ToColorF(MakeSceneColor(0,   30,  40))  },
        { 1.0f,  ToColorF(MakeSceneColor(40,  220, 200)) },
    });

    return { Base(MakeSceneColor(8, 6, 14)), ring, glow };
}

static std::vector<EffectLayer> KeyRipple(unsigned int seed)
{
    RemixRng rng(seed);

    /* Key presses spawn rings at the pressed key; position-less
       fallback lands at the keyboard center. */
    EffectLayer ring;
    ring.primitive = "ripple";
    ring.source    = "key";
    ring.origin    = { -0.06f, 0.04f, 0.26f };        /* keyboard center */
    ring.speed     = rng.Range(0.45f, 0.7f);          /* m/s — reaches the case in ~1s */
    ring.scale     = rng.Range(0.03f, 0.06f);         /* band half-width */
    ring.density   = rng.Range(1.8f, 2.6f);           /* decay 1/s      */
    ring.opacity   = 1.0f;
    ring.palette   = MakePalette({
        { 0.00f, ToColorF(MakeSceneColor(220, 245, 255)) },  /* press flash */
        { 0.40f, ToColorF(MakeSceneColor(80,  160, 255)) },
        { 1.00f, ToColorF(MakeSceneColor(16,  32,  80))  },
    });

    return { Base(MakeSceneColor(6, 10, 18)), ring };
}

static std::vector<EffectLayer> ScreenAtmosphere(unsigned int seed)
{
    RemixRng rng(seed);
    (void)rng;      /* deterministic look; remix perturbs nothing */

    /* Monitor decor sits at (0.0, 0.32, -0.22) with a 0.62 m face —
       emitters sample the screen cell they sit in front of. */
    EffectLayer field;
    field.primitive = "screenfield";
    field.origin    = { 0.0f, 0.32f, -0.22f };
    field.scale     = 0.62f;                          /* screen width (m) */
    field.opacity   = 1.0f;

    /* A faint warm base keeps unlit regions readable when the
       screen goes dark. */
    return { Base(MakeSceneColor(10, 8, 12), 0.6f), field };
}

/*---------------------------------------------------------*\
||| Registry                                                |
\*---------------------------------------------------------*/
const std::vector<PresetInfo>& PresetList()
{
    static const std::vector<PresetInfo> presets = {
        { "aurora",    "Aurora",           "Teal/violet ribbons sweeping the desk",    ""       },
        { "reactor",   "Reactor",          "Spinning rings, climbing RAM, key pulses", ""       },
        { "comet",     "Comet",            "A bright head chasing a desk loop",        ""       },
        { "chrome",    "Liquid chrome",    "Slow pearlescent gradients",               ""       },
        { "portal",    "Portal",           "Waves expanding out of the case",          ""       },
        { "embers",    "Embers",           "Warm noise with rising sparks",            ""       },
        { "shockwave", "Bass shockwave",   "Audio onsets launch spatial rings",        "audio"  },
        { "keyripple", "Key ripple",       "Key presses ripple out to the desk",       "key"    },
        { "ambient",   "Screen atmosphere","Screen colors wash over the setup",        "screen" },
    };
    return presets;
}

const PresetInfo* FindPreset(const std::string& id)
{
    for(const PresetInfo& p : PresetList())
    {
        if(p.id == id)
        {
            return &p;
        }
    }
    return nullptr;
}

std::vector<EffectLayer> BuildPreset(const std::string& id, unsigned int seed)
{
    if(id == "aurora")    return Aurora(seed);
    if(id == "reactor")   return Reactor(seed);
    if(id == "comet")     return Comet(seed);
    if(id == "chrome")    return LiquidChrome(seed);
    if(id == "portal")    return Portal(seed);
    if(id == "embers")    return Embers(seed);
    if(id == "shockwave") return BassShockwave(seed);
    if(id == "keyripple") return KeyRipple(seed);
    if(id == "ambient")   return ScreenAtmosphere(seed);
    return {};
}

void ApplyGlobalParams(std::vector<EffectLayer>& layers, float speed, float intensity)
{
    for(EffectLayer& L : layers)
    {
        L.speed   *= speed;
        L.opacity *= intensity;
        if(L.opacity > 1.0f) L.opacity = 1.0f;
        if(L.opacity < 0.0f) L.opacity = 0.0f;
    }
}

} /* namespace studio */

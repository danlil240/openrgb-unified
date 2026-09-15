/*---------------------------------------------------------*\
||| EffectTypes.h                                             |
|||                                                           |
|||   Desktop Lighting Studio effect model — Qt-free core.   |
|||   An EffectLayer is a parametric color field evaluated   |
|||   at emitter positions; layers composite in order onto   |
|||   the emitter's painted base color. Deterministic: same  |
|||   (doc, t, params) always gives the same frame.          |
|||                                                           |
|||   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include "../scene/SceneTypes.h"

#include <string>
#include <vector>

namespace studio
{

/*---------------------------------------------------------*\
||| ColorF — 0..1 float working space for compositing.     |
||| SceneColor (packed 0x00BBGGRR) converts at the edges.  |
\*---------------------------------------------------------*/
struct ColorF
{
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;         /* coverage: how much this result owns the pixel */
};

ColorF    ToColorF(SceneColor c);
SceneColor ToSceneColor(const ColorF& c);

enum class BlendMode
{
    Replace,    /* lerp toward the layer color by coverage            */
    Add,        /* dst + src * coverage                               */
    Screen,     /* 1 - (1-dst)(1-src*coverage) — soft additive light  */
};

enum class CoordSpace
{
    World,      /* shared desk frame — effects cross devices          */
    Local,      /* object-local frame — per-fan/per-keyboard fields   */
};

/*---------------------------------------------------------*\
||| Palette — interpolated color stops. Sample(u) wraps;   |
||| the last stop blends back into the first so cyclic      |
||| effects (spin, wave) stay seamless.                    |
\*---------------------------------------------------------*/
struct PaletteStop
{
    float     pos = 0.0f;   /* 0..1 */
    ColorF    color;
};

struct Palette
{
    std::vector<PaletteStop> stops;
    ColorF Sample(float u) const;   /* u wraps via frac */
};

Palette MakePalette(std::initializer_list<SceneColor> colors); /* even stops */
Palette MakePalette(std::initializer_list<PaletteStop> stops);

/*---------------------------------------------------------*\
||| EffectLayer — one field + how it composites.           |
|||                                                         |
|||   primitive — "static" | "gradient" | "wave" | "pulse" ||
|||               "comet" | "noise" | "spin"               |
|||   space     — World (shared desk) or Local (per object)||
|||   speed     — primitive motion rate (m/s, rev/s, ...)  |
|||   scale     — wavelength / ring spacing / noise freq / ||
|||               spoke count / comet tail length          |
|||   phase     — 0..1 starting offset                     |
|||   density   — band sharpness / noise contrast          |
|||   origin    — pulse/comet/spin center (world or local) ||
|||   direction — wave/gradient axis; noise wind           |
|||   path      — comet waypoints (polyline, closed auto)  |
|||   targets   — object ids, geometry tags, emitter       |
|||               groups; empty = every device object      |
|||   seed      — per-layer deterministic variation        |
\*---------------------------------------------------------*/
struct EffectLayer
{
    std::string              primitive = "static";
    CoordSpace               space     = CoordSpace::World;
    BlendMode                blend     = BlendMode::Replace;
    float                    opacity   = 1.0f;
    float                    speed     = 1.0f;
    float                    scale     = 0.5f;
    float                    phase     = 0.0f;
    float                    density   = 1.0f;
    Vec3                     origin;
    Vec3                     direction { 1.0f, 0.0f, 0.0f };
    std::vector<Vec3>        path;
    Palette                  palette;
    std::vector<std::string> targets;
    unsigned int             seed      = 0;
};

/*---------------------------------------------------------*\
||| EvalInput — everything a primitive sees for one        |
||| emitter sample.                                        |
\*---------------------------------------------------------*/
struct EvalInput
{
    Vec3               world;       /* emitter world position (m)      */
    Vec3               local;       /* emitter object-local position   */
    const SceneObject* object;
    const Emitter*     emitter;
    double             t;           /* seconds, bridge-owned clock     */
};

/* Does this layer touch this emitter? Empty targets = all.
   Matches object id, object geometry tag, or emitter group. */
bool   LayerMatches(const EffectLayer& layer, const SceneObject& obj,
                    const Emitter& emitter);

/* Evaluate one primitive — returns color + coverage (a). */
ColorF EvalPrimitive(const EffectLayer& layer, const EvalInput& in);

/* Composite src over dst with src.a already including opacity. */
ColorF BlendOver(ColorF dst, ColorF src, BlendMode mode);

/* Deterministic helpers (integer hash — platform-stable). */
unsigned int HashU32(unsigned int x);
float          Noise3(const Vec3& p, unsigned int seed);   /* 0..1 value noise */
Vec3           Normalize(const Vec3& v);
float          Dot(const Vec3& a, const Vec3& b);
float          Length(const Vec3& v);

} /* namespace studio */

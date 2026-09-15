/*---------------------------------------------------------*\
||| EffectTypes.cpp                                           |
|||                                                           |
|||   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "EffectTypes.h"

#include <cmath>

namespace studio
{

static constexpr float PI  = 3.14159265358979323846f;
static constexpr float TAU = 6.28318530717958647692f;

static float Frac(float v) { return v - std::floor(v); }
static float Clamp01(float v) { return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; }

/*---------------------------------------------------------*\
||| Color conversion                                        |
\*---------------------------------------------------------*/
ColorF ToColorF(SceneColor c)
{
    ColorF out;
    out.r = (float)(c & 0xFF)         / 255.0f;
    out.g = (float)((c >> 8) & 0xFF)  / 255.0f;
    out.b = (float)((c >> 16) & 0xFF) / 255.0f;
    out.a = 1.0f;
    return out;
}

SceneColor ToSceneColor(const ColorF& c)
{
    const unsigned int r = (unsigned int)(Clamp01(c.r) * 255.0f + 0.5f);
    const unsigned int g = (unsigned int)(Clamp01(c.g) * 255.0f + 0.5f);
    const unsigned int b = (unsigned int)(Clamp01(c.b) * 255.0f + 0.5f);
    return (b << 16) | (g << 8) | r;
}

/*---------------------------------------------------------*\
||| Palette                                                 |
\*---------------------------------------------------------*/
Palette MakePalette(std::initializer_list<SceneColor> colors)
{
    Palette p;
    const float n = (float)colors.size();
    float pos = 0.0f;
    for(SceneColor c : colors)
    {
        p.stops.push_back({ pos / n, ToColorF(c) });
        pos += 1.0f;
    }
    return p;
}

Palette MakePalette(std::initializer_list<PaletteStop> stops)
{
    Palette p;
    for(const PaletteStop& s : stops)
    {
        p.stops.push_back(s);
    }
    return p;
}

ColorF Palette::Sample(float u) const
{
    if(stops.empty())
    {
        return ColorF{};
    }
    if(stops.size() == 1)
    {
        return stops[0].color;
    }

    u = Frac(u);
    /* find the segment containing u; wrap from last stop to first */
    size_t i = stops.size() - 1;
    for(size_t k = 0; k < stops.size(); k++)
    {
        if(stops[k].pos <= u)
        {
            i = k;
        }
    }
    const size_t j = (i + 1) % stops.size();
    const float  lo = stops[i].pos;
    const float  hi = (j == 0) ? 1.0f + stops[0].pos : stops[j].pos;
    const float  uu = (u < lo) ? u + 1.0f : u;
    const float  f  = (hi > lo) ? (uu - lo) / (hi - lo) : 0.0f;

    ColorF out;
    out.r = stops[i].color.r + (stops[j].color.r - stops[i].color.r) * f;
    out.g = stops[i].color.g + (stops[j].color.g - stops[i].color.g) * f;
    out.b = stops[i].color.b + (stops[j].color.b - stops[i].color.b) * f;
    out.a = stops[i].color.a + (stops[j].color.a - stops[i].color.a) * f;
    return out;
}

/*---------------------------------------------------------*\
||| Deterministic hash + value noise                        |
\*---------------------------------------------------------*/
unsigned int HashU32(unsigned int x)
{
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CAE5Bu;
    x ^= x >> 16;
    return x;
}

static float HashFloat(int x, int y, int z, unsigned int seed)
{
    unsigned int h = seed;
    h ^= HashU32((unsigned int)x);
    h  = HashU32(h ^ (unsigned int)y);
    h  = HashU32(h ^ (unsigned int)z);
    return (float)(h & 0xFFFFFF) / (float)0x1000000;
}

static float Smooth(float v) { return v * v * (3.0f - 2.0f * v); }

float Noise3(const Vec3& p, unsigned int seed)
{
    const int x0 = (int)std::floor(p.x), y0 = (int)std::floor(p.y), z0 = (int)std::floor(p.z);
    const float fx = Smooth(p.x - (float)x0), fy = Smooth(p.y - (float)y0), fz = Smooth(p.z - (float)z0);

    float v[2][2][2];
    for(int dz = 0; dz < 2; dz++)
    {
        for(int dy = 0; dy < 2; dy++)
        {
            for(int dx = 0; dx < 2; dx++)
            {
                v[dz][dy][dx] = HashFloat(x0 + dx, y0 + dy, z0 + dz, seed);
            }
        }
    }
    float out = 0.0f;
    for(int dz = 0; dz < 2; dz++)
    {
        for(int dy = 0; dy < 2; dy++)
        {
            for(int dx = 0; dx < 2; dx++)
            {
                const float w = (dx ? fx : 1.0f - fx)
                              * (dy ? fy : 1.0f - fy)
                              * (dz ? fz : 1.0f - fz);
                out += v[dz][dy][dx] * w;
            }
        }
    }
    return out;
}

/*---------------------------------------------------------*\
||| Vec math                                                |
\*---------------------------------------------------------*/
Vec3 Normalize(const Vec3& v)
{
    const float len = Length(v);
    if(len < 1e-6f)
    {
        return { 1.0f, 0.0f, 0.0f };
    }
    return { v.x / len, v.y / len, v.z / len };
}

float Dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
float Length(const Vec3& v)             { return std::sqrt(Dot(v, v)); }

/*---------------------------------------------------------*\
||| Masking                                                 |
\*---------------------------------------------------------*/
bool LayerMatches(const EffectLayer& layer, const SceneObject& obj,
                  const Emitter& emitter)
{
    if(layer.targets.empty())
    {
        return true;
    }
    for(const std::string& t : layer.targets)
    {
        if(t == obj.id || t == obj.geometry || t == emitter.group)
        {
            return true;
        }
    }
    return false;
}

/*---------------------------------------------------------*\
||| Primitives                                              |
\*---------------------------------------------------------*/
static Vec3 SamplePos(const EffectLayer& layer, const EvalInput& in)
{
    return (layer.space == CoordSpace::Local) ? in.local : in.world;
}

static ColorF EvalWave(const EffectLayer& L, const EvalInput& in)
{
    const Vec3  p   = SamplePos(L, in);
    const Vec3  dir = Normalize(L.direction);
    const float u   = Dot(p, dir) / L.scale - (float)(in.t * L.speed) / L.scale + L.phase;
    /* band shape: density >1 thins the bands, <1 widens, 0 = full sweep */
    const float band = 0.5f - 0.5f * std::cos(TAU * u);
    ColorF out = L.palette.Sample(u);
    out.a = (L.density > 0.0f ? std::pow(band, L.density) : 1.0f) * L.opacity;
    return out;
}

static ColorF EvalPulse(const EffectLayer& L, const EvalInput& in)
{
    const Vec3  p = SamplePos(L, in);
    const Vec3  d { p.x - L.origin.x, p.y - L.origin.y, p.z - L.origin.z };
    const float u = (Length(d) - (float)(in.t * L.speed)) / L.scale + L.phase;
    const float band = 0.5f - 0.5f * std::cos(TAU * u);
    ColorF out = L.palette.Sample(u);
    out.a = std::pow(band, L.density > 0.0f ? L.density : 1.0f) * L.opacity;
    return out;
}

static ColorF EvalGradient(const EffectLayer& L, const EvalInput& in)
{
    const Vec3  p   = SamplePos(L, in);
    const Vec3  dir = Normalize(L.direction);
    const float u   = Dot(p, dir) * L.scale + (float)(in.t * L.speed) + L.phase;
    ColorF out = L.palette.Sample(u);
    out.a = L.opacity;
    return out;
}

static ColorF EvalSpin(const EffectLayer& L, const EvalInput& in)
{
    const Vec3  p = SamplePos(L, in);
    const float a = std::atan2(p.z - L.origin.z, p.x - L.origin.x);
    const float u = (a / TAU) * L.scale - (float)(in.t * L.speed) + L.phase;
    ColorF out = L.palette.Sample(u);
    out.a = L.opacity;
    return out;
}

/* Arc-length position of the point on the layer's polyline path
   nearest to p. Also returns the squared distance off the path. */
static float PathArc(const std::vector<Vec3>& path, const Vec3& p, float& total_len)
{
    total_len = 0.0f;
    const size_t n = path.size();
    if(n < 2)
    {
        return 0.0f;
    }
    std::vector<float> seg_len(n);
    for(size_t i = 0; i < n; i++)
    {
        const Vec3& a = path[i];
        const Vec3& b = path[(i + 1) % n];
        seg_len[i] = Length({ b.x - a.x, b.y - a.y, b.z - a.z });
        total_len += seg_len[i];
    }
    if(total_len < 1e-6f)
    {
        total_len = 0.0f;
        return 0.0f;
    }

    float best_s = 0.0f, best_d2 = 1e30f, acc = 0.0f;
    for(size_t i = 0; i < n; i++)
    {
        const Vec3& a = path[i];
        const Vec3  ab { path[(i + 1) % n].x - a.x, path[(i + 1) % n].y - a.y,
                         path[(i + 1) % n].z - a.z };
        const float l2 = seg_len[i] * seg_len[i];
        float f = 0.0f;
        if(l2 > 1e-12f)
        {
            f = Clamp01(Dot({ p.x - a.x, p.y - a.y, p.z - a.z }, ab) / l2);
        }
        const Vec3 q { a.x + ab.x * f, a.y + ab.y * f, a.z + ab.z * f };
        const Vec3 dq { p.x - q.x, p.y - q.y, p.z - q.z };
        const float d2 = Dot(dq, dq);
        if(d2 < best_d2)
        {
            best_d2 = d2;
            best_s  = acc + seg_len[i] * f;
        }
        acc += seg_len[i];
    }
    return best_s;
}

static ColorF EvalComet(const EffectLayer& L, const EvalInput& in)
{
    const Vec3 p = SamplePos(L, in);
    float total = 0.0f;
    const float s = PathArc(L.path, p, total);
    ColorF out;
    if(total < 1e-6f)
    {
        out.a = 0.0f;
        return out;
    }
    const float head   = std::fmod((float)(in.t * L.speed) + L.phase * total, total);
    const float behind = std::fmod(head - s + total, total);
    const float tail   = L.scale > 1e-4f ? L.scale : 0.3f;
    const float u      = behind / tail;             /* 0 = head */
    const float inten  = (u < 1.0f) ? std::exp(-4.0f * u) : 0.0f;
    out   = L.palette.Sample(u);
    out.a = inten * L.opacity;
    return out;
}

static ColorF EvalNoise(const EffectLayer& L, const EvalInput& in)
{
    const Vec3 p = SamplePos(L, in);
    const Vec3 q { p.x * L.scale + L.direction.x * (float)(in.t * L.speed),
                   p.y * L.scale + L.direction.y * (float)(in.t * L.speed),
                   p.z * L.scale + L.direction.z * (float)(in.t * L.speed) };
    const float n = Noise3(q, L.seed);
    /* density gates coverage: 0 = everywhere, ~0.9 = sparse sparks */
    const float lo  = 1.0f - 0.9f * Clamp01(L.density);
    float cov = (n - lo) / (1.0f - lo + 1e-6f);
    cov = Smooth(Clamp01(cov));
    ColorF out = L.palette.Sample(n);
    out.a = cov * L.opacity;
    return out;
}

ColorF EvalPrimitive(const EffectLayer& L, const EvalInput& in)
{
    if(L.primitive == "wave")     return EvalWave(L, in);
    if(L.primitive == "pulse")    return EvalPulse(L, in);
    if(L.primitive == "gradient") return EvalGradient(L, in);
    if(L.primitive == "spin")     return EvalSpin(L, in);
    if(L.primitive == "comet")    return EvalComet(L, in);
    if(L.primitive == "noise")    return EvalNoise(L, in);

    /* "static" and unknown primitives: flat palette[0] */
    ColorF out = L.palette.stops.empty() ? ColorF{} : L.palette.stops[0].color;
    out.a = L.opacity;
    return out;
}

/*---------------------------------------------------------*\
||| Compositing                                             |
\*---------------------------------------------------------*/
ColorF BlendOver(ColorF dst, ColorF src, BlendMode mode)
{
    const float a = Clamp01(src.a);
    switch(mode)
    {
    case BlendMode::Add:
        dst.r = Clamp01(dst.r + src.r * a);
        dst.g = Clamp01(dst.g + src.g * a);
        dst.b = Clamp01(dst.b + src.b * a);
        return dst;
    case BlendMode::Screen:
        dst.r = 1.0f - (1.0f - dst.r) * (1.0f - src.r * a);
        dst.g = 1.0f - (1.0f - dst.g) * (1.0f - src.g * a);
        dst.b = 1.0f - (1.0f - dst.b) * (1.0f - src.b * a);
        return dst;
    case BlendMode::Replace:
    default:
        dst.r += (src.r - dst.r) * a;
        dst.g += (src.g - dst.g) * a;
        dst.b += (src.b - dst.b) * a;
        return dst;
    }
}

} /* namespace studio */

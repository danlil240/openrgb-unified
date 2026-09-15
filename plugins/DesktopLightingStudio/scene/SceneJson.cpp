/*---------------------------------------------------------*\
|| SceneJson.cpp                                             |
||                                                           |
||   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "SceneJson.h"
#include "SceneGraph.h"

#include <cstdio>
#include <unordered_map>

#ifdef _MSC_VER
#pragma warning(disable: 4996)   /* sscanf with bounded %02x widths */
#endif

namespace studio
{

/* v1: flat object list; decor bodies carried their dimensions in
   transform.scale. v2: parent_id hierarchy + size_m split. */
static constexpr int SCENE_VERSION = 2;

static nlohmann::json ToJson(const Vec3& v)
{
    return nlohmann::json::object({{"x", v.x}, {"y", v.y}, {"z", v.z}});
}

static bool IsInt(const nlohmann::json& v)
{
    return v.is_number_integer() || v.is_number_unsigned();
}

static void AddErr(std::vector<std::string>& errs, const std::string& path,
                   const std::string& msg)
{
    errs.push_back(path + ": " + msg);
}

/*---------------------------------------------------------*\
|| Checked readers — record "<path>: expected ..." errors  ||
|| instead of throwing on malformed hand-edited files.     ||
\*---------------------------------------------------------*/
static std::string FieldStr(const nlohmann::json& j, const char* key,
                            const std::string& def, const std::string& path,
                            std::vector<std::string>& errs)
{
    if(!j.contains(key))
    {
        return def;
    }
    if(!j[key].is_string())
    {
        AddErr(errs, path + "." + key, "expected string");
        return def;
    }
    return j[key].get<std::string>();
}

static float FieldNum(const nlohmann::json& j, const char* key,
                      float def, const std::string& path,
                      std::vector<std::string>& errs)
{
    if(!j.contains(key))
    {
        return def;
    }
    if(!j[key].is_number())
    {
        AddErr(errs, path + "." + key, "expected number");
        return def;
    }
    return j[key].get<float>();
}

static int FieldInt(const nlohmann::json& j, const char* key,
                    int def, const std::string& path,
                    std::vector<std::string>& errs)
{
    if(!j.contains(key))
    {
        return def;
    }
    if(!IsInt(j[key]))
    {
        AddErr(errs, path + "." + key, "expected integer");
        return def;
    }
    return j[key].get<int>();
}

static bool FieldBool(const nlohmann::json& j, const char* key,
                      bool def, const std::string& path,
                      std::vector<std::string>& errs)
{
    if(!j.contains(key))
    {
        return def;
    }
    if(!j[key].is_boolean())
    {
        AddErr(errs, path + "." + key, "expected boolean");
        return def;
    }
    return j[key].get<bool>();
}

static Vec3 Vec3From(const nlohmann::json& j, const Vec3& def,
                     const std::string& path, std::vector<std::string>& errs)
{
    if(j.is_null())
    {
        return def;
    }
    if(!j.is_object())
    {
        AddErr(errs, path, "expected {x,y,z} object");
        return def;
    }
    Vec3 v = def;
    v.x = FieldNum(j, "x", def.x, path, errs);
    v.y = FieldNum(j, "y", def.y, path, errs);
    v.z = FieldNum(j, "z", def.z, path, errs);
    return v;
}

/* Colors write as "#RRGGBB"; reads accept that or the legacy packed
   0x00BBGGRR integer. */
static std::string ColorHex(SceneColor c)
{
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%02X%02X%02X",
                  c & 0xFFu, (c >> 8) & 0xFFu, (c >> 16) & 0xFFu);
    return buf;
}

static bool ParseColor(const nlohmann::json& v, SceneColor& out)
{
    if(v.is_number_unsigned() || v.is_number_integer())
    {
        out = v.get<SceneColor>();
        return true;
    }
    if(v.is_string())
    {
        const std::string s = v.get<std::string>();
        unsigned int r = 0, g = 0, b = 0;
        if(s.size() == 7 && s[0] == '#'
           && std::sscanf(s.c_str() + 1, "%02x%02x%02x", &r, &g, &b) == 3
           && r <= 0xFF && g <= 0xFF && b <= 0xFF)
        {
            out = MakeSceneColor(r, g, b);
            return true;
        }
    }
    return false;
}

static const char* KindName(ObjectKind kind)
{
    switch(kind)
    {
    case ObjectKind::Device: return "device";
    case ObjectKind::Linked: return "linked";
    case ObjectKind::Group:  return "group";
    default:                 return "decor";
    }
}

static ObjectKind KindFrom(const std::string& s, const std::string& path,
                           std::vector<std::string>& errs)
{
    if(s == "device") return ObjectKind::Device;
    if(s == "linked") return ObjectKind::Linked;
    if(s == "group")  return ObjectKind::Group;
    if(s == "decor")  return ObjectKind::Decor;
    AddErr(errs, path, "unknown kind '" + s + "'");
    return ObjectKind::Decor;
}

nlohmann::json ToJson(const SceneDocument& doc)
{
    nlohmann::json j;
    j["version"]    = doc.version;
    j["name"]       = doc.name;
    j["brightness"] = doc.brightness;

    nlohmann::json bindings = nlohmann::json::array();
    for(const DeviceBinding& b : doc.bindings)
    {
        bindings.push_back({
            {"id",              b.id},
            {"controller_name", b.controller_name},
            {"vendor",          b.vendor},
            {"serial",          b.serial},
            {"location",        b.location},
            {"device_type",     b.device_type},
            {"zone_name",       b.zone_name},
            {"zone_leds",       b.zone_leds},
        });
    }
    j["bindings"] = bindings;

    nlohmann::json objects = nlohmann::json::array();
    for(const SceneObject& o : doc.objects)
    {
        nlohmann::json emitters = nlohmann::json::array();
        for(const Emitter& e : o.emitters)
        {
            emitters.push_back({
                {"pos",     ToJson(e.local_pos)},
                {"group",   e.group},
                {"address", e.address},
            });
        }
        objects.push_back({
            {"id",        o.id},
            {"label",     o.label},
            {"kind",      KindName(o.kind)},
            {"parent_id", o.parent_id},
            {"position",  ToJson(o.transform.position)},
            {"rotation",  ToJson(o.transform.rotation_deg)},
            {"scale",     ToJson(o.transform.scale)},
            {"size_m",    ToJson(o.size_m)},
            {"binding",   o.binding},
            {"mirror_of", o.mirror_of},
            {"geometry",  o.geometry},
            {"layout",    o.layout},
            {"verified",  o.verified},
            {"visible",   o.visible},
            {"emitters",  emitters},
        });
    }
    j["objects"] = objects;

    nlohmann::json colors = nlohmann::json::object();
    for(const auto& kv : doc.object_colors)
    {
        colors[kv.first] = ColorHex(kv.second);
    }
    j["object_colors"] = colors;

    nlohmann::json ecolors = nlohmann::json::object();
    for(const auto& kv : doc.emitter_colors)
    {
        nlohmann::json inner = nlohmann::json::object();
        for(const auto& e : kv.second)
        {
            inner[std::to_string(e.first)] = ColorHex(e.second);
        }
        ecolors[kv.first] = inner;
    }
    j["emitter_colors"] = ecolors;

    j["effect"] = {
        {"preset",    doc.effect.preset},
        {"seed",      doc.effect.seed},
        {"speed",     doc.effect.speed},
        {"intensity", doc.effect.intensity},
        {"playing",   doc.effect.playing},
    };

    return j;
}

bool FromJson(const nlohmann::json& j, SceneDocument& doc,
              std::vector<std::string>* errors)
{
    std::vector<std::string> local;
    std::vector<std::string>& errs = errors ? *errors : local;

    if(!j.is_object())
    {
        AddErr(errs, "root", "expected object");
        return false;
    }
    if(!j.contains("version") || !IsInt(j["version"]))
    {
        AddErr(errs, "version", "expected integer");
        return false;
    }
    const int version = j["version"].get<int>();
    if(version < 1)
    {
        AddErr(errs, "version", "expected >= 1");
        return false;
    }
    if(version > SCENE_VERSION)
    {
        AddErr(errs, "version", std::to_string(version)
               + " is newer than supported " + std::to_string(SCENE_VERSION));
        return false;
    }

    SceneDocument out;
    out.version    = version;
    out.name       = FieldStr(j, "name", std::string(), "root", errs);
    out.brightness = FieldNum(j, "brightness", 1.0f, "root", errs);

    if(j.contains("bindings") && !j["bindings"].is_array())
    {
        AddErr(errs, "bindings", "expected array");
    }
    else
    {
        int i = 0;
        for(const nlohmann::json& jb :
                j.value("bindings", nlohmann::json::array()))
        {
            const std::string path = "bindings[" + std::to_string(i++) + "]";
            if(!jb.is_object())
            {
                AddErr(errs, path, "expected object");
                continue;
            }
            DeviceBinding b;
            b.id              = FieldStr(jb, "id", std::string(), path, errs);
            b.controller_name = FieldStr(jb, "controller_name", std::string(), path, errs);
            b.vendor          = FieldStr(jb, "vendor", std::string(), path, errs);
            b.serial          = FieldStr(jb, "serial", std::string(), path, errs);
            b.location        = FieldStr(jb, "location", std::string(), path, errs);
            b.device_type     = FieldInt(jb, "device_type", -1, path, errs);
            b.zone_name       = FieldStr(jb, "zone_name", std::string(), path, errs);
            b.zone_leds       = (unsigned int)std::max(0,
                                    FieldInt(jb, "zone_leds", 0, path, errs));
            if(b.id.empty())
            {
                AddErr(errs, path + ".id", "missing or empty");
                continue;
            }
            out.bindings.push_back(b);
        }
    }

    if(j.contains("objects") && !j["objects"].is_array())
    {
        AddErr(errs, "objects", "expected array");
    }
    else
    {
        int i = 0;
        for(const nlohmann::json& jo :
                j.value("objects", nlohmann::json::array()))
        {
            const std::string path = "objects[" + std::to_string(i++) + "]";
            if(!jo.is_object())
            {
                AddErr(errs, path, "expected object");
                continue;
            }
            SceneObject o;
            o.id                   = FieldStr(jo, "id", std::string(), path, errs);
            o.label                = FieldStr(jo, "label", o.id, path, errs);
            o.kind                 = KindFrom(FieldStr(jo, "kind", "decor",
                                                       path, errs), path + ".kind", errs);
            o.parent_id            = FieldStr(jo, "parent_id", std::string(), path, errs);
            o.transform.position   = Vec3From(jo.value("position", nlohmann::json()),
                                              Vec3{}, path + ".position", errs);
            o.transform.rotation_deg = Vec3From(jo.value("rotation", nlohmann::json()),
                                                Vec3{}, path + ".rotation", errs);
            if(jo.contains("scale"))
            {
                o.transform.scale  = Vec3From(jo["scale"], o.transform.scale,
                                              path + ".scale", errs);
            }
            if(o.transform.scale.x == 0.0f && o.transform.scale.y == 0.0f
               && o.transform.scale.z == 0.0f)
            {
                /* absent/all-zero scale tolerates to identity; validation
                   still rejects explicitly non-positive or non-finite
                   components. */
                o.transform.scale = { 1.0f, 1.0f, 1.0f };
            }
            o.size_m               = Vec3From(jo.value("size_m", nlohmann::json()),
                                              Vec3{}, path + ".size_m", errs);
            if(version < 2)
            {
                /* v1 stored body dimensions in `scale` for decor bodies
                   (devices used canonical QML sizes and emitters ignored
                   it). Migrate those dimensions to size_m; the field is
                   dimensionless now, so reset to identity. */
                if(o.kind == ObjectKind::Decor)
                {
                    o.size_m = o.transform.scale;
                }
                o.transform.scale = { 1.0f, 1.0f, 1.0f };
            }
            o.binding              = FieldStr(jo, "binding", std::string(), path, errs);
            o.mirror_of            = FieldStr(jo, "mirror_of", std::string(), path, errs);
            o.geometry             = FieldStr(jo, "geometry", std::string(), path, errs);
            o.layout               = FieldStr(jo, "layout", std::string(), path, errs);
            o.verified             = FieldBool(jo, "verified", false, path, errs);
            o.visible              = FieldBool(jo, "visible", true, path, errs);

            if(jo.contains("emitters") && !jo["emitters"].is_array())
            {
                AddErr(errs, path + ".emitters", "expected array");
            }
            else
            {
                int ei = 0;
                for(const nlohmann::json& je :
                        jo.value("emitters", nlohmann::json::array()))
                {
                    const std::string ep = path + ".emitters[" + std::to_string(ei++) + "]";
                    if(!je.is_object())
                    {
                        AddErr(errs, ep, "expected object");
                        continue;
                    }
                    Emitter e;
                    e.local_pos = Vec3From(je.value("pos", nlohmann::json()),
                                           Vec3{}, ep + ".pos", errs);
                    e.group     = FieldStr(je, "group", std::string(), ep, errs);
                    e.address   = FieldInt(je, "address", -1, ep, errs);
                    o.emitters.push_back(e);
                }
            }
            if(o.id.empty())
            {
                AddErr(errs, path + ".id", "missing or empty");
                continue;
            }
            out.objects.push_back(o);
        }
    }

    if(j.contains("object_colors"))
    {
        const nlohmann::json& colors = j["object_colors"];
        if(!colors.is_object())
        {
            AddErr(errs, "object_colors", "expected object");
        }
        else
        {
            for(auto it = colors.begin(); it != colors.end(); ++it)
            {
                SceneColor c = 0;
                if(ParseColor(it.value(), c))
                {
                    out.object_colors[it.key()] = c;
                }
                else
                {
                    AddErr(errs, "object_colors." + it.key(),
                           "expected \"#RRGGBB\" or packed integer");
                }
            }
        }
    }

    if(j.contains("emitter_colors"))
    {
        const nlohmann::json& ecolors = j["emitter_colors"];
        if(!ecolors.is_object())
        {
            AddErr(errs, "emitter_colors", "expected object");
        }
        else
        {
            for(auto it = ecolors.begin(); it != ecolors.end(); ++it)
            {
                const std::string path = "emitter_colors." + it.key();
                if(!it.value().is_object())
                {
                    AddErr(errs, path, "expected object");
                    continue;
                }
                for(auto e = it.value().begin(); e != it.value().end(); ++e)
                {
                    int index = -1;
                    try
                    {
                        index = std::stoi(e.key());
                    }
                    catch(...)
                    {
                    }
                    if(index < 0)
                    {
                        AddErr(errs, path, "bad emitter index '" + e.key() + "'");
                        continue;
                    }
                    SceneColor c = 0;
                    if(ParseColor(e.value(), c))
                    {
                        out.emitter_colors[it.key()][index] = c;
                    }
                    else
                    {
                        AddErr(errs, path + "." + e.key(),
                               "expected \"#RRGGBB\" or packed integer");
                    }
                }
            }
        }
    }

    if(j.contains("effect"))
    {
        const nlohmann::json& effect = j["effect"];
        if(!effect.is_object())
        {
            AddErr(errs, "effect", "expected object");
        }
        else
        {
            out.effect.preset    = FieldStr(effect, "preset", std::string(), "effect", errs);
            const int seed       = FieldInt(effect, "seed", 0, "effect", errs);
            if(seed < 0)
            {
                AddErr(errs, "effect.seed", "expected unsigned integer");
            }
            else
            {
                out.effect.seed = (unsigned int)seed;
            }
            out.effect.speed     = FieldNum(effect, "speed", 1.0f, "effect", errs);
            out.effect.intensity = FieldNum(effect, "intensity", 1.0f, "effect", errs);
            out.effect.playing   = FieldBool(effect, "playing", false, "effect", errs);
        }
    }

    if(!errs.empty())
    {
        return false;
    }

    /* Structural validation — cycles, dangling parent/mirror refs,
       non-positive scale. A failed document leaves `doc` untouched. */
    if(!ValidateSceneGraph(out, errors))
    {
        return false;
    }

    out.version = SCENE_VERSION;   /* migrated to the current schema */
    doc = out;
    return true;
}

} /* namespace studio */

/*---------------------------------------------------------*\
|| SceneJson.cpp                                             |
||                                                           |
||   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "SceneJson.h"

namespace studio
{

static constexpr int SCENE_VERSION = 1;

static nlohmann::json ToJson(const Vec3& v)
{
    return nlohmann::json::object({{"x", v.x}, {"y", v.y}, {"z", v.z}});
}

static Vec3 Vec3From(const nlohmann::json& j)
{
    Vec3 v;
    if(j.is_object())
    {
        v.x = j.value("x", 0.0f);
        v.y = j.value("y", 0.0f);
        v.z = j.value("z", 0.0f);
    }
    return v;
}

static const char* KindName(ObjectKind kind)
{
    switch(kind)
    {
    case ObjectKind::Device: return "device";
    case ObjectKind::Linked: return "linked";
    default:                 return "decor";
    }
}

static ObjectKind KindFrom(const std::string& s)
{
    if(s == "device") return ObjectKind::Device;
    if(s == "linked") return ObjectKind::Linked;
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
            {"position",  ToJson(o.transform.position)},
            {"rotation",  ToJson(o.transform.rotation_deg)},
            {"scale",     ToJson(o.transform.scale)},
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
        colors[kv.first] = kv.second;
    }
    j["object_colors"] = colors;

    nlohmann::json ecolors = nlohmann::json::object();
    for(const auto& kv : doc.emitter_colors)
    {
        nlohmann::json inner = nlohmann::json::object();
        for(const auto& e : kv.second)
        {
            inner[std::to_string(e.first)] = e.second;
        }
        ecolors[kv.first] = inner;
    }
    j["emitter_colors"] = ecolors;

    return j;
}

bool FromJson(const nlohmann::json& j, SceneDocument& doc)
{
    if(!j.is_object())
    {
        return false;
    }
    const int version = j.value("version", 0);
    if(version < 1 || version > SCENE_VERSION)
    {
        return false;
    }

    SceneDocument out;
    out.version    = version;
    out.name       = j.value("name", std::string());
    out.brightness = j.value("brightness", 1.0f);

    for(const nlohmann::json& jb : j.value("bindings", nlohmann::json::array()))
    {
        DeviceBinding b;
        b.id              = jb.value("id", std::string());
        b.controller_name = jb.value("controller_name", std::string());
        b.vendor          = jb.value("vendor", std::string());
        b.serial          = jb.value("serial", std::string());
        b.location        = jb.value("location", std::string());
        b.device_type     = jb.value("device_type", -1);
        b.zone_name       = jb.value("zone_name", std::string());
        b.zone_leds       = jb.value("zone_leds", 0u);
        if(b.id.empty())
        {
            continue;
        }
        out.bindings.push_back(b);
    }

    for(const nlohmann::json& jo : j.value("objects", nlohmann::json::array()))
    {
        SceneObject o;
        o.id                   = jo.value("id", std::string());
        o.label                = jo.value("label", o.id);
        o.kind                 = KindFrom(jo.value("kind", std::string("decor")));
        o.transform.position   = Vec3From(jo.value("position", nlohmann::json::object()));
        o.transform.rotation_deg = Vec3From(jo.value("rotation", nlohmann::json::object()));
        o.transform.scale      = Vec3From(jo.value("scale", nlohmann::json::object()));
        if(o.transform.scale.x == 0.0f && o.transform.scale.y == 0.0f && o.transform.scale.z == 0.0f)
        {
            o.transform.scale = { 1.0f, 1.0f, 1.0f };
        }
        o.binding              = jo.value("binding", std::string());
        o.mirror_of            = jo.value("mirror_of", std::string());
        o.geometry             = jo.value("geometry", std::string());
        o.layout               = jo.value("layout", std::string());
        o.verified             = jo.value("verified", false);
        o.visible              = jo.value("visible", true);

        for(const nlohmann::json& je : jo.value("emitters", nlohmann::json::array()))
        {
            Emitter e;
            e.local_pos = Vec3From(je.value("pos", nlohmann::json::object()));
            e.group     = je.value("group", std::string());
            e.address   = je.value("address", -1);
            o.emitters.push_back(e);
        }
        if(o.id.empty())
        {
            continue;
        }
        out.objects.push_back(o);
    }

    const nlohmann::json colors = j.value("object_colors", nlohmann::json::object());
    if(colors.is_object())
    {
        for(auto it = colors.begin(); it != colors.end(); ++it)
        {
            if(it.value().is_number_unsigned() || it.value().is_number_integer())
            {
                out.object_colors[it.key()] = it.value().get<SceneColor>();
            }
        }
    }

    const nlohmann::json ecolors = j.value("emitter_colors", nlohmann::json::object());
    if(ecolors.is_object())
    {
        for(auto it = ecolors.begin(); it != ecolors.end(); ++it)
        {
            if(!it.value().is_object())
            {
                continue;
            }
            for(auto e = it.value().begin(); e != it.value().end(); ++e)
            {
                if(e.value().is_number_unsigned() || e.value().is_number_integer())
                {
                    try
                    {
                        out.emitter_colors[it.key()][std::stoi(e.key())] = e.value().get<SceneColor>();
                    }
                    catch(...)
                    {
                    }
                }
            }
        }
    }

    doc = out;
    return true;
}

} /* namespace studio */

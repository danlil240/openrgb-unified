/*---------------------------------------------------------*\
|| StudioConfig.cpp                                          |
||                                                           |
||   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "StudioConfig.h"
#include "../scene/SceneJson.h"
#include "../scene/JsonFields.h"
#include "../effects/Presets.h"

#include <cstdint>
#include <set>

namespace studio
{
namespace
{

/* The checked field readers (IsInt/AddErr/FieldStr/FieldBool/
   FieldNum/FieldInt/FieldI32/FieldU32) are shared in
   scene/JsonFields.h - one copy for the scene and config
   validators; this file keeps only the workspace-specific
   Section/FieldEnum helpers. */

/* If `key` exists on j it must be an object; returns the member or
   nullptr when absent/invalid. */
const nlohmann::json* Section(const nlohmann::json& j, const char* key,
                              std::vector<std::string>& errs)
{
    if(!j.contains(key))
    {
        return nullptr;
    }
    if(!j[key].is_object())
    {
        AddErr(errs, key, "expected object");
        return nullptr;
    }
    return &j[key];
}

std::string FieldEnum(const nlohmann::json& j, const char* key,
                      const std::string& def,
                      std::initializer_list<const char*> allowed,
                      const std::string& path,
                      std::vector<std::string>& errs)
{
    const std::string v = FieldStr(j, key, def, path, errs);
    if(v != def)   /* only re-check values the user actually set */
    {
        bool ok = false;
        for(const char* a : allowed)
        {
            if(v == a) { ok = true; break; }
        }
        if(!ok)
        {
            AddErr(errs, path + "." + key, "unknown value '" + v + "'");
            return def;
        }
    }
    return v;
}

} /* anonymous namespace */

nlohmann::json ToJson(const StudioDocument& doc)
{
    /* The scene keeps its own SceneJson shape minus the fields that
       live in the workspace-level sections (name, brightness,
       effect) so each value has exactly one home in the file. */
    nlohmann::json scene = ToJson(doc.scene);
    scene.erase("name");
    scene.erase("brightness");
    scene.erase("effect");

    nlohmann::json effects = {
        {"preset",    doc.scene.effect.preset},
        {"seed",      doc.scene.effect.seed},
        {"speed",     doc.scene.effect.speed},
        {"intensity", doc.scene.effect.intensity},
        {"playing",   doc.scene.effect.playing},
        /* Reserved for JSON layer definitions (milestone 5);
           retained verbatim like definitions/extensions. */
        {"layers",    doc.meta.layers.is_array() ? doc.meta.layers
                                                 : nlohmann::json::array()},
    };

    nlohmann::json j;
    j["$schema"]        = "schemas/studio.schema.json";
    j["schema_version"] = doc.schema_version;
    j["name"]           = doc.meta.name.empty() ? doc.scene.name
                                                : doc.meta.name;
    j["ui"] = {
        {"theme",          doc.meta.ui.theme},
        {"reduced_motion", doc.meta.ui.reduced_motion},
    };
    j["camera"] = {
        {"view",       doc.meta.camera.view},
        {"projection", doc.meta.camera.projection},
    };
    j["controls"] = {
        {"middle_drag",     doc.meta.controls.middle_drag},
        {"move_snap_m",     doc.meta.controls.move_snap_m},
        {"rotate_snap_deg", doc.meta.controls.rotate_snap_deg},
    };
    j["render"] = {
        {"quality", doc.meta.render.quality},
        {"bloom",   doc.meta.render.bloom},
    };
    j["inputs"] = {
        {"audio",        doc.inputs.audio},
        {"keys",         doc.inputs.keys},
        {"screen",       doc.inputs.screen},
        {"screen_index", doc.inputs.screen_index},
        {"sens_pct",     doc.inputs.sens_pct},
        {"decay_pct",    doc.inputs.decay_pct},
    };
    j["output"] = {
        {"brightness",       doc.scene.brightness},
        {"live_on_startup",  doc.meta.live_on_startup},
    };
    j["scene"]   = scene;
    j["effects"] = effects;
    j["definitions"] = doc.meta.definitions.is_object()
        ? doc.meta.definitions
        : nlohmann::json{{"devices",  nlohmann::json::array()},
                         {"effects",  nlohmann::json::array()}};
    j["extensions"]  = doc.meta.extensions.is_object()
        ? doc.meta.extensions
        : nlohmann::json::object();
    return j;
}

bool FromJson(const nlohmann::json& j, StudioDocument& doc,
              std::vector<std::string>* errors,
              std::vector<std::string>* warnings)
{
    std::vector<std::string> errs, warns;

    if(!j.is_object())
    {
        errs.push_back("root: expected object");
        if(errors) { *errors = errs; }
        return false;
    }

    /*------------------------------------------------*\
    || schema_version — newer docs are rejected        ||
    || untouched rather than silently downgraded.      ||
    \*------------------------------------------------*/
    if(!j.contains("schema_version"))
    {
        errs.push_back("schema_version: missing");
    }
    else if(!IsInt(j["schema_version"]))
    {
        errs.push_back("schema_version: expected integer");
    }
    else
    {
        const long long v = j["schema_version"].get<long long>();
        if(v < 1)
        {
            errs.push_back("schema_version: expected >= 1");
        }
        else if(v > STUDIO_SCHEMA_VERSION)
        {
            AddErr(errs, "schema_version",
                   std::to_string(v) + " is newer than supported "
                   + std::to_string(STUDIO_SCHEMA_VERSION));
        }
    }

    StudioDocument out;

    if(j.contains("name"))
    {
        if(j["name"].is_string())
        {
            out.meta.name = j["name"].get<std::string>();
        }
        else
        {
            errs.push_back("name: expected string");
        }
    }

    /*------------------------------------------------*\
    || Editor preference sections                      ||
    \*------------------------------------------------*/
    if(const nlohmann::json* s = Section(j, "ui", errs))
    {
        out.meta.ui.theme = FieldStr(*s, "theme", out.meta.ui.theme,
                                     "ui", errs);
        out.meta.ui.reduced_motion = FieldBool(*s, "reduced_motion",
                                     out.meta.ui.reduced_motion, "ui", errs);
    }
    if(const nlohmann::json* s = Section(j, "camera", errs))
    {
        out.meta.camera.view = FieldEnum(*s, "view", out.meta.camera.view,
            {"desk", "top", "front", "case", "free"}, "camera", errs);
        out.meta.camera.projection = FieldEnum(*s, "projection",
            out.meta.camera.projection,
            {"orthographic", "perspective"}, "camera", errs);
    }
    if(const nlohmann::json* s = Section(j, "controls", errs))
    {
        out.meta.controls.middle_drag = FieldEnum(*s, "middle_drag",
            out.meta.controls.middle_drag, {"pan", "orbit"}, "controls", errs);
        const double snap = FieldNum(*s, "move_snap_m",
            out.meta.controls.move_snap_m, "controls", errs);
        if(snap > 0.0)
        {
            out.meta.controls.move_snap_m = (float)snap;
        }
        else if(s->contains("move_snap_m"))
        {
            AddErr(errs, "controls.move_snap_m", "expected number > 0");
        }
        const double rot = FieldNum(*s, "rotate_snap_deg",
            out.meta.controls.rotate_snap_deg, "controls", errs);
        if(rot > 0.0)
        {
            out.meta.controls.rotate_snap_deg = (float)rot;
        }
        else if(s->contains("rotate_snap_deg"))
        {
            AddErr(errs, "controls.rotate_snap_deg", "expected number > 0");
        }
    }
    if(const nlohmann::json* s = Section(j, "render", errs))
    {
        out.meta.render.quality = FieldEnum(*s, "quality",
            out.meta.render.quality, {"low", "balanced", "high"},
            "render", errs);
        out.meta.render.bloom = FieldBool(*s, "bloom",
            out.meta.render.bloom, "render", errs);
    }

    /*------------------------------------------------*\
    || inputs                                          ||
    \*------------------------------------------------*/
    if(const nlohmann::json* s = Section(j, "inputs", errs))
    {
        out.inputs.audio  = FieldBool(*s, "audio",  out.inputs.audio,
                                      "inputs", errs);
        out.inputs.keys   = FieldBool(*s, "keys",   out.inputs.keys,
                                      "inputs", errs);
        out.inputs.screen = FieldBool(*s, "screen", out.inputs.screen,
                                      "inputs", errs);
        const long long idx = FieldInt(*s, "screen_index",
            out.inputs.screen_index, "inputs", errs);
        if(idx < 0 || idx > 2147483647ll)
        {
            AddErr(errs, "inputs.screen_index",
                   "expected integer in 0..2147483647");
        }
        else
        {
            out.inputs.screen_index = (int)idx;
        }
        const long long sens = FieldInt(*s, "sens_pct",
            out.inputs.sens_pct, "inputs", errs);
        if(sens < 25 || sens > 200)
        {
            AddErr(errs, "inputs.sens_pct", "expected integer in 25..200");
        }
        else
        {
            out.inputs.sens_pct = (int)sens;
        }
        const long long decay = FieldInt(*s, "decay_pct",
            out.inputs.decay_pct, "inputs", errs);
        if(decay < 50 || decay > 300)
        {
            AddErr(errs, "inputs.decay_pct", "expected integer in 50..300");
        }
        else
        {
            out.inputs.decay_pct = (int)decay;
        }
    }

    /*------------------------------------------------*\
    || output — brightness is hoisted here; it lands   ||
    || on the scene doc before scene validation runs.  ||
    \*------------------------------------------------*/
    bool     have_brightness = false;
    double   brightness      = 1.0;
    if(const nlohmann::json* s = Section(j, "output", errs))
    {
        if(s->contains("brightness"))
        {
            const double b = FieldNum(*s, "brightness", 1.0, "output", errs);
            if(b < 0.0 || b > 1.0)
            {
                AddErr(errs, "output.brightness", "expected number in 0..1");
            }
            else if(s->at("brightness").is_number())
            {
                brightness      = b;
                have_brightness = true;
            }
        }
        out.meta.live_on_startup = FieldBool(*s, "live_on_startup",
            out.meta.live_on_startup, "output", errs);
    }

    /*------------------------------------------------*\
    || effects — re-anchored into scene.effect so the  ||
    || scene doc stays the single in-memory owner.     ||
    \*------------------------------------------------*/
    bool          have_effect = false;
    nlohmann::json effect_j    = nlohmann::json::object();
    std::string   preset;
    if(const nlohmann::json* s = Section(j, "effects", errs))
    {
        have_effect = true;
        preset = FieldStr(*s, "preset", std::string(), "effects", errs);
        effect_j["preset"] = preset;
        /* Full-range uint32 read — the old long-long read +
           (unsigned int) cast wrapped seeds > UINT32_MAX and a
           >= 2^31 seed never survived scene validation. */
        effect_j["seed"] = FieldU32(*s, "seed", 0, "effects", errs);
        const double speed = FieldNum(*s, "speed", 1.0, "effects", errs);
        if(speed <= 0.0)
        {
            AddErr(errs, "effects.speed", "expected number > 0");
        }
        effect_j["speed"] = speed;
        const double intensity = FieldNum(*s, "intensity", 1.0,
                                          "effects", errs);
        if(intensity < 0.0 || intensity > 1.0)
        {
            AddErr(errs, "effects.intensity", "expected number in 0..1");
        }
        effect_j["intensity"] = intensity;
        effect_j["playing"] = FieldBool(*s, "playing", false,
                                        "effects", errs);
        if(s->contains("layers"))
        {
            if(!s->at("layers").is_array())
            {
                AddErr(errs, "effects.layers", "expected array");
            }
            else
            {
                /* Reserved for milestone 5 — retained verbatim like
                   definitions/extensions so a hand-authored layers
                   array is never destroyed by a save. */
                out.meta.layers = s->at("layers");
            }
        }
    }

    /*------------------------------------------------*\
    || scene — delegated to the scene validator with   ||
    || the hoisted fields re-attached.                  ||
    \*------------------------------------------------*/
    bool scene_ok = false;
    if(!j.contains("scene"))
    {
        errs.push_back("scene: missing");
    }
    else if(!j["scene"].is_object())
    {
        errs.push_back("scene: expected object");
    }
    else
    {
        nlohmann::json scene_j = j["scene"];
        if(!scene_j.contains("version"))
        {
            scene_j["version"] = 2;
        }
        if(!out.meta.name.empty())
        {
            scene_j["name"] = out.meta.name;
        }
        if(have_brightness)
        {
            scene_j["brightness"] = brightness;
        }
        if(have_effect)
        {
            scene_j["effect"] = effect_j;
        }

        std::vector<std::string> scene_errors;
        try
        {
            scene_ok = FromJson(scene_j, out.scene, &scene_errors);
        }
        catch(...)
        {
            scene_errors.push_back("unexpected parse failure");
        }
        for(const std::string& e : scene_errors)
        {
            errs.push_back("scene: " + e);
        }

        if(scene_ok)
        {
            /* Workspace-level checks the scene layer can't see:
               object -> binding references, LED addresses against
               the bound zone, and content caps. */
            if(out.scene.objects.size() > STUDIO_MAX_OBJECTS)
            {
                AddErr(errs, "scene.objects", "count exceeds cap "
                       + std::to_string(STUDIO_MAX_OBJECTS));
            }
            if(out.scene.bindings.size() > STUDIO_MAX_BINDINGS)
            {
                AddErr(errs, "scene.bindings", "count exceeds cap "
                       + std::to_string(STUDIO_MAX_BINDINGS));
            }

            std::set<std::string> binding_ids;
            for(const DeviceBinding& b : out.scene.bindings)
            {
                binding_ids.insert(b.id);
            }

            size_t total_emitters = 0;
            for(const SceneObject& o : out.scene.objects)
            {
                total_emitters += o.emitters.size();
                const std::string opath = "scene.objects[" + o.id + "]";

                const DeviceBinding* bound = nullptr;
                if(!o.binding.empty())
                {
                    const auto bit = binding_ids.find(o.binding);
                    if(bit == binding_ids.end())
                    {
                        AddErr(errs, opath + ".binding",
                               "unknown binding '" + o.binding + "'");
                    }
                    else
                    {
                        for(const DeviceBinding& b : out.scene.bindings)
                        {
                            if(b.id == o.binding) { bound = &b; break; }
                        }
                    }
                }
                for(size_t i = 0; i < o.emitters.size(); i++)
                {
                    const int a = o.emitters[i].address;
                    const std::string ep = opath + ".emitters["
                                           + std::to_string(i) + "].address";
                    if(a < -1)
                    {
                        AddErr(errs, ep, "expected -1 or >= 0");
                    }
                    else if(bound != nullptr && bound->zone_leds > 0
                            && a >= (int)bound->zone_leds)
                    {
                        AddErr(errs, ep, std::to_string(a)
                               + " out of range (zone '" + bound->zone_name
                               + "' has " + std::to_string(bound->zone_leds)
                               + " LEDs)");
                    }
                }
            }
            if(total_emitters > STUDIO_MAX_EMITTERS)
            {
                AddErr(errs, "scene.objects.emitters", "count exceeds cap "
                       + std::to_string(STUDIO_MAX_EMITTERS));
            }
        }
    }

    /*------------------------------------------------*\
    || definitions + extensions — retained verbatim    ||
    \*------------------------------------------------*/
    if(const nlohmann::json* s = Section(j, "definitions", errs))
    {
        if(s->contains("devices") && !s->at("devices").is_array())
        {
            AddErr(errs, "definitions.devices", "expected array");
        }
        if(s->contains("effects") && !s->at("effects").is_array())
        {
            AddErr(errs, "definitions.effects", "expected array");
        }
        out.meta.definitions = *s;
    }
    if(const nlohmann::json* s = Section(j, "extensions", errs))
    {
        out.meta.extensions = *s;
    }

    /*------------------------------------------------*\
    || Non-fatal notes                                 ||
    \*------------------------------------------------*/
    static const std::set<std::string> known = {
        "$schema", "schema_version", "name", "ui", "camera", "controls",
        "render", "inputs", "output", "scene", "effects", "definitions",
        "extensions",
    };
    for(auto it = j.begin(); it != j.end(); ++it)
    {
        if(known.find(it.key()) == known.end())
        {
            warns.push_back("unknown field '" + it.key()
                            + "' — possible typo");
        }
    }
    if(!out.scene.effect.preset.empty()
       && FindPreset(out.scene.effect.preset) == nullptr)
    {
        warns.push_back("effects.preset: unknown preset '"
                        + out.scene.effect.preset + "'");
    }

    if(!errs.empty())
    {
        if(errors)   { *errors   = errs;  }
        if(warnings) { *warnings = warns; }
        return false;
    }
    if(errors)   { errors->clear();   }
    if(warnings) { *warnings = warns; }
    doc = out;
    return true;
}

} /* namespace studio */

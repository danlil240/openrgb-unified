/*---------------------------------------------------------*\
|| ConfigMigration.cpp                                       |
||                                                           |
||   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "ConfigMigration.h"

namespace studio
{

bool HasLegacySettings(const nlohmann::json& legacy)
{
    return legacy.is_object() && !legacy.empty()
           && legacy.contains("scene") && legacy["scene"].is_object();
}

bool MigrateLegacySettings(const nlohmann::json& legacy,
                           StudioDocument& doc,
                           std::vector<std::string>* errors)
{
    if(!HasLegacySettings(legacy))
    {
        if(errors)
        {
            errors->push_back("legacy: no saved scene in host settings");
        }
        return false;
    }

    /* Re-express the legacy blob as a workspace document and let the
       normal validator own the rules — the migration produces a
       candidate, so a malformed legacy save can never half-apply.

       The legacy scene sub-doc keeps its own version (v1 scenes get
       the scene-level v1->v2 migration inside SceneJson::FromJson).
       name/brightness/effect hoist to the workspace sections, exactly
       like the file format. */
    nlohmann::json scene = legacy["scene"];
    nlohmann::json w;
    w["schema_version"] = STUDIO_SCHEMA_VERSION;
    w["name"] = (scene.contains("name") && scene["name"].is_string())
        ? scene["name"].get<std::string>()
        : std::string("Migrated workspace");
    scene.erase("name");

    if(scene.contains("brightness"))
    {
        w["output"]["brightness"] = scene["brightness"];
        scene.erase("brightness");
    }
    /* Migration never turns live output on. */
    w["output"]["live_on_startup"] = false;

    if(scene.contains("effect") && scene["effect"].is_object())
    {
        w["effects"] = scene["effect"];
        scene.erase("effect");
    }
    w["scene"] = scene;

    /* Input settings carry the same field names in both formats. */
    if(legacy.contains("inputs") && legacy["inputs"].is_object())
    {
        w["inputs"] = legacy["inputs"];
    }

    std::vector<std::string> errs;
    if(!FromJson(w, doc, &errs))
    {
        if(errors)
        {
            for(std::string& e : errs)
            {
                errors->push_back("legacy." + e);
            }
        }
        return false;
    }
    return true;
}

} /* namespace studio */

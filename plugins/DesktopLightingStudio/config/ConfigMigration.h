/*---------------------------------------------------------*\
|| ConfigMigration.h                                         |
||                                                           |
||   One-time migration: the pre-workspace save lived in    |
||   OpenRGB host settings as                                 |
||     { "scene": <SceneJson v1/v2>, "inputs": {...} }        |
||   That blob is a migration source only — after the move  |
||   the file store (studio.json) is exclusive.             |
||                                                           |
||   Qt-free so the fast test suite covers it.              |
||                                                           |
||   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include "StudioConfig.h"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace studio
{

/* True when the host-settings blob actually carries a saved scene
   (non-empty object with an object "scene" member). */
bool HasLegacySettings(const nlohmann::json& legacy);

/* Migrate a legacy settings blob into a workspace document
   candidate. Runs the same whole-document validation as a file
   load — on failure `doc` is untouched and `errors` describes the
   fields. Never enables live output (output.live_on_startup is
   forced false). Never throws. */
bool MigrateLegacySettings(const nlohmann::json& legacy,
                           StudioDocument& doc,
                           std::vector<std::string>* errors = nullptr);

} /* namespace studio */

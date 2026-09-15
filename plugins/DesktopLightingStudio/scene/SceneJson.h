/*---------------------------------------------------------*\
|| SceneJson.h                                               |
||                                                           |
||   SceneDocument <-> JSON. Versioned; unknown fields are  |
||   ignored so newer documents degrade instead of failing. |
||                                                           |
||   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include "SceneTypes.h"
#include <nlohmann/json.hpp>

namespace studio
{

nlohmann::json ToJson(const SceneDocument& doc);

/* Parse a document. Returns false on malformed input or a version
   newer than the current schema (no silent downgrade). */
bool FromJson(const nlohmann::json& j, SceneDocument& doc);

} /* namespace studio */

/*---------------------------------------------------------*\
||| Presets.h                                                 |
|||                                                           |
|||   The six nonreactive Stage-2 presets. Each builds a     |
|||   deterministic layer list from (id, seed); global       |
|||   speed/intensity multipliers apply on top. The seed is  |
|||   stored in the scene document, so a Remix is           |
|||   reproducible and persists with the scene.              |
|||                                                           |
|||   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include "EffectTypes.h"

namespace studio
{

struct PresetInfo
{
    std::string id;
    std::string name;
    std::string description;
};

/* Fixed preset order for the scene-card strip. */
const std::vector<PresetInfo>& PresetList();
const PresetInfo*              FindPreset(const std::string& id);

/* Build a preset's layers. Unknown id -> empty list. */
std::vector<EffectLayer> BuildPreset(const std::string& id, unsigned int seed);

/* Global user controls applied to a built layer list. */
void ApplyGlobalParams(std::vector<EffectLayer>& layers, float speed, float intensity);

/* SplitMix32 — deterministic remix stream. */
struct RemixRng
{
    unsigned int state;
    explicit RemixRng(unsigned int seed) : state(seed) {}
    unsigned int Next();
    float        Next01();                 /* [0,1)  */
    float        Range(float lo, float hi);
    float        Angle();                  /* radians, full circle */
};

/* Rotate a direction in the desk plane (around +Y). */
Vec3 RotateYaw(const Vec3& dir, float radians);

} /* namespace studio */

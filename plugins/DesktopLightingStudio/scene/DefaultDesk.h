/*---------------------------------------------------------*\
||| DefaultDesk.h                                             |
|||                                                           |
|||   Builds the default desk scene from the verified        |
|||   hardware inventory (docs/hardware-inventory.md) and    |
|||   zone imports (configs/zone-imports/). Placement is a   |
|||   reasonable guess — the plan requires user confirmation |
|||   of desk dimensions and component positions.            |
|||                                                           |
|||   Two surfaces:                                          |
|||   - BuildDefaultWorkspace() + DefaultDevicePresets()     |
|||     produce the v3 compact workspace and its packaged    |
|||     device-type files (presets/devices/*.device.json).   |
|||   - BuildDefaultDesk() keeps emitting the expanded       |
|||     SceneDocument directly — the v2 shape migration      |
|||     tests still exercise.                                |
|||                                                           |
|||   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include "SceneTypes.h"
#include "../config/StudioConfig.h"
#include "../presets/DevicePreset.h"

#include <vector>

namespace studio
{

/* Expanded v2 scene — retained for migration tests. */
SceneDocument BuildDefaultDesk();

/* Packaged device types matching the bundled presets/devices/
   *.device.json files (the test suite asserts file == struct). */
std::vector<DevicePreset> DefaultDevicePresets();

/* The v3 default desk: compact placements referencing
   DefaultDevicePresets() ids. Resolving it must produce the same
   scene BuildDefaultDesk() produces (the parity test). */
StudioDocument BuildDefaultWorkspace();

} /* namespace studio */

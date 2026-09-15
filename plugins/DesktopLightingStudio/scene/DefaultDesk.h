/*---------------------------------------------------------*\
|| DefaultDesk.h                                             |
||                                                           |
||   Builds the default desk scene from the verified        |
||   hardware inventory (docs/hardware-inventory.md) and    |
||   zone imports (configs/zone-imports/). Placement is a   |
||   reasonable guess — the plan requires user confirmation |
||   of desk dimensions and component positions.            |
||                                                           |
||   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include "SceneTypes.h"

namespace studio
{

SceneDocument BuildDefaultDesk();

} /* namespace studio */

/*---------------------------------------------------------*\
||| DevicePreset.h                                            |
|||                                                           |
|||   External device-type document (schema v1) — the      |
|||   reusable definition a workspace instance references  |
|||   through devices.<id>.type. One file per type under   |
|||   presets/devices/<id>.device.json.                    |
|||                                                           |
|||   Qt-free: parsed and validated under plain cl so      |
|||   tests/device_preset_test.cpp covers it.              |
|||                                                           |
|||   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include "../scene/SceneTypes.h"

#include <nlohmann/json.hpp>

#include <map>
#include <string>
#include <vector>

namespace studio
{

constexpr int DEVICE_PRESET_SCHEMA_VERSION = 1;

/* Content caps — preset files are user/community content. */
constexpr unsigned int PRESET_MAX_ENTITIES = 512;
constexpr unsigned int PRESET_MAX_ZONES    = 128;
constexpr unsigned int PRESET_MAX_POINTS   = 4096;

/* Preset / entity / zone / instance identifiers: a single
   namespace segment — '/' is the inst/entity separator and
   file names must stay portable. */
bool IsPresetId(const std::string& s);

/*---------------------------------------------------------*\
||| ZoneLayout — how a zone's emitters are generated.      ||
|||   ring    — radius_m, start_angle_deg, reverse,        ||
|||             face_y_m (emitter plane offset)            ||
|||   strip   — spacing_m along +X from origin             ||
|||   matrix  — dynamic (hardware zone map at runtime) or  ||
|||             a static rows/cols/map grid                ||
|||   points  — explicit [x,y,z] list; addresses optional  ||
|||             (default = instance addr_base + index)     |
\*---------------------------------------------------------*/
struct ZoneLayout
{
    std::string type;                       /* ring|strip|matrix|points */

    /* ring */
    float radius_m         = 0.0f;
    float start_angle_deg  = 0.0f;
    float face_y_m         = 0.0f;
    bool  reverse          = false;

    /* strip */
    float spacing_m        = 0.0f;
    Vec3  origin;

    /* matrix */
    bool          dynamic     = false;      /* resolved from the bound
                                               zone's matrix map */
    unsigned int  rows        = 0;
    unsigned int  cols        = 0;
    float         pitch_x_m   = 0.0f;
    float         pitch_z_m   = 0.0f;
    unsigned int  empty_cell  = 0xFFFFFFFFu;
    std::vector<unsigned int> map;

    /* points */
    std::vector<Vec3> points;
    std::vector<int>  addresses;            /* optional per-point */
};

struct DeviceZone
{
    std::string  id;
    std::string  entity;                    /* entity carrying the LEDs */
    unsigned int led_count = 0;             /* 0 = dynamic (matrix)    */
    ZoneLayout   layout;
};

/*---------------------------------------------------------*\
||| PresetEntity — one node inside the type's local graph. ||
|||   Two flavours, mutually exclusive:                    ||
|||   - local part: geometry/size_m/zone/appearance        ||
|||   - child device ref: `type` + transform — mounts      ||
|||     another preset at a local position (nested         ||
|||     assemblies); its entities are never copied.        |
\*---------------------------------------------------------*/
struct PresetEntity
{
    std::string    id;
    std::string    type;                    /* child type id; "" = part */
    std::string    geometry;
    Vec3           size_m;
    Vec3           position;                /* local, meters           */
    Vec3           rotation_deg;            /* local, XYZ degrees      */
    std::string    parent;                  /* entity id in this type  */
    std::string    zone;                    /* DeviceZone id           */
    nlohmann::json appearance;              /* retained verbatim       */
};

struct DevicePreset
{
    int                             schema_version = DEVICE_PRESET_SCHEMA_VERSION;
    std::string                     id;
    std::string                     name;
    std::string                     category;
    std::map<std::string, PresetEntity> entities;
    std::vector<DeviceZone>         zones;
};

/* Deterministic serialization (two-space pretty print via dump(2)
   at the call site). */
nlohmann::json ToJson(const DevicePreset& p);

/* Parse + validate a preset document. On failure `p` is untouched
   and `errors` holds "<path>: ..." messages. Checks: schema_version
   == 1, non-empty charset-safe id, entity field types, finite
   transforms, non-negative size_m, resolvable entity parents with
   no cycles, entity<->zone cross references, per-layout required
   parameters, content caps. Never throws. */
bool DevicePresetFromJson(const nlohmann::json& j, DevicePreset& p,
                          std::vector<std::string>* errors = nullptr);

/* File convenience wrapper (parse + validate). */
bool DevicePresetFromJsonFile(const std::string& path, DevicePreset& p,
                              std::vector<std::string>* errors = nullptr);

/* Generate a zone's emitters in entity-local space. `group` is the
   expanded object id; `addr_base` shifts generated addresses
   (explicit points.addresses win). A dynamic matrix produces no
   emitters — the bridge rebuilds them from the hardware zone map. */
std::vector<Emitter> GenerateZoneEmitters(const DeviceZone& z,
                                        const std::string& group,
                                        int addr_base);

/* Other type ids this preset references through entity `type`
   fields (dependency edges for cycle checks). */
std::vector<std::string> PresetDependencies(const DevicePreset& p);

} /* namespace studio */

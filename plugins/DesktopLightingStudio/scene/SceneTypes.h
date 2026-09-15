/*---------------------------------------------------------*\
|| SceneTypes.h                                              |
||                                                           |
||   Desktop Lighting Studio scene model — Qt-free core.     |
||   Right-handed coordinates, Y up, meters.                 |
||                                                           |
||   The scene document is the single source of truth for    |
||   object placement, emitter positions, bindings, and      |
||   static colors. QML renders from it; the output          |
||   adapter writes hardware from the same document.         |
||                                                           |
||   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include <map>
#include <string>
#include <vector>

namespace studio
{

/* Colors use OpenRGB's packed 0x00BBGGRR format so scene colors feed
   controller writes without conversion. */
typedef unsigned int SceneColor;

inline SceneColor MakeSceneColor(unsigned int r, unsigned int g, unsigned int b)
{
    return ((b & 0xFF) << 16) | ((g & 0xFF) << 8) | (r & 0xFF);
}

struct Vec3
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Transform
{
    Vec3 position;
    Vec3 rotation_deg;      /* euler XYZ, degrees                     */
    Vec3 scale { 1.0f, 1.0f, 1.0f };
};

/* world = T * Rxyz * S applied to local point */
Vec3 TransformPoint(const Transform& t, const Vec3& p);

/*---------------------------------------------------------*\
|| Emitter — one addressable LED inside a scene object.    |
||                                                         |
||   group    — logical emitter group; mirrored copies     |
||              share a group so they show identical state.|
||   address  — LED index inside the bound zone, or -1     |
||              for render-only emitters (linked copies,   |
||              unverified lights).                        |
\*---------------------------------------------------------*/
struct Emitter
{
    Vec3        local_pos;
    std::string group;
    int         address = -1;
};

enum class ObjectKind
{
    Decor,      /* render only — desk, case shell                  */
    Device,     /* bound to a real controller zone                 */
    Linked,     /* mirror copy of another object's emitter group   */
};

struct SceneObject
{
    std::string             id;
    std::string             label;
    ObjectKind              kind = ObjectKind::Decor;
    Transform               transform;
    std::string             binding;        /* DeviceBinding id          */
    std::string             mirror_of;      /* source object id (Linked) */
    std::string             geometry;       /* decor/body mesh hint      */
    std::string             layout;         /* "" or "matrix_map" —      */
                                            /* rebuild emitters from     */
                                            /* the bound zone's matrix   */
    std::vector<Emitter>    emitters;
    bool                    verified = false; /* false => never written  */
    bool                    visible  = true;
};

/*---------------------------------------------------------*\
|| DeviceBinding — persistent device identity.             |
||   Matches controllers by name/vendor/serial/location    |
||   and zones by name. NEVER a controller-list index.     |
\*---------------------------------------------------------*/
struct DeviceBinding
{
    std::string id;
    std::string controller_name;
    std::string vendor;
    std::string serial;
    std::string location;
    int         device_type = -1;       /* device_type enum, -1 = any   */
    std::string zone_name;
    unsigned int zone_leds = 0;         /* expected count, 0 = unchecked */
};

struct SceneDocument
{
    int                                 version = 1;
    std::string                         name;
    std::vector<DeviceBinding>          bindings;
    std::vector<SceneObject>            objects;
    /* object id -> base color (emitters without an override)         */
    std::map<std::string, SceneColor>   object_colors;
    /* object id -> emitter index -> color (painted overrides)        */
    std::map<std::string, std::map<int, SceneColor>> emitter_colors;
    float                               brightness = 1.0f;
};

const SceneObject* FindObject(const SceneDocument& doc, const std::string& id);
SceneObject*       FindObject(SceneDocument& doc, const std::string& id);

/* The object that owns the output addresses for id — follows
   mirror_of on Linked objects. Returns nullptr if unresolvable. */
const SceneObject* OutputOwner(const SceneDocument& doc, const std::string& id);

/* Effective color of an emitter: painted override, else object base
   color, else 0. Linked objects read through to their source. */
SceneColor EmitterColor(const SceneDocument& doc, const std::string& object_id,
                        int emitter_index);

} /* namespace studio */

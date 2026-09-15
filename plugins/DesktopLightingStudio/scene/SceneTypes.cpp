/*---------------------------------------------------------*\
|| SceneTypes.cpp                                            |
||                                                           |
||   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "SceneTypes.h"

#include <cmath>

namespace studio
{

static Vec3 RotateDeg(const Vec3& p, const Vec3& rot_deg)
{
    constexpr float DEG = 3.14159265358979323846f / 180.0f;
    const float cx = std::cos(rot_deg.x * DEG), sx = std::sin(rot_deg.x * DEG);
    const float cy = std::cos(rot_deg.y * DEG), sy = std::sin(rot_deg.y * DEG);
    const float cz = std::cos(rot_deg.z * DEG), sz = std::sin(rot_deg.z * DEG);

    /* Rz * Ry * Rx column-vector convention */
    Vec3 r;
    r.x = (cy * cz) * p.x + (sx * sy * cz - cx * sz) * p.y + (cx * sy * cz + sx * sz) * p.z;
    r.y = (cy * sz) * p.x + (sx * sy * sz + cx * cz) * p.y + (cx * sy * sz - sx * cz) * p.z;
    r.z = (-sy)     * p.x + (sx * cy) * p.y               + (cx * cy) * p.z;
    return r;
}

Vec3 TransformPoint(const Transform& t, const Vec3& p)
{
    Vec3 scaled { p.x * t.scale.x, p.y * t.scale.y, p.z * t.scale.z };
    Vec3 rotated = RotateDeg(scaled, t.rotation_deg);
    return { rotated.x + t.position.x, rotated.y + t.position.y, rotated.z + t.position.z };
}

const SceneObject* FindObject(const SceneDocument& doc, const std::string& id)
{
    for(const SceneObject& obj : doc.objects)
    {
        if(obj.id == id)
        {
            return &obj;
        }
    }
    return nullptr;
}

SceneObject* FindObject(SceneDocument& doc, const std::string& id)
{
    for(SceneObject& obj : doc.objects)
    {
        if(obj.id == id)
        {
            return &obj;
        }
    }
    return nullptr;
}

const SceneObject* OutputOwner(const SceneDocument& doc, const std::string& id)
{
    const SceneObject* obj = FindObject(doc, id);
    if(obj != nullptr && obj->kind == ObjectKind::Linked && !obj->mirror_of.empty())
    {
        obj = FindObject(doc, obj->mirror_of);
    }
    return obj;
}

SceneColor EmitterColor(const SceneDocument& doc, const std::string& object_id,
                        int emitter_index)
{
    const SceneObject* owner = OutputOwner(doc, object_id);
    const std::string& owner_id = owner ? owner->id : object_id;

    auto ov = doc.emitter_colors.find(owner_id);
    if(ov != doc.emitter_colors.end())
    {
        auto it = ov->second.find(emitter_index);
        if(it != ov->second.end())
        {
            return it->second;
        }
    }
    auto base = doc.object_colors.find(owner_id);
    return (base != doc.object_colors.end()) ? base->second : 0;
}

} /* namespace studio */

/*---------------------------------------------------------*\
|| SceneTypes.cpp                                            |
||                                                           |
||   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "SceneTypes.h"
#include "SceneGraph.h"

namespace studio
{

/* T * Rxyz * S — implemented through LocalMatrix so the quaternion
   path (RotationQuat) is the single rotation convention for the
   whole plugin. */
Vec3 TransformPoint(const Transform& t, const Vec3& p)
{
    return TransformPoint(LocalMatrix(t), p);
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

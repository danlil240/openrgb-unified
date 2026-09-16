/*---------------------------------------------------------*\
|| SceneObjectModel.cpp                                      ||
||                                                           ||
||   SPDX-License-Identifier: GPL-2.0-or-later               ||
\*---------------------------------------------------------*/

#include "SceneObjectModel.h"

#include "../scene/SceneGraph.h"
#include "../scene/SceneTypes.h"
#include "../config/StudioConfig.h"
#include "../output/ControllerAdapter.h"

namespace studio
{

static QString KindName(ObjectKind kind)
{
    switch(kind)
    {
    case ObjectKind::Device: return QStringLiteral("device");
    case ObjectKind::Linked: return QStringLiteral("linked");
    case ObjectKind::Group:  return QStringLiteral("group");
    default:                 return QStringLiteral("decor");
    }
}

QString BoundStatusFor(const SceneDocument& doc,
                       const ControllerAdapter& adapter,
                       const SceneObject& o)
{
    if(o.kind == ObjectKind::Device && !o.binding.empty())
    {
        const ResolvedBinding* r = adapter.Resolution(o.binding);
        if(r != nullptr)
        {
            return (r->status == BindingStatus::Resolved)  ? QStringLiteral("ok")
                 : (r->status == BindingStatus::Ambiguous) ? QStringLiteral("ambiguous")
                                                         : QStringLiteral("unresolved");
        }
        return QStringLiteral("none");
    }
    if(o.kind == ObjectKind::Linked)
    {
        const SceneObject* owner = FindObject(doc, o.mirror_of);
        if(owner != nullptr && !owner->binding.empty())
        {
            const ResolvedBinding* r = adapter.Resolution(owner->binding);
            return (r != nullptr && r->status == BindingStatus::Resolved)
                 ? QStringLiteral("ok") : QStringLiteral("unresolved");
        }
    }
    return QStringLiteral("none");
}

SceneObjectModel::SceneObjectModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

void SceneObjectModel::Bind(const SceneDocument* d,
                            const StudioDocument* w,
                            const ControllerAdapter* a)
{
    doc     = d;
    ws      = w;
    adapter = a;
}

const SceneObject* SceneObjectModel::ObjectAt(int row) const
{
    if(doc == nullptr || row < 0 || row >= (int)order.size())
    {
        return nullptr;
    }
    return FindObject(*doc, order[row]);
}

int SceneObjectModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : (int)order.size();
}

QVariant SceneObjectModel::data(const QModelIndex& index, int role) const
{
    const SceneObject* o = ObjectAt(index.row());
    if(o == nullptr)
    {
        return QVariant();
    }
    switch(role)
    {
    case IdRole:       return QString::fromStdString(o->id);
    case LabelRole:    return QString::fromStdString(o->label);
    case KindRole:     return KindName(o->kind);
    case GeometryRole: return QString::fromStdString(o->geometry);
    case ParentIdRole: return QString::fromStdString(o->parent_id);
    case XRole:        return o->transform.position.x;
    case YRole:        return o->transform.position.y;
    case ZRole:        return o->transform.position.z;
    case RxRole:       return o->transform.rotation_deg.x;
    case RyRole:       return o->transform.rotation_deg.y;
    case RzRole:       return o->transform.rotation_deg.z;
    case SxRole:       return o->transform.scale.x;
    case SyRole:       return o->transform.scale.y;
    case SzRole:       return o->transform.scale.z;
    case DxRole:       return o->size_m.x;
    case DyRole:       return o->size_m.y;
    case DzRole:       return o->size_m.z;
    case VisibleRole:  return o->visible;
    case VerifiedRole: return o->verified;
    case EmittersRole: return (int)o->emitters.size();
    case QwRole:
    case QxRole:
    case QyRole:
    case QzRole:
    {
        /* The stored XYZ degrees converted once into the quaternion
           the QML node binds to `rotation` — the same value the
           core's world matrices use. */
        const Quat q = RotationQuat(o->transform.rotation_deg);
        switch(role)
        {
        case QwRole: return q.w;
        case QxRole: return q.x;
        case QyRole: return q.y;
        default:     return q.z;
        }
    }
    case BxRole:
    case ByRole:
    case BzRole:
    {
        /* Resolved body size — the "0 axis = canonical" contract
           lives in the core, not in QML. */
        const Vec3 body = ResolvedBodySize(*o);
        switch(role)
        {
        case BxRole: return body.x;
        case ByRole: return body.y;
        default:     return body.z;
        }
    }
    case BoundRole:
        return adapter != nullptr
             ? BoundStatusFor(*doc, *adapter, *o)
             : QStringLiteral("none");
    case InstancePathRole:
    case LockedRole:
    {
        const std::string inst = o->id.substr(0, o->id.find('/'));
        if(role == InstancePathRole)
        {
            return QString::fromStdString(inst);
        }
        if(ws != nullptr)
        {
            const auto it = ws->device_settings.find(inst);
            return it != ws->device_settings.end() && it->second.locked;
        }
        return false;
    }
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> SceneObjectModel::roleNames() const
{
    return {
        { IdRole,           "id" },
        { LabelRole,        "label" },
        { KindRole,         "kind" },
        { GeometryRole,     "geometry" },
        { ParentIdRole,     "parentId" },
        { XRole,            "x" },
        { YRole,            "y" },
        { ZRole,            "z" },
        { RxRole,           "rx" },
        { RyRole,           "ry" },
        { RzRole,           "rz" },
        { QwRole,           "qw" },
        { QxRole,           "qx" },
        { QyRole,           "qy" },
        { QzRole,           "qz" },
        { SxRole,           "sx" },
        { SyRole,           "sy" },
        { SzRole,           "sz" },
        { DxRole,           "dx" },
        { DyRole,           "dy" },
        { DzRole,           "dz" },
        { BxRole,           "bx" },
        { ByRole,           "by" },
        { BzRole,           "bz" },
        { VisibleRole,      "visible" },
        { VerifiedRole,     "verified" },
        { EmittersRole,     "emitters" },
        { BoundRole,        "bound" },
        { InstancePathRole, "instancePath" },
        { LockedRole,       "locked" },
    };
}

void SceneObjectModel::ResetFrom()
{
    beginResetModel();
    order.clear();
    if(doc != nullptr)
    {
        for(const SceneObject* po : TopologicalOrder(*doc))
        {
            order.push_back(po->id);
        }
    }
    endResetModel();
}

void SceneObjectModel::UpdateTransforms(const std::set<std::string>& ids)
{
    for(int row = 0; row < (int)order.size(); row++)
    {
        /* An instance's transform lives on its group row; entity
           rows keep local transforms but a settings re-key may
           touch any path, so prefix-match the instance segment. */
        const std::string& id = order[row];
        const std::string inst = id.substr(0, id.find('/'));
        if(ids.count(id) || ids.count(inst))
        {
            const QModelIndex mi = index(row);
            emit dataChanged(mi, mi);
        }
    }
}

void SceneObjectModel::RefreshAll()
{
    if(order.empty())
    {
        return;
    }
    emit dataChanged(index(0), index((int)order.size() - 1));
}

int SceneObjectModel::RowOf(const std::string& id) const
{
    for(int i = 0; i < (int)order.size(); i++)
    {
        if(order[i] == id)
        {
            return i;
        }
    }
    return -1;
}

} /* namespace studio */

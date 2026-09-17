/*---------------------------------------------------------*\
||| EffectLayerModel.cpp                                    ||
|||                                                          ||
|||   SPDX-License-Identifier: GPL-2.0-or-later               ||
\*---------------------------------------------------------*/

#include "EffectLayerModel.h"

namespace studio
{
namespace
{

const char* BlendName(BlendMode m)
{
    switch(m)
    {
    case BlendMode::Add:    return "add";
    case BlendMode::Screen: return "screen";
    default:                return "replace";
    }
}

const char* SpaceName(CoordSpace s)
{
    return s == CoordSpace::Local ? "local" : "world";
}

} /* anonymous namespace */

EffectLayerModel::EffectLayerModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int EffectLayerModel::rowCount(const QModelIndex& parent) const
{
    if(parent.isValid())
    {
        return 0;
    }
    return (int)rows.size();
}

QVariantMap EffectLayerModel::RowMap(const EffectLayer& l,
                                     int row) const
{
    QVariantMap m;
    m["index"]     = row;
    m["primitive"] = QString::fromStdString(l.primitive);
    m["enabled"]   = l.enabled;
    m["blend"]     = BlendName(l.blend);
    m["opacity"]   = l.opacity;
    m["space"]     = SpaceName(l.space);
    m["source"]    = QString::fromStdString(l.source);
    m["stops"]     = (int)l.palette.stops.size();
    m["pathPts"]   = (int)l.path.size();
    /* Compact row summary: "world · ×0.85 · 4 stops". */
    QString s = QString::fromLatin1(SpaceName(l.space));
    if(l.opacity != 1.0f)
    {
        s += QStringLiteral(" · ×%1")
                 .arg(QString::number(l.opacity, 'g', 3));
    }
    if(!l.palette.stops.empty())
    {
        s += QStringLiteral(" · %1 stops")
                 .arg(l.palette.stops.size());
    }
    if(!l.path.empty())
    {
        s += QStringLiteral(" · %1 pts").arg(l.path.size());
    }
    m["summary"] = s;
    return m;
}

QVariant EffectLayerModel::data(const QModelIndex& index,
                                int role) const
{
    if(!index.isValid() || index.row() < 0
       || index.row() >= (int)rows.size())
    {
        return {};
    }
    const EffectLayer& l = rows[index.row()];
    switch(role)
    {
    case IndexRole:     return index.row();
    case PrimitiveRole: return QString::fromStdString(l.primitive);
    case EnabledRole:   return l.enabled;
    case BlendRole:     return BlendName(l.blend);
    case OpacityRole:   return l.opacity;
    case SpaceRole:     return SpaceName(l.space);
    case SourceRole:    return QString::fromStdString(l.source);
    case StopsRole:     return (int)l.palette.stops.size();
    case PathRole:      return (int)l.path.size();
    case SummaryRole:   return RowMap(l, index.row())["summary"];
    default:            return {};
    }
}

QHash<int, QByteArray> EffectLayerModel::roleNames() const
{
    return {
        { IndexRole,     "index"     },
        { PrimitiveRole, "primitive" },
        { EnabledRole,   "enabled"   },
        { BlendRole,     "blend"     },
        { OpacityRole,   "opacity"   },
        { SpaceRole,     "space"     },
        { SourceRole,    "source"    },
        { StopsRole,     "stops"     },
        { PathRole,      "pathPts"   },
        { SummaryRole,   "summary"   },
    };
}

bool EffectLayerModel::SetStack(const std::vector<EffectLayer>& layers)
{
    if(layers.size() != rows.size())
    {
        beginResetModel();
        rows = layers;
        endResetModel();
        return true;
    }
    /* Same shape — diff each row's display fields so a scrub
       preview updates the changed row without a reset. */
    bool any = false;
    for(int i = 0; i < (int)rows.size(); i++)
    {
        if(RowMap(rows[i], i) != RowMap(layers[i], i))
        {
            emit dataChanged(index(i), index(i));
            any = true;
        }
    }
    if(any)
    {
        rows = layers;
    }
    return any;
}

QVariantMap EffectLayerModel::rowAt(int row) const
{
    if(row < 0 || row >= (int)rows.size())
    {
        return {};
    }
    return RowMap(rows[row], row);
}

} /* namespace studio */

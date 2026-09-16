/*---------------------------------------------------------*\
|| PresetListModel.cpp                                      ||
||                                                          ||
||   SPDX-License-Identifier: GPL-2.0-or-later               ||
\*---------------------------------------------------------*/

#include "PresetListModel.h"

#include "../config/StudioConfig.h"

#include <algorithm>

namespace studio
{

PresetListModel::PresetListModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

void PresetListModel::Bind(const PresetRegistry* r,
                           const WorkspaceMeta* m)
{
    reg  = r;
    meta = m;
}

int PresetListModel::rowCount(const QModelIndex& parent) const
{
    if(parent.isValid())
    {
        return 0;
    }
    return (int)rows.size();
}

bool PresetListModel::IsFavorite(const std::string& id) const
{
    if(meta == nullptr)
    {
        return false;
    }
    return std::find(meta->ui.favorites.begin(), meta->ui.favorites.end(),
                     id) != meta->ui.favorites.end();
}

QVariant PresetListModel::data(const QModelIndex& index, int role) const
{
    if(!index.isValid() || index.row() < 0
       || index.row() >= (int)rows.size())
    {
        return {};
    }
    const PresetRegistry::PresetInfo& info = rows[index.row()];
    switch(role)
    {
    case TypeIdRole:   return QString::fromStdString(info.id);
    case NameRole:     return QString::fromStdString(info.name);
    case CategoryRole: return QString::fromStdString(info.category);
    case FromFileRole: return info.from_file;
    case ZoneCountRole: return (int)info.zone_count;
    case LedTotalRole:  return (int)info.led_total;
    case BoundsRole:
        return QVariant::fromValue(QVector3D(info.bounds_m.x,
                                             info.bounds_m.y,
                                             info.bounds_m.z));
    case FavoriteRole: return IsFavorite(info.id);
    default:           return {};
    }
}

QHash<int, QByteArray> PresetListModel::roleNames() const
{
    return {
        { TypeIdRole,    "typeId"    },
        { NameRole,      "name"      },
        { CategoryRole,  "category"  },
        { FromFileRole,  "fromFile"  },
        { ZoneCountRole, "zoneCount" },
        { LedTotalRole,  "ledTotal"  },
        { BoundsRole,    "bounds"    },
        { FavoriteRole,  "favorite"  },
    };
}

void PresetListModel::Reload()
{
    beginResetModel();
    rows = (reg != nullptr) ? reg->List()
                            : std::vector<PresetRegistry::PresetInfo>();
    endResetModel();
}

void PresetListModel::RefreshFavorites()
{
    if(rows.empty())
    {
        return;
    }
    emit dataChanged(index(0), index((int)rows.size() - 1),
                     { FavoriteRole });
}

const PresetRegistry::PresetInfo*
PresetListModel::InfoFor(const QString& id) const
{
    const std::string s = id.toStdString();
    for(const PresetRegistry::PresetInfo& info : rows)
    {
        if(info.id == s)
        {
            return &info;
        }
    }
    return nullptr;
}

QVariantMap PresetListModel::rowAt(int row) const
{
    QVariantMap m;
    if(row < 0 || row >= (int)rows.size())
    {
        return m;
    }
    const PresetRegistry::PresetInfo& info = rows[row];
    m["typeId"]    = QString::fromStdString(info.id);
    m["name"]      = QString::fromStdString(info.name);
    m["category"]  = QString::fromStdString(info.category);
    m["fromFile"]  = info.from_file;
    m["zoneCount"] = (int)info.zone_count;
    m["ledTotal"]  = (int)info.led_total;
    m["bounds"]    = QVariant::fromValue(
        QVector3D(info.bounds_m.x, info.bounds_m.y, info.bounds_m.z));
    m["favorite"]  = IsFavorite(info.id);
    return m;
}

} /* namespace studio */

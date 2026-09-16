/*---------------------------------------------------------*\
|| PresetListModel.h                                        ||
||                                                          ||
||   QAbstractListModel over PresetRegistry::List() — the ||
||   merged file-over-default device-type listing (Qt —   ||
||   plugin-only, NOT part of the Qt-free editor core).   ||
||   Feeds the DeviceLibrary panel: search/category/fav   ||
||   sorting is pure-QML on top of rowAt(), so this model ||
||   only answers "what types exist and what are their    ||
||   facts".                                               ||
||                                                          ||
||   Update paths:                                        ||
||   - Reload()          — registry content changed: full ||
||     reset + fresh List() snapshot.                     ||
||   - RefreshFavorites()— meta.ui.favorites changed: a   ||
||     dataChanged on FavoriteRole alone (the QML sort    ||
||     reorders; rows stay alive).                        ||
||                                                          ||
||   SPDX-License-Identifier: GPL-2.0-or-later               ||
\*---------------------------------------------------------*/

#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QString>
#include <QVariant>
#include <QVariantMap>
#include <QVector3D>

#include "../presets/PresetRegistry.h"

#include <set>
#include <string>
#include <vector>

namespace studio
{
struct WorkspaceMeta;

class PresetListModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Role
    {
        TypeIdRole = Qt::UserRole + 1,
        NameRole,
        CategoryRole,
        FromFileRole,
        ZoneCountRole,
        LedTotalRole,
        BoundsRole,        /* QVector3D size in metres  */
        FavoriteRole,      /* read live from meta       */
    };
    Q_ENUM(Role)

    explicit PresetListModel(QObject* parent = nullptr);

    /* Point the model at the bridge-owned registry + workspace
       meta. Both pointers stay valid for the bridge's lifetime;
       favorites are re-read live so RefreshFavorites() only needs
       a dataChanged. */
    void Bind(const PresetRegistry* reg, const WorkspaceMeta* meta);

    int      rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index,
                  int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    /* Registry content changed (reload / new variant / new
       selection-preset): re-snapshot List() under a full reset. */
    void Reload();
    /* Only ui.favorites changed — re-badge every row. */
    void RefreshFavorites();

    /* Listing facts for a type id — the bridge reads floor_y
       here to drop new instances onto the desk surface. */
    const PresetRegistry::PresetInfo* InfoFor(const QString& id) const;

    /* QML helpers — the library panel's search/category/favorite
       pipeline consumes plain QVariantMaps so the same code path
       runs against a plain-JS stub in tests. */
    Q_INVOKABLE int count() const { return (int)rows.size(); }
    Q_INVOKABLE QVariantMap rowAt(int row) const;

private:
    bool IsFavorite(const std::string& id) const;

    const PresetRegistry*             reg  = nullptr;
    const WorkspaceMeta*              meta = nullptr;
    std::vector<PresetRegistry::PresetInfo> rows;
};

} /* namespace studio */

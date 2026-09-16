/*---------------------------------------------------------*\
|| SceneObjectModel.h                                        ||
||                                                           ||
||   QAbstractListModel over the resolved SceneDocument   ||
||   (Qt — plugin-only, NOT part of the Qt-free editor    ||
||   core). Roles cover everything the legacy objectList  ||
||   QVariantList publishes, plus instancePath/locked.    ||
||                                                           ||
||   Two update paths:                                     ||
||   - ResetFrom()      — full reset for structural       ||
||     changes (add/remove/rename/reparent).              ||
||   - UpdateTransforms(ids) — granular dataChanged for   ||
||     the drag path: delegates stay alive, only the      ||
||     touched instance rows re-read.                     ||
||                                                           ||
||   Rows follow TopologicalOrder so QML delegates can    ||
||   reparent through a node map populated in model       ||
||   order, exactly like objectList() did.                ||
||                                                           ||
||   SPDX-License-Identifier: GPL-2.0-or-later               ||
\*---------------------------------------------------------*/

#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QString>
#include <QVariant>

#include <set>
#include <string>
#include <vector>

namespace studio
{
class ControllerAdapter;
struct SceneDocument;
struct SceneObject;
struct StudioDocument;

/* Shared bound-status text ("ok"/"ambiguous"/"unresolved"/"none")
   used by the model and SceneBridge::objectList. */
QString BoundStatusFor(const SceneDocument& doc,
                       const ControllerAdapter& adapter,
                       const SceneObject& o);

class SceneObjectModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Role
    {
        IdRole = Qt::UserRole + 1,
        LabelRole,
        KindRole,
        GeometryRole,
        ParentIdRole,
        XRole, YRole, ZRole,
        RxRole, RyRole, RzRole,
        QwRole, QxRole, QyRole, QzRole,
        SxRole, SyRole, SzRole,
        DxRole, DyRole, DzRole,     /* authored size_m        */
        BxRole, ByRole, BzRole,     /* ResolvedBodySize       */
        VisibleRole,
        VerifiedRole,
        EmittersRole,
        BoundRole,
        InstancePathRole,           /* owning root instance   */
        LockedRole,
    };
    Q_ENUM(Role)

    explicit SceneObjectModel(QObject* parent = nullptr);

    /* Point the model at the bridge-owned documents. The
       pointers stay valid for the bridge's lifetime; contents
       are re-read on demand so an adopted doc is picked up
       without rebinding. */
    void Bind(const SceneDocument* doc, const StudioDocument* workspace,
              const ControllerAdapter* adapter);

    int      rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index,
                  int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    /* Structure changed (instances added/removed/renamed/
       reparented): rebuild the row order. */
    void ResetFrom();
    /* Only transforms (or per-row flags) changed for these
       instance ids: emit dataChanged per matching row — the
       gesture path. Ids are object ids; instance ids match the
       group row that carries the moved transform. */
    void UpdateTransforms(const std::set<std::string>& ids);
    /* Every row re-read (e.g. hardware refresh changed bound). */
    void RefreshAll();

    int RowOf(const std::string& id) const;

private:
    const SceneObject* ObjectAt(int row) const;

    const SceneDocument*     doc  = nullptr;
    const StudioDocument*    ws   = nullptr;
    const ControllerAdapter* adapter = nullptr;
    /* Topological row order — object ids only; all fields are
       read live from `doc` so a replaced document can't leave
       dangling object pointers here. */
    std::vector<std::string> order;
};

} /* namespace studio */

/*---------------------------------------------------------*\
|| SceneBridge.cpp                                           |
||                                                           |
||   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "SceneBridge.h"

#include <QThread>
#include <QUndoCommand>
#include <QUndoStack>

#include "../scene/DefaultDesk.h"
#include "../scene/EmitterLayout.h"
#include "../scene/SceneJson.h"
#include "OpenRGBPluginInterface.h"

#include <cmath>
#include <thread>

namespace studio
{

/*---------------------------------------------------------*\
|| Undo commands                                             |
\*---------------------------------------------------------*/
class SceneColorCommand : public QUndoCommand
{
public:
    SceneColorCommand(SceneBridge* b, const std::string& id,
                      SceneColor oldc, SceneColor newc)
        : bridge(b), owner(id), old_color(oldc), new_color(newc)
    {
        setText(QStringLiteral("color %1").arg(QString::fromStdString(id)));
    }
    void undo() override { bridge->applyObjectColor(owner, old_color); }
    void redo() override { bridge->applyObjectColor(owner, new_color); }
private:
    SceneBridge* bridge;
    std::string  owner;
    SceneColor   old_color, new_color;
};

class SceneEmitterCommand : public QUndoCommand
{
public:
    SceneEmitterCommand(SceneBridge* b, const std::string& id, int idx,
                        SceneColor oldc, SceneColor newc)
        : bridge(b), owner(id), index(idx), old_color(oldc), new_color(newc)
    {
        setText(QStringLiteral("paint %1[%2]").arg(QString::fromStdString(id)).arg(idx));
    }
    void undo() override { bridge->applyEmitterColor(owner, index, old_color); }
    void redo() override { bridge->applyEmitterColor(owner, index, new_color); }
private:
    SceneBridge* bridge;
    std::string  owner;
    int          index;
    SceneColor   old_color, new_color;
};

class SceneBrightnessCommand : public QUndoCommand
{
public:
    SceneBrightnessCommand(SceneBridge* b, float oldb, float newb)
        : bridge(b), old_b(oldb), new_b(newb)
    {
        setText(QStringLiteral("brightness"));
    }
    void undo() override { bridge->applyBrightness(old_b); }
    void redo() override { bridge->applyBrightness(new_b); }
private:
    SceneBridge* bridge;
    float        old_b, new_b;
};

/*---------------------------------------------------------*\
|| Bridge                                                    |
\*---------------------------------------------------------*/
SceneBridge::SceneBridge(OpenRGBPluginAPIInterface* plugin_api, QObject* parent)
    : QObject(parent)
    , api(plugin_api)
    , adapter(plugin_api)
    , undo_stack(new QUndoStack(this))
{
    doc = BuildDefaultDesk();
    refreshDevices();
}

SceneBridge::~SceneBridge() = default;

int  SceneBridge::brightnessPct() const { return (int)std::lround(doc.brightness * 100.0f); }
bool SceneBridge::canUndo() const       { return undo_stack->canUndo(); }
bool SceneBridge::canRedo() const       { return undo_stack->canRedo(); }

static QString Hex(SceneColor c)
{
    return QStringLiteral("#%1%2%3")
        .arg(c & 0xFF, 2, 16, QLatin1Char('0'))
        .arg((c >> 8) & 0xFF, 2, 16, QLatin1Char('0'))
        .arg((c >> 16) & 0xFF, 2, 16, QLatin1Char('0'));
}

static QString KindName(ObjectKind kind)
{
    switch(kind)
    {
    case ObjectKind::Device: return QStringLiteral("device");
    case ObjectKind::Linked: return QStringLiteral("linked");
    default:                 return QStringLiteral("decor");
    }
}

QVariantList SceneBridge::objectList() const
{
    QVariantList out;
    for(const SceneObject& o : doc.objects)
    {
        QVariantMap m;
        m["id"]       = QString::fromStdString(o.id);
        m["label"]    = QString::fromStdString(o.label);
        m["kind"]     = KindName(o.kind);
        m["geometry"] = QString::fromStdString(o.geometry);
        m["x"]  = o.transform.position.x;
        m["y"]  = o.transform.position.y;
        m["z"]  = o.transform.position.z;
        m["rx"] = o.transform.rotation_deg.x;
        m["ry"] = o.transform.rotation_deg.y;
        m["rz"] = o.transform.rotation_deg.z;
        m["sx"] = o.transform.scale.x;
        m["sy"] = o.transform.scale.y;
        m["sz"] = o.transform.scale.z;
        m["visible"]  = o.visible;
        m["verified"] = o.verified;
        m["emitters"] = (int)o.emitters.size();

        QString bound = "none";
        if(o.kind == ObjectKind::Device && !o.binding.empty())
        {
            const ResolvedBinding* r = adapter.Resolution(o.binding);
            if(r != nullptr)
            {
                bound = (r->status == BindingStatus::Resolved)  ? "ok"
                      : (r->status == BindingStatus::Ambiguous) ? "ambiguous"
                                                              : "unresolved";
            }
        }
        else if(o.kind == ObjectKind::Linked)
        {
            const SceneObject* owner = FindObject(doc, o.mirror_of);
            if(owner != nullptr && !owner->binding.empty())
            {
                const ResolvedBinding* r = adapter.Resolution(owner->binding);
                bound = (r != nullptr && r->status == BindingStatus::Resolved)
                      ? "ok" : "unresolved";
            }
        }
        m["bound"] = bound;
        out.push_back(m);
    }
    return out;
}

QVariantList SceneBridge::emittersOf(const QString& objectId) const
{
    QVariantList out;
    const std::string id = objectId.toStdString();

    const SceneObject* obj = FindObject(doc, id);
    if(obj == nullptr)
    {
        return out;
    }
    const SceneObject* owner = OutputOwner(doc, id);
    if(owner == nullptr)
    {
        owner = obj;
    }

    for(size_t i = 0; i < owner->emitters.size(); i++)
    {
        const Emitter& e = owner->emitters[i];
        QVariantMap m;
        m["x"] = e.local_pos.x;
        m["y"] = e.local_pos.y;
        m["z"] = e.local_pos.z;
        m["c"] = Hex(EmitterColor(doc, owner->id, (int)i));
        m["i"] = (int)i;
        out.push_back(m);
    }
    return out;
}

QVariantMap SceneBridge::objectInfo(const QString& objectId) const
{
    QVariantMap m;
    const SceneObject* obj = FindObject(doc, objectId.toStdString());
    if(obj == nullptr)
    {
        return m;
    }
    m["label"]    = QString::fromStdString(obj->label);
    m["kind"]     = KindName(obj->kind);
    m["verified"] = obj->verified;

    const SceneObject* owner = OutputOwner(doc, obj->id);
    if(owner != nullptr && !owner->binding.empty())
    {
        const ResolvedBinding* r = adapter.Resolution(owner->binding);
        m["binding"]  = QString::fromStdString(owner->binding);
        m["bound"]    = (r != nullptr && r->status == BindingStatus::Resolved);
        m["reason"]   = r ? QString::fromStdString(r->reason) : QString();
        m["writable"] = owner->verified && r != nullptr
                        && r->status == BindingStatus::Resolved;
    }
    return m;
}

QString SceneBridge::bindingReport() const
{
    QStringList lines;
    for(const DeviceBinding& b : doc.bindings)
    {
        const ResolvedBinding* r = adapter.Resolution(b.id);
        const char* st = (r == nullptr)                     ? "?"
                       : (r->status == BindingStatus::Resolved)  ? "OK "
                       : (r->status == BindingStatus::Ambiguous) ? "AMB"
                                                                : "MISS";
        lines << QStringLiteral("%1 %2 -> %3%4%5")
                     .arg(QLatin1String(st))
                     .arg(QString::fromStdString(b.id))
                     .arg(QString::fromStdString(b.controller_name))
                     .arg(b.zone_name.empty() ? QString() : QStringLiteral(" / %1").arg(QString::fromStdString(b.zone_name)))
                     .arg(r != nullptr && !r->reason.empty()
                          ? QStringLiteral("   (%1)").arg(QString::fromStdString(r->reason)) : QString());
    }
    return lines.join('\n');
}

void SceneBridge::select(const QString& objectId)
{
    if(selected == objectId)
    {
        return;
    }
    selected = objectId;
    emit selectionChanged();
}

void SceneBridge::setSelectedColor(const QColor& color)
{
    const SceneObject* owner = OutputOwner(doc, selected.toStdString());
    if(owner == nullptr || owner->kind != ObjectKind::Device)
    {
        setStatus(QStringLiteral("select a light device first"));
        return;
    }
    const SceneColor nc = MakeSceneColor(color.red(), color.green(), color.blue());
    auto it = doc.object_colors.find(owner->id);
    const SceneColor oc = (it != doc.object_colors.end()) ? it->second : 0;
    undo_stack->push(new SceneColorCommand(this, owner->id, oc, nc));
    emit undoChanged();
}

void SceneBridge::paintEmitter(const QString& objectId, int index, const QColor& color)
{
    const SceneObject* owner = OutputOwner(doc, objectId.toStdString());
    if(owner == nullptr || owner->kind != ObjectKind::Device
       || index < 0 || index >= (int)owner->emitters.size())
    {
        return;
    }
    const SceneColor oc = EmitterColor(doc, owner->id, index);
    const SceneColor nc = MakeSceneColor(color.red(), color.green(), color.blue());
    undo_stack->push(new SceneEmitterCommand(this, owner->id, index, oc, nc));
    emit undoChanged();
}

void SceneBridge::setBrightnessPct(int pct)
{
    const float nb = qBound(0, pct, 100) / 100.0f;
    if(qFuzzyCompare(nb, doc.brightness))
    {
        return;
    }
    undo_stack->push(new SceneBrightnessCommand(this, doc.brightness, nb));
    emit undoChanged();
}

void SceneBridge::setLive(bool on)
{
    if(live_output == on)
    {
        return;
    }
    live_output = on;
    emit liveChanged();
    if(on)
    {
        pushLiveAll();
    }
}

void SceneBridge::setCaseGhost(bool on)
{
    if(case_ghost == on)
    {
        return;
    }
    case_ghost = on;
    emit caseGhostChanged();
}

void SceneBridge::setPaintColor(const QColor& color)
{
    if(!color.isValid() || paint_color == color)
    {
        return;
    }
    paint_color = color;
    emit paintColorChanged();
}

void SceneBridge::undo() { undo_stack->undo(); emit undoChanged(); }
void SceneBridge::redo() { undo_stack->redo(); emit undoChanged(); }

void SceneBridge::refreshDevices()
{
    adapter.Refresh(doc);
    rebuildMatrixLayouts();
    emit sceneChanged();
}

bool SceneBridge::saveScene()
{
    if(api == nullptr)
    {
        setStatus(QStringLiteral("plugin API unavailable — cannot save"));
        return false;
    }
    nlohmann::json j;
    j["scene"] = ToJson(doc);
    api->SetSettings("DesktopLightingStudio", j);
    api->SaveSettings();
    setStatus(QStringLiteral("scene saved (%1 objects)").arg((int)doc.objects.size()));
    return true;
}

bool SceneBridge::loadScene()
{
    if(api == nullptr)
    {
        return false;
    }
    const nlohmann::json j = api->GetSettings("DesktopLightingStudio");
    if(!j.is_object() || !j.contains("scene") || !FromJson(j["scene"], doc))
    {
        setStatus(QStringLiteral("no saved scene — using default desk"));
        return false;
    }
    undo_stack->clear();
    emit undoChanged();
    refreshDevices();
    setStatus(QStringLiteral("scene loaded (%1 objects)").arg((int)doc.objects.size()));
    if(live_output)
    {
        pushLiveAll();
    }
    return true;
}

void SceneBridge::resetScene()
{
    doc = BuildDefaultDesk();
    undo_stack->clear();
    emit undoChanged();
    refreshDevices();
    setStatus(QStringLiteral("scene reset to default desk"));
}

/*---------------------------------------------------------*\
|| Core ops (undo commands call these)                       |
\*---------------------------------------------------------*/
void SceneBridge::applyObjectColor(const std::string& owner_id, SceneColor color)
{
    doc.object_colors[owner_id] = color;
    emit emittersChanged(QString::fromStdString(owner_id));
    for(const SceneObject& o : doc.objects)
    {
        if(o.kind == ObjectKind::Linked && o.mirror_of == owner_id)
        {
            emit emittersChanged(QString::fromStdString(o.id));
        }
    }
    pushLive(owner_id);
}

void SceneBridge::applyEmitterColor(const std::string& owner_id, int index, SceneColor color)
{
    doc.emitter_colors[owner_id][index] = color;
    emit emittersChanged(QString::fromStdString(owner_id));
    for(const SceneObject& o : doc.objects)
    {
        if(o.kind == ObjectKind::Linked && o.mirror_of == owner_id)
        {
            emit emittersChanged(QString::fromStdString(o.id));
        }
    }
    pushLive(owner_id);
}

void SceneBridge::applyBrightness(float brightness)
{
    doc.brightness = brightness;
    emit brightnessChanged();
    for(const SceneObject& o : doc.objects)
    {
        emit emittersChanged(QString::fromStdString(o.id));
    }
    pushLiveAll();
}

/*---------------------------------------------------------*\
|| Live push — serialized worker, doc snapshot so the UI    |
|| thread can keep editing safely.                          |
\*---------------------------------------------------------*/
void SceneBridge::pushLive(const std::string& object_id)
{
    if(!live_output || api == nullptr)
    {
        return;
    }
    const SceneDocument snapshot = doc;
    const std::string oid = object_id;
    std::thread([this, snapshot, oid]()
    {
        QMutexLocker lock(&io_mutex);
        const std::string err = adapter.PushObject(snapshot, oid);
        if(!err.empty())
        {
            const QString msg = QString::fromStdString(err);
            QMetaObject::invokeMethod(this, [this, msg]() { setStatus(msg); },
                                      Qt::QueuedConnection);
        }
    }).detach();
}

void SceneBridge::pushLiveAll()
{
    if(!live_output || api == nullptr)
    {
        return;
    }
    const SceneDocument snapshot = doc;
    std::thread([this, snapshot]()
    {
        QMutexLocker lock(&io_mutex);
        const std::string err = adapter.PushAll(snapshot);
        const QString msg = err.empty() ? QStringLiteral("live output on")
                                        : QString::fromStdString(err);
        QMetaObject::invokeMethod(this, [this, msg]() { setStatus(msg); },
                                  Qt::QueuedConnection);
    }).detach();
}

void SceneBridge::rebuildMatrixLayouts()
{
    for(SceneObject& obj : doc.objects)
    {
        if(obj.layout != "matrix_map" || obj.kind != ObjectKind::Device)
        {
            continue;
        }
        unsigned int rows = 0, cols = 0;
        std::vector<unsigned int> map;
        if(!adapter.ZoneMatrix(obj.binding, rows, cols, map))
        {
            continue;
        }
        const float pitch = 0.019f;
        const Vec3 origin { -((float)cols - 1) * pitch * 0.5f,
                            0.016f,
                            ((float)rows - 1) * pitch * 0.5f };
        obj.emitters = layout::KeyboardMatrix(rows, cols, map.data(), 0xFFFFFFFFu,
                                            pitch, pitch, origin, obj.id);
    }
}

void SceneBridge::setStatus(const QString& text)
{
    status = text;
    emit statusChanged();
    emit statusMessage(text);
}

} /* namespace studio */

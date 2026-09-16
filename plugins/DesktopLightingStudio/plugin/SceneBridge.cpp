/*---------------------------------------------------------*\
|| SceneBridge.cpp                                           |
||                                                           |
||   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "SceneBridge.h"

#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QThread>
#include <QTimer>
#include <QUndoCommand>
#include <QUndoStack>

#include "../scene/DefaultDesk.h"
#include "../scene/EmitterLayout.h"
#include "../scene/SceneGraph.h"
#include "../scene/SceneResolver.h"
#include "../config/ConfigStore.h"
#include "../editor/SceneObjectModel.h"
#include "../effects/Presets.h"
#include "../inputs/KeyMap.h"
#include "../inputs/ScreenSampler.h"
#include "OpenRGBPluginInterface.h"
#include "filesystem.h"

#include <algorithm>
#include <cctype>
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

/* One EditorEdit = one undo command. The Qt-free record carries
   every section delta; applyEdit does workspace -> resolve ->
   adopt -> model -> key lookup -> markDirty. `applied` skips the
   redo() QUndoStack::push fires, because the committing path
   already landed the edit. */
class SceneEditCommand : public QUndoCommand
{
public:
    SceneEditCommand(SceneBridge* b, EditorEdit e, bool already_applied)
        : bridge(b), edit(std::move(e)), applied(already_applied)
    {
        setText(QString::fromStdString(edit.label));
    }
    void undo() override
    {
        bridge->applyEdit(edit, true);
        applied = false;
    }
    void redo() override
    {
        if(applied)
        {
            applied = false;
            return;
        }
        bridge->applyEdit(edit, false);
    }
private:
    SceneBridge* bridge;
    EditorEdit   edit;
    bool         applied;
};

/*---------------------------------------------------------*\
|| Bridge                                                    |
\*---------------------------------------------------------*/
SceneBridge::SceneBridge(OpenRGBPluginAPIInterface* plugin_api, QObject* parent)
    : QObject(parent)
    , api(plugin_api)
    , adapter(plugin_api)
    , editor(workspace)
    , undo_stack(new QUndoStack(this))
{
    play_timer = new QTimer(this);
    /* PreciseTimer: a coarse WM_TIMER quantizes to the ~15.6 ms
       Windows system tick, so a 33 ms request fires at ~31/47 ms
       irregularly — visible judder even though play_t uses real
       elapsed time. ~60 fps at ~1 ms cadence instead. */
    play_timer->setTimerType(Qt::PreciseTimer);
    play_timer->setInterval(16);
    connect(play_timer, &QTimer::timeout, this, &SceneBridge::tick);
    play_clock = new QElapsedTimer();

    /* Input providers stamp events on the play clock so ripple ages
       share the evaluation time base. */
    input_bus.SetNow([this]() { return play_t.load(); });
    screen_in = new ScreenSampler(&input_bus, this);

    /*------------------------------------------------*\
    || Workspace store — studio.json under the host's  ||
    || resolved user config dir (no fixed paths).      ||
    \*------------------------------------------------*/
    QString cfg_base;
    if(api != nullptr)
    {
        const filesystem::path p = api->GetConfigurationDirectory();
#ifdef _WIN32
        cfg_base = QString::fromStdWString(p.wstring());
#else
        cfg_base = QString::fromUtf8(p.u8string().c_str());
#endif
    }
    if(cfg_base.isEmpty())
    {
        cfg_base = QDir::currentPath();
    }
    store = new ConfigStore(cfg_base + "/DesktopLightingStudio", this);
    store->SetSnapshotProvider([this]() { return CurrentWorkspace(); });
    connect(store, &ConfigStore::dirtyChanged,
            this, &SceneBridge::dirtyChanged);
    connect(store, &ConfigStore::externalChange, this, [this]()
    {
        emit externalChangeDetected(store->dirty());
    });
    connect(store, &ConfigStore::autosaveFailed, this,
            [this](const QString& msg)
    {
        setStatus(QStringLiteral("autosave failed: %1").arg(msg));
    });

    /* Type library: packaged defaults underneath whatever the
       workspace presets dir holds (loaded on each Reload). */
    registry.SetDefaults(DefaultDevicePresets());

    /* Start on the default compact workspace resolved through the
       registry — resolution cannot fail on the bundled types, but
       guard anyway. */
    workspace = BuildDefaultWorkspace();
    SceneDocument resolved;
    if(ResolveScene(workspace, registry, resolved, nullptr))
    {
        doc = resolved;
    }
    else
    {
        doc = BuildDefaultDesk();
    }
    doc.name = workspace.meta.name;

    /* Stable object model for the editor — reads doc/workspace/
       adapter through bound member pointers; ResetFrom lands the
       initial row order. */
    obj_model = new SceneObjectModel(this);
    obj_model->Bind(&doc, &workspace, &adapter);

    refreshDevices();
}

SceneBridge::~SceneBridge()
{
    /* Leave recoverable edits behind on a clean shutdown too. */
    if(store != nullptr)
    {
        store->FlushAutosave();
    }

    /* Input sources disconnect before teardown — hooks and capture
       threads join here, never outliving the bridge. */
    audio_in.Stop();
    key_in.Stop();
    screen_in->Stop();

    /* Detached push workers hold `this` — give in-flight writes a
       moment to finish before the bridge is torn down. */
    play_timer->stop();
    for(int i = 0; i < 50
            && (push_in_flight.load()
                || lane_in_flight[0].load() || lane_in_flight[1].load());
        i++)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    delete play_clock;
}

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

static SceneColor ScaleScene(SceneColor c, float brightness)
{
    const unsigned int r = (unsigned int)((c & 0xFF) * brightness) & 0xFF;
    const unsigned int g = (unsigned int)(((c >> 8) & 0xFF) * brightness) & 0xFF;
    const unsigned int b = (unsigned int)(((c >> 16) & 0xFF) * brightness) & 0xFF;
    return (b << 16) | (g << 8) | r;
}

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

QVariantList SceneBridge::objectList() const
{
    QVariantList out;
    /* Parents precede children: the QML delegate reparents through a
       node map populated in model order. */
    for(const SceneObject* po : TopologicalOrder(doc))
    {
        const SceneObject& o = *po;
        QVariantMap m;
        m["id"]       = QString::fromStdString(o.id);
        m["label"]    = QString::fromStdString(o.label);
        m["kind"]     = KindName(o.kind);
        m["geometry"] = QString::fromStdString(o.geometry);
        m["parentId"] = QString::fromStdString(o.parent_id);
        m["x"]  = o.transform.position.x;
        m["y"]  = o.transform.position.y;
        m["z"]  = o.transform.position.z;
        m["rx"] = o.transform.rotation_deg.x;
        m["ry"] = o.transform.rotation_deg.y;
        m["rz"] = o.transform.rotation_deg.z;
        /* The stored XYZ degrees are converted once, here, into the
           quaternion the QML node binds to `rotation` — same value
           the core's world matrices use, so preview and effects can
           never disagree about an object's orientation. */
        const Quat q = RotationQuat(o.transform.rotation_deg);
        m["qw"] = q.w;
        m["qx"] = q.x;
        m["qy"] = q.y;
        m["qz"] = q.z;
        m["sx"] = o.transform.scale.x;
        m["sy"] = o.transform.scale.y;
        m["sz"] = o.transform.scale.z;
        m["dx"] = o.size_m.x;
        m["dy"] = o.size_m.y;
        m["dz"] = o.size_m.z;
        /* Resolved body size (size_m with per-axis canonical
           fallback) — the renderer consumes this so the "0 axis =
           canonical" contract lives in the core, not in QML. */
        const Vec3 body = ResolvedBodySize(o);
        m["bx"] = body.x;
        m["by"] = body.y;
        m["bz"] = body.z;
        m["visible"]  = o.visible;
        m["verified"] = o.verified;
        m["emitters"] = (int)o.emitters.size();

        m["bound"] = BoundStatusFor(doc, adapter, o);
        /* Editor fields: owning root instance + its lock state —
           same values the SceneObjectModel roles publish. */
        const std::string inst = o.id.substr(0, o.id.find('/'));
        m["instancePath"] = QString::fromStdString(inst);
        const auto sit = workspace.device_settings.find(inst);
        m["locked"] = sit != workspace.device_settings.end()
                   && sit->second.locked;
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

    /* While an effect frame exists it owns the preview — same colors
       the live push writes, brightness-scaled like the output path. */
    const auto fit = frame.find(owner->id);
    const bool has_frame = (fit != frame.end());

    for(size_t i = 0; i < owner->emitters.size(); i++)
    {
        const Emitter& e = owner->emitters[i];
        SceneColor c = (has_frame && i < fit->second.size())
                     ? fit->second[i]
                     : EmitterColor(doc, owner->id, (int)i);
        QVariantMap m;
        m["x"] = e.local_pos.x;
        m["y"] = e.local_pos.y;
        m["z"] = e.local_pos.z;
        m["c"] = Hex(ScaleScene(c, doc.brightness));
        m["i"] = (int)i;
        out.push_back(m);
    }
    return out;
}

QVariantList SceneBridge::emitterColorsOf(const QString& objectId) const
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

    const auto fit = frame.find(owner->id);
    const bool has_frame = (fit != frame.end());

    out.reserve((int)owner->emitters.size());
    for(size_t i = 0; i < owner->emitters.size(); i++)
    {
        const SceneColor c = (has_frame && i < fit->second.size())
                           ? fit->second[i]
                           : EmitterColor(doc, owner->id, (int)i);
        out.push_back(Hex(ScaleScene(c, doc.brightness)));
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
    /* Editor selection follows the click: a resolved object id
       maps to its owning root instance. */
    if(objectId.isEmpty())
    {
        editor.ClearSelection();
    }
    else
    {
        editor.SetSelection({ objectId.toStdString() });
    }
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
    /* Live output is a runtime switch — it is never persisted as
       enabled (output.live_on_startup is a JSON-edited preference). */
    if(live_output == on)
    {
        return;
    }
    live_output = on;
    emit liveChanged();
    if(on)
    {
        schedulePush();
    }
}

void SceneBridge::pausePushes()
{
    /* Called on the probe's worker thread: setLive must run on the
       GUI thread so liveChanged/property notifies stay there. */
    if(QThread::currentThread() == thread())
    {
        probe_was_live = live_output;
        setLive(false);
    }
    else
    {
        QMetaObject::invokeMethod(this, [this]()
        {
            probe_was_live = live_output;
            setLive(false);
        }, Qt::BlockingQueuedConnection);
    }

    /* Once live_output is false no new push can start — schedulePush,
       scheduleLane, pushLive, and pushLiveAll all gate on it. Holding
       both lane mutexes for the probe's duration drains in-flight
       workers and blocks any straggler that slipped the gate. */
    probe_lane_locks[0] = std::make_unique<std::unique_lock<QMutex>>(io_mutex);
    probe_lane_locks[1] = std::make_unique<std::unique_lock<QMutex>>(fast_io_mutex);
}

void SceneBridge::resumePushes()
{
    probe_lane_locks[0].reset();
    probe_lane_locks[1].reset();

    const bool restore = probe_was_live;
    if(QThread::currentThread() == thread())
    {
        setLive(restore);
    }
    else
    {
        QMetaObject::invokeMethod(this, [this, restore]() { setLive(restore); },
                                  Qt::QueuedConnection);
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

void SceneBridge::undo()
{
    /* An active gesture's snapshot predates the stack op — cancel
       it (restores previewed transforms into the workspace) before
       applyEdit rewrites the workspace underneath it. */
    const bool had = editor.GestureActive();
    const std::set<std::string> gids = editor.GestureIds();
    editor.Cancel();
    undo_stack->undo();
    /* The stack op may not re-resolve: an empty stack is a no-op and
       overlay commands (color/emitter/brightness) mutate doc fields
       directly. If a gesture was just cancelled, doc would otherwise
       keep the last previewed pose — the gesture is dead, so no
       further preview would heal it. Re-resolve the restored
       workspace; the one extra resolve in the already-adopted case
       is cheap. */
    if(had)
    {
        previewAdopt(gids);
    }
    PruneSelection();
    emit undoChanged();
}

void SceneBridge::redo()
{
    /* Same gesture-cancel + re-resolve reasoning as undo(). */
    const bool had = editor.GestureActive();
    const std::set<std::string> gids = editor.GestureIds();
    editor.Cancel();
    undo_stack->redo();
    if(had)
    {
        previewAdopt(gids);
    }
    PruneSelection();
    emit undoChanged();
}

void SceneBridge::PruneSelection()
{
    /* After edits that add/remove/rename instances (undo of a group
       or delete, doc swaps), drop selection ids the workspace no
       longer has so selectedInstances never reports phantoms.
       SetSelection re-filters through Exists — copy first since it
       clears the very vector Selection() returns. */
    const std::vector<std::string> keep = editor.Selection();
    const std::set<std::string> before_set(keep.begin(), keep.end());
    editor.SetSelection(keep);
    const std::set<std::string> after_set(editor.Selection().begin(),
                                        editor.Selection().end());

    bool changed = (before_set != after_set);
    /* `selected` holds the raw object id select() stored — keep it
       while its owning instance stays selected so sub-object
       granularity survives undo/redo; otherwise fall back to the
       primary instance id. */
    const std::string sel_inst =
        EditorController::InstanceOf(selected.toStdString());
    if(sel_inst.empty() || !editor.IsSelected(sel_inst))
    {
        const QString primary =
            QString::fromStdString(editor.PrimarySelection());
        if(selected != primary)
        {
            selected = primary;
            changed  = true;
        }
    }
    if(changed)
    {
        emit selectionChanged();
    }
}

void SceneBridge::refreshDevices()
{
    /* io_mutex + fast_io_mutex keep push workers on both lanes from
       touching the controller list mid-refresh. */
    {
        QMutexLocker lock(&io_mutex);
        QMutexLocker lock_fast(&fast_io_mutex);
        adapter.Refresh(doc);
    }
    rebuildMatrixLayouts();
    rebuildKeyLookup();
    if(obj_model != nullptr)
    {
        /* Covers both hardware-refresh bound changes and whole-doc
           swaps (ApplyWorkspace/resetScene reach here). */
        obj_model->ResetFrom();
    }
    emit sceneChanged();
}

/*---------------------------------------------------------*\
|| Workspace persistence                                    ||
||                                                           ||
||   studio.json under <OpenRGB config>/DesktopLighting-    ||
||   Studio is authoritative. The old host-settings blob    ||
||   is a one-time migration source; after the move the     ||
||   file store is exclusive — SetSettings is never called  ||
||   again for workspace data.                              ||
\*---------------------------------------------------------*/
StudioDocument SceneBridge::CurrentWorkspace() const
{
    /* Compact authoring state is authoritative — the resolved scene
       (doc) only contributes runtime state (colors, effect,
       brightness) that authoring doesn't carry. */
    StudioDocument w = workspace;
    w.meta = meta;
    if(w.meta.name.empty())
    {
        w.meta.name = doc.name.empty() ? std::string("My desk")
                                       : doc.name;
    }
    w.inputs.audio        = audio_on;
    w.inputs.keys         = key_on;
    w.inputs.screen       = screen_on;
    w.inputs.screen_index = screen_index;
    w.inputs.sens_pct     = audio_sens_pct;
    w.inputs.decay_pct    = ripple_decay_pct;
    w.object_colors  = doc.object_colors;
    w.emitter_colors = doc.emitter_colors;
    w.brightness     = doc.brightness;
    w.effect         = doc.effect;
    return w;
}

void SceneBridge::ApplyWorkspace(const StudioDocument& w)
{
    /* A saved workspace carries its effect state — loading restores
       the preset and resumes playback if it was playing. Live output
       is deliberately not touched here; it is a runtime switch.
       w.scene is the already-resolved document (the caller resolved
       it against the registry — resolution failure never reaches
       here). */
    setPlaying(false);
    /* Any live gesture's snapshot belongs to the outgoing document —
       cancel it (restores preview state into the old workspace)
       before the swap, then drop the selection: ids in it belong to
       the old doc. */
    editor.Cancel();
    workspace = w;
    doc       = w.scene;
    doc.name  = w.meta.name;
    meta      = w.meta;
    emit cameraChanged();   /* loaded prefs replace the live pose */

    editor.ClearSelection();
    if(!selected.isEmpty())
    {
        selected.clear();
    }
    emit selectionChanged();

    audio_sens_pct   = w.inputs.sens_pct;
    ripple_decay_pct = w.inputs.decay_pct;
    setScreenIndex(w.inputs.screen_index);
    setAudioInput(w.inputs.audio);
    setKeyInput(w.inputs.keys);
    setScreenInput(w.inputs.screen);
    emit inputsChanged();

    frame.clear();
    rebuildEffect();
    emit presetChanged();
    emit effectParamsChanged();
    undo_stack->clear();
    emit undoChanged();
    refreshDevices();
    if(doc.effect.playing)
    {
        setPlaying(true);
    }
    if(live_output)
    {
        schedulePush();
    }
}

void SceneBridge::ReloadPresets()
{
    /* Packaged defaults underneath the file layer — a missing or
       removed type file falls back to the shipped definition, so
       the default desk is always recoverable. Bad files report
       errors but never block the rest of the library. */
    registry.SetDefaults(DefaultDevicePresets());
    registry.ClearFiles();
    if(store != nullptr)
    {
        std::vector<std::string> errs;
        registry.LoadDirectory(store->PresetDir().toStdString(), &errs);
        if(!errs.empty())
        {
            emit statusMessage(QStringLiteral("presets: %1")
                .arg(QString::fromStdString(errs.front())));
        }
    }
}

bool SceneBridge::LoadWorkspace()
{
    if(!store->DocumentExists())
    {
        setStatus(QStringLiteral("no saved workspace — using default desk"));
        return false;
    }
    StudioDocument w;
    QString err, warns;
    if(!store->Load(&w, &err, &warns))
    {
        /* A rejected candidate never touches the active scene,
           inputs, or live-output state. */
        setStatus(QStringLiteral("studio.json rejected: %1").arg(err));
        return false;
    }
    /* The registry must load AFTER Load: a v1/v2 file migrates in
       place inside it, and InstallTypes lands the extracted
       *.device.json files that the compact doc's device types
       reference. Loading the registry first would resolve the
       just-migrated workspace against a stale library and fail. */
    ReloadPresets();
    SceneDocument resolved;
    std::vector<std::string> rerrs;
    if(!ResolveScene(w, registry, resolved, &rerrs))
    {
        /* Resolution failure (missing/mismatched types, dangling
           references) leaves the current scene active too. */
        setStatus(QStringLiteral("workspace resolve failed: %1")
            .arg(QString::fromStdString(
                rerrs.empty() ? "unknown" : rerrs.front())));
        return false;
    }
    w.scene = resolved;
    ApplyWorkspace(w);
    store->SetClean();
    if(!warns.isEmpty())
    {
        emit statusMessage(QStringLiteral("workspace: %1").arg(warns));
    }
    setStatus(QStringLiteral("loaded %1 (%2 objects)")
                  .arg(store->DocumentPath()).arg((int)doc.objects.size()));
    return true;
}

void SceneBridge::markDirty()
{
    if(store != nullptr)
    {
        store->MarkDirty();
    }
}

/*---------------------------------------------------------*\
|| Editor plumbing (M2)                                   ||
||                                                           ||
||   The Qt-free EditorController mutates `workspace`     ||
||   (the authoring document) and returns EditorEdit      ||
||   records. This side resolves the workspace into the   ||
||   runtime scene, refreshes the presentation model and  ||
||   key lookup, dirties on COMMIT only, and adapts the   ||
||   records onto the QUndoStack.                         ||
\*---------------------------------------------------------*/
QObject* SceneBridge::objectModel() const
{
    return obj_model;
}

QVariantList SceneBridge::selectedInstances() const
{
    QVariantList out;
    for(const std::string& id : editor.Selection())
    {
        out.push_back(QString::fromStdString(id));
    }
    return out;
}

void SceneBridge::SyncWorkspace()
{
    /* Same runtime overlay CurrentWorkspace() applies — the
       workspace the controller edits must already carry the live
       colors/effect/brightness so a paint made since the last
       save is never dropped by a transform edit's re-resolve. */
    workspace.meta                = meta;
    workspace.inputs.audio        = audio_on;
    workspace.inputs.keys         = key_on;
    workspace.inputs.screen       = screen_on;
    workspace.inputs.screen_index = screen_index;
    workspace.inputs.sens_pct     = audio_sens_pct;
    workspace.inputs.decay_pct    = ripple_decay_pct;
    workspace.object_colors       = doc.object_colors;
    workspace.emitter_colors      = doc.emitter_colors;
    workspace.brightness          = doc.brightness;
    workspace.effect              = doc.effect;
}

bool SceneBridge::ResolveWorkspace(SceneDocument& out)
{
    std::vector<std::string> errs;
    if(!ResolveScene(workspace, registry, out, &errs))
    {
        setStatus(QStringLiteral("edit resolve failed: %1")
            .arg(QString::fromStdString(
                errs.empty() ? "unknown" : errs.front())));
        return false;
    }
    return true;
}

void SceneBridge::AdoptResolved(const SceneDocument& r, const EditorEdit& e)
{
    doc      = r;
    doc.name = workspace.meta.name;
    rebuildMatrixLayouts();
    rebuildKeyLookup();      /* moved keyboards ripple from the new pos */
    if(obj_model != nullptr)
    {
        if(e.TransformsOnly())
        {
            /* Granular path — drag delegates stay alive; only the
               touched instance rows re-read. */
            obj_model->UpdateTransforms(e.TransformIds());
        }
        else
        {
            obj_model->ResetFrom();
            emit sceneChanged();
        }
    }
    else if(!e.TransformsOnly())
    {
        emit sceneChanged();
    }
}

void SceneBridge::applyEdit(const EditorEdit& e, bool reverse)
{
    SyncWorkspace();
    if(reverse)
    {
        RevertEditorEdit(workspace, e);
    }
    else
    {
        ApplyEditorEdit(workspace, e);
    }
    SceneDocument resolved;
    if(!ResolveWorkspace(resolved))
    {
        /* Should not happen for controller-produced edits — put
           the workspace back the way it was so the active scene
           stays consistent. */
        if(reverse)
        {
            ApplyEditorEdit(workspace, e);
        }
        else
        {
            RevertEditorEdit(workspace, e);
        }
        return;
    }
    AdoptResolved(resolved, e);
    markDirty();
}

void SceneBridge::commitEdit(EditorEdit&& e)
{
    if(e.Empty())
    {
        return;
    }
    SceneDocument resolved;
    if(!ResolveWorkspace(resolved))
    {
        /* The controller already mutated `workspace` — roll the
           edit back so document and scene stay consistent, then
           re-adopt so `doc` doesn't keep a stale preview state.
           The revert restores the document but not the selection —
           prune it so a rolled-back op (e.g. a failed Group) leaves
           no phantom ids selected. */
        RevertEditorEdit(workspace, e);
        SceneDocument back;
        if(ResolveWorkspace(back))
        {
            AdoptResolved(back, e);
        }
        PruneSelection();
        return;
    }
    AdoptResolved(resolved, e);
    markDirty();
    undo_stack->push(new SceneEditCommand(this, std::move(e),
                                        /*already_applied*/ true));
    emit undoChanged();
}

void SceneBridge::previewAdopt(const std::set<std::string>& ids)
{
    /* Gesture preview: adopt the re-resolved scene and update the
       touched rows — never markDirty, never an undo record.
       SyncWorkspace runs first so a runtime-overlay edit made
       mid-gesture (paint, brightness, effect) survives the
       re-resolve instead of being silently dropped — it only
       touches non-devices sections, so previewed transforms are
       never clobbered. */
    SyncWorkspace();
    SceneDocument resolved;
    if(!ResolveWorkspace(resolved))
    {
        return;
    }
    doc      = resolved;
    doc.name = workspace.meta.name;
    rebuildMatrixLayouts();
    rebuildKeyLookup();
    if(obj_model != nullptr)
    {
        obj_model->UpdateTransforms(ids);
    }
}

/*---------------------------------------------------------*\
|| Editor slots — selection + gestures + discrete ops     ||
\*---------------------------------------------------------*/
void SceneBridge::selectInstance(const QString& id, bool additive)
{
    editor.Select(id.toStdString(), additive);
    const QString primary =
        QString::fromStdString(editor.PrimarySelection());
    if(selected != primary)
    {
        selected = primary;
    }
    emit selectionChanged();
}

void SceneBridge::clearEditorSelection()
{
    editor.ClearSelection();
    if(!selected.isEmpty())
    {
        selected.clear();
    }
    emit selectionChanged();
}

void SceneBridge::beginTransformGesture()
{
    SyncWorkspace();
    editor.BeginTransform();
}

void SceneBridge::updateTransformGesture(double dx, double dy, double dz,
                                         int plane, bool snap)
{
    if(!editor.GestureActive())
    {
        return;
    }
    /* Reject out-of-range plane values instead of silently
       treating them as Free. */
    if(plane < (int)EditPlane::Free || plane > (int)EditPlane::SideYZ)
    {
        return;
    }
    editor.PreviewTranslate({ (float)dx, (float)dy, (float)dz },
                            (EditPlane)plane, snap);
    previewAdopt(editor.GestureIds());
}

void SceneBridge::updateRotateGesture(double degrees, bool snap)
{
    updateRotateGestureAxis(0.0, 1.0, 0.0, degrees, snap);
}

void SceneBridge::updateRotateGestureAxis(double ax, double ay, double az,
                                          double degrees, bool snap)
{
    if(!editor.GestureActive())
    {
        return;
    }
    editor.PreviewRotate({ (float)ax, (float)ay, (float)az },
                         (float)degrees, snap);
    previewAdopt(editor.GestureIds());
}

void SceneBridge::commitTransformGesture()
{
    if(!editor.GestureActive())
    {
        return;
    }
    /* A runtime-overlay edit may have landed since the last
       preview — sync before Commit so the resolve inside
       commitEdit sees it. Only non-devices sections are
       touched, so the gesture's transform diff is unaffected.
       (commitEdit itself must NOT sync — discrete ops mutate
       workspace first, and a sync there would clobber e.g.
       rename-re-keyed color maps with stale doc state.) */
    SyncWorkspace();
    std::optional<EditorEdit> e = editor.Commit();
    if(e.has_value())
    {
        commitEdit(std::move(*e));
    }
}

void SceneBridge::cancelTransformGesture()
{
    if(!editor.GestureActive())
    {
        return;
    }
    const std::set<std::string> ids = editor.GestureIds();
    editor.Cancel();
    previewAdopt(ids);
}

bool SceneBridge::setInstancePosition(const QString& id,
                                      double x, double y, double z)
{
    SyncWorkspace();
    std::optional<EditorEdit> e =
        editor.SetPosition(id.toStdString(),
                           { (float)x, (float)y, (float)z });
    if(!e.has_value())
    {
        return false;
    }
    commitEdit(std::move(*e));
    return true;
}

bool SceneBridge::setInstanceRotation(const QString& id,
                                      double rx, double ry, double rz)
{
    SyncWorkspace();
    std::optional<EditorEdit> e =
        editor.SetRotation(id.toStdString(),
                           { (float)rx, (float)ry, (float)rz });
    if(!e.has_value())
    {
        return false;
    }
    commitEdit(std::move(*e));
    return true;
}

bool SceneBridge::renameInstance(const QString& id, const QString& newId)
{
    SyncWorkspace();
    std::optional<EditorEdit> e =
        editor.Rename(id.toStdString(), newId.toStdString());
    if(!e.has_value())
    {
        const std::string& why = editor.LastError();
        setStatus(why.empty()
            ? QStringLiteral("rename rejected — check the new id")
            : QString::fromStdString(why));
        return false;
    }
    commitEdit(std::move(*e));
    selected = QString::fromStdString(editor.PrimarySelection());
    emit selectionChanged();
    return true;
}

void SceneBridge::setInstanceVisible(const QString& id, bool on)
{
    SyncWorkspace();
    std::optional<EditorEdit> e =
        editor.SetVisible(id.toStdString(), on);
    if(e.has_value())
    {
        commitEdit(std::move(*e));
    }
    else if(!editor.LastError().empty())
    {
        setStatus(QString::fromStdString(editor.LastError()));
    }
}

void SceneBridge::setInstanceLocked(const QString& id, bool on)
{
    SyncWorkspace();
    std::optional<EditorEdit> e =
        editor.SetLocked(id.toStdString(), on);
    if(e.has_value())
    {
        commitEdit(std::move(*e));
    }
    else if(!editor.LastError().empty())
    {
        setStatus(QString::fromStdString(editor.LastError()));
    }
}

void SceneBridge::groupSelected()
{
    SyncWorkspace();
    std::optional<EditorEdit> e = editor.Group();
    if(e.has_value())
    {
        commitEdit(std::move(*e));
        selected = QString::fromStdString(editor.PrimarySelection());
        emit selectionChanged();
    }
    else if(!editor.LastError().empty())
    {
        setStatus(QString::fromStdString(editor.LastError()));
    }
}

void SceneBridge::ungroupSelected()
{
    SyncWorkspace();
    std::optional<EditorEdit> e = editor.Ungroup();
    if(e.has_value())
    {
        commitEdit(std::move(*e));
        selected = QString::fromStdString(editor.PrimarySelection());
        emit selectionChanged();
    }
    else if(!editor.LastError().empty())
    {
        setStatus(QString::fromStdString(editor.LastError()));
    }
}

void SceneBridge::deleteSelected()
{
    SyncWorkspace();
    std::optional<EditorEdit> e = editor.DeleteSelected();
    if(e.has_value())
    {
        commitEdit(std::move(*e));
        selected = QString::fromStdString(editor.PrimarySelection());
        emit selectionChanged();
    }
    else if(!editor.LastError().empty())
    {
        setStatus(QString::fromStdString(editor.LastError()));
    }
}

void SceneBridge::duplicateMirrored()
{
    SyncWorkspace();
    std::optional<EditorEdit> e = editor.DuplicateMirrored();
    if(e.has_value())
    {
        commitEdit(std::move(*e));
        selected = QString::fromStdString(editor.PrimarySelection());
        emit selectionChanged();
    }
    else if(!editor.LastError().empty())
    {
        setStatus(QString::fromStdString(editor.LastError()));
    }
}

void SceneBridge::alignSelected(int axis, int mode)
{
    SyncWorkspace();
    std::optional<EditorEdit> e = editor.Align(axis, mode);
    if(e.has_value())
    {
        commitEdit(std::move(*e));
    }
    else if(!editor.LastError().empty())
    {
        setStatus(QString::fromStdString(editor.LastError()));
    }
}

void SceneBridge::distributeSelected(int axis)
{
    SyncWorkspace();
    std::optional<EditorEdit> e = editor.Distribute(axis);
    if(e.has_value())
    {
        commitEdit(std::move(*e));
    }
    else if(!editor.LastError().empty())
    {
        setStatus(QString::fromStdString(editor.LastError()));
    }
}

QVariantMap SceneBridge::instanceState(const QString& id) const
{
    QVariantMap m;
    const std::string iid = EditorController::InstanceOf(id.toStdString());
    const auto it = workspace.devices.find(iid);
    if(it == workspace.devices.end())
    {
        return m;
    }
    const DeviceInstance& d = it->second;
    m["id"]   = QString::fromStdString(iid);
    m["type"] = QString::fromStdString(d.type);
    m["x"]  = d.position.x;
    m["y"]  = d.position.y;
    m["z"]  = d.position.z;
    m["rx"] = d.rotation_deg.x;
    m["ry"] = d.rotation_deg.y;
    m["rz"] = d.rotation_deg.z;
    const auto sit = workspace.device_settings.find(iid);
    m["visible"] = (sit == workspace.device_settings.end())
                 || sit->second.visible;
    m["locked"]  = (sit != workspace.device_settings.end())
                 && sit->second.locked;
    return m;
}

QVariantMap SceneBridge::cameraState() const
{
    QVariantMap m;
    m["view"]       = QString::fromStdString(meta.camera.view);
    m["projection"] = QString::fromStdString(meta.camera.projection);
    m["tx"]       = meta.camera.target.x;
    m["ty"]       = meta.camera.target.y;
    m["tz"]       = meta.camera.target.z;
    m["yaw"]      = meta.camera.yaw_deg;
    m["pitch"]    = meta.camera.pitch_deg;
    m["distance"] = meta.camera.distance;
    m["span"]     = meta.camera.span;
    return m;
}

void SceneBridge::setCameraState(const QVariantMap& state)
{
    /* Camera prefs ride the dirty/autosave path — final pose lands
       in studio.json without touching the undo stack. */
    CameraPrefs& c = meta.camera;
    if(state.contains("view"))
    {
        const QString v = state["view"].toString();
        if(v == "desk" || v == "top" || v == "front"
           || v == "case" || v == "free")
        {
            c.view = v.toStdString();
        }
    }
    if(state.contains("projection"))
    {
        const QString v = state["projection"].toString();
        if(v == "orthographic" || v == "perspective")
        {
            c.projection = v.toStdString();
        }
    }
    auto num = [&state](const char* k, float& dst)
    {
        if(state.contains(k))
        {
            bool ok = false;
            const double v = state[k].toDouble(&ok);
            if(ok && std::isfinite(v))
            {
                dst = (float)v;
            }
        }
    };
    num("tx", c.target.x);
    num("ty", c.target.y);
    num("tz", c.target.z);
    num("yaw",   c.yaw_deg);
    num("pitch", c.pitch_deg);
    num("distance", c.distance);
    num("span",     c.span);
    if(c.distance <= 0.0f) { c.distance = 1.21f; }
    if(c.span     <= 0.0f) { c.span     = 0.9f;  }
    markDirty();
    emit cameraChanged();
}

bool SceneBridge::dirty() const
{
    return store != nullptr && store->dirty();
}

QString SceneBridge::workspaceDir() const
{
    return store != nullptr ? store->WorkspaceDir() : QString();
}

QString SceneBridge::documentPath() const
{
    return store != nullptr ? store->DocumentPath() : QString();
}

bool SceneBridge::hasRecovery() const
{
    return store != nullptr && store->HasRecovery();
}

bool SceneBridge::saveScene()
{
    if(api == nullptr || store == nullptr)
    {
        setStatus(QStringLiteral("plugin API unavailable — cannot save"));
        return false;
    }
    QString err;
    if(!store->Save(CurrentWorkspace(), &err))
    {
        /* Save errors stay visible — never report a save that did
           not land. */
        setStatus(QStringLiteral("save failed: %1").arg(err));
        return false;
    }
    setStatus(QStringLiteral("saved %1 (%2 objects)")
                  .arg(store->DocumentPath()).arg((int)doc.objects.size()));
    return true;
}

bool SceneBridge::saveSceneAs(const QString& path)
{
    if(api == nullptr || store == nullptr || path.isEmpty())
    {
        return false;
    }
    QString err;
    if(!store->SaveAs(CurrentWorkspace(), path, &err))
    {
        setStatus(QStringLiteral("save failed: %1").arg(err));
        return false;
    }
    setStatus(QStringLiteral("saved copy to %1").arg(path));
    return true;
}

bool SceneBridge::loadScene()
{
    if(api == nullptr || store == nullptr)
    {
        return false;
    }
    QString err;
    if(!store->EnsureWorkspaceDir(&err))
    {
        setStatus(QStringLiteral("workspace unavailable: %1").arg(err));
        return false;
    }

    /* One-time migration from the host-settings blob. The store owns
       the ordering (backup → validate → save → marker); a transient
       write failure leaves the marker unset so the move is retried
       next launch rather than orphaning the old scene. */
    {
        const nlohmann::json legacy = api->GetSettings("DesktopLightingStudio");
        QString detail;
        switch(store->RunLegacyMigration(legacy, &detail))
        {
        case ConfigStore::MigrationResult::Migrated:
            setStatus(QStringLiteral("migrated settings to %1")
                          .arg(store->DocumentPath()));
            break;
        case ConfigStore::MigrationResult::Invalid:
            setStatus(QStringLiteral("legacy settings failed migration: %1")
                          .arg(detail));
            break;
        case ConfigStore::MigrationResult::Failed:
            setStatus(QStringLiteral("migration deferred (will retry): %1")
                          .arg(detail));
            break;
        case ConfigStore::MigrationResult::NotNeeded:
            break;
        }
    }

    const bool loaded = LoadWorkspace();
    if(!startup_load_done)
    {
        startup_load_done = true;
        /* Live output is off on first launch unless the user opted in
           via output.live_on_startup — default false, and neither
           migration nor imported documents may set it. */
        if(meta.live_on_startup)
        {
            setLive(true);
        }
    }
    if(store->HasRecovery())
    {
        emit recoveryAvailable();
    }
    return loaded;
}

bool SceneBridge::reloadScene()
{
    /* Reload intent kills any live gesture — its snapshot belongs to
       the document about to be replaced (or kept, on failure — the
       cancel restores it either way). */
    editor.Cancel();
    if(api == nullptr || store == nullptr)
    {
        return false;
    }
    return LoadWorkspace();
}

bool SceneBridge::restoreBackup()
{
    editor.Cancel();
    if(store == nullptr || !QFileInfo::exists(store->BackupPath()))
    {
        setStatus(QStringLiteral("no backup yet"));
        return false;
    }
    StudioDocument w;
    QString err;
    if(!store->LoadFile(store->BackupPath(), &w, &err))
    {
        setStatus(QStringLiteral("backup invalid: %1").arg(err));
        return false;
    }
    ReloadPresets();
    SceneDocument resolved;
    if(!ResolveScene(w, registry, resolved, nullptr))
    {
        setStatus(QStringLiteral("backup resolve failed — scene kept"));
        return false;
    }
    w.scene = resolved;
    ApplyWorkspace(w);
    /* The restored doc differs from studio.json until saved. */
    store->MarkDirty();
    setStatus(QStringLiteral("restored backup — save to make it active"));
    return true;
}

bool SceneBridge::recoverAutosave()
{
    editor.Cancel();
    StudioDocument w;
    QString err;
    if(store == nullptr || !store->RecoverAutosave(&w, &err))
    {
        setStatus(QStringLiteral("autosave invalid: %1").arg(err));
        return false;
    }
    ReloadPresets();
    SceneDocument resolved;
    if(!ResolveScene(w, registry, resolved, nullptr))
    {
        setStatus(QStringLiteral("autosave resolve failed — scene kept"));
        return false;
    }
    w.scene = resolved;
    ApplyWorkspace(w);
    store->DiscardRecovery();
    store->MarkDirty();   /* recovered edits are unsaved */
    setStatus(QStringLiteral("recovered autosaved changes"));
    return true;
}

void SceneBridge::discardRecovery()
{
    if(store != nullptr)
    {
        store->DiscardRecovery();
    }
}

void SceneBridge::resetScene()
{
    setPlaying(false);
    /* Same doc-swap isolation as ApplyWorkspace: cancel the gesture
       while the old workspace is live, then drop the selection. */
    editor.Cancel();
    editor.ClearSelection();
    if(!selected.isEmpty())
    {
        selected.clear();
    }
    emit selectionChanged();
    workspace = BuildDefaultWorkspace();
    SceneDocument resolved;
    doc = ResolveScene(workspace, registry, resolved, nullptr)
        ? resolved : BuildDefaultDesk();
    doc.name = workspace.meta.name;
    frame.clear();
    engine.SetLayers({});
    emit presetChanged();
    emit effectParamsChanged();
    undo_stack->clear();
    emit undoChanged();
    refreshDevices();
    markDirty();
    setStatus(QStringLiteral("scene reset to default desk"));
}

/*---------------------------------------------------------*\
|| Core ops (undo commands call these)                       |
\*---------------------------------------------------------*/
/*---------------------------------------------------------*\
||| Stage 2 — effect playback                                |
|||                                                           |
|||   The engine is pure: the bridge owns time (play_t) and  |
|||   evaluates on the UI thread each tick (~300 emitters).  |
|||   Live pushes run on one worker with newest-frame        |
|||   coalescing — a slow wireless group can never build a   |
|||   backlog or freeze the editor.                          |
\*---------------------------------------------------------*/
int SceneBridge::effectSpeedPct() const     { return (int)std::lround(doc.effect.speed * 100.0f); }
int SceneBridge::effectIntensityPct() const { return (int)std::lround(doc.effect.intensity * 100.0f); }

QVariantList SceneBridge::presetList() const
{
    QVariantList out;
    for(const PresetInfo& p : PresetList())
    {
        QVariantMap m;
        m["id"]          = QString::fromStdString(p.id);
        m["name"]        = QString::fromStdString(p.name);
        m["description"] = QString::fromStdString(p.description);
        out.push_back(m);
    }
    return out;
}

void SceneBridge::rebuildEffect()
{
    std::vector<EffectLayer> layers;
    if(!doc.effect.preset.empty())
    {
        layers = BuildPreset(doc.effect.preset, doc.effect.seed);
        ApplyGlobalParams(layers, doc.effect.speed, doc.effect.intensity);
        /* User-adjustable ripple decay — scales the age decay on
           ripple layers (Stage 3 reactive presets). */
        const float decay_mult = ripple_decay_pct / 100.0f;
        for(EffectLayer& L : layers)
        {
            if(L.primitive == "ripple")
            {
                L.density *= decay_mult;
            }
        }
        /* Dead-target guard: a target naming no emitter-bearing
           object leaves the layer silently inert — surface it once
           per rebuild instead of debugging visuals. (Emitter groups
           equal their object id, so id/geometry cover all match
           terms; matrix_map emitters are runtime-generated.) */
        for(const EffectLayer& L : layers)
        {
            for(const std::string& t : L.targets)
            {
                bool hit = false;
                for(const SceneObject& o : doc.objects)
                {
                    const bool emits = !o.emitters.empty()
                                       || o.layout == "matrix_map";
                    if(emits && (t == o.id || t == o.geometry))
                    {
                        hit = true;
                        break;
                    }
                }
                if(!hit)
                {
                    qWarning("DesktopLightingStudio: effect layer target"
                             " '%s' matches no emitter-bearing object",
                             t.c_str());
                }
            }
        }
    }
    engine.SetLayers(layers);
    /* New layers may produce an identical first frame (or an empty
       one where the static-scene push path runs) — the next tick
       must emit + push regardless of the identical-frame skip. */
    frame_sent = false;
}

void SceneBridge::playPreset(const QString& presetId)
{
    const std::string id = presetId.toStdString();
    if(FindPreset(id) == nullptr)
    {
        setStatus(QStringLiteral("unknown preset %1").arg(presetId));
        return;
    }
    if(doc.effect.preset != id)
    {
        doc.effect.preset = id;
        doc.effect.seed   = 0;
        play_t            = 0.0;
        input_bus.ClearEvents();   /* old-clock events would age wrong */
        markDirty();
        emit presetChanged();
    }
    /* A reactive preset auto-enables its input source — the toggle
       stays visible and can be switched off. */
    const PresetInfo* info = FindPreset(id);
    if(info != nullptr)
    {
        if(info->needs == "audio"  && !audio_on)
        {
            setAudioInput(true);
        }
        if(info->needs == "key"    && !key_on)
        {
            setKeyInput(true);
        }
        if(info->needs == "screen" && !screen_on)
        {
            setScreenInput(true);
        }
    }
    rebuildEffect();
    setPlaying(true);
}

void SceneBridge::setPlaying(bool on)
{
    if(playing_state == on)
    {
        return;
    }
    playing_state      = on;
    doc.effect.playing = on;
    markDirty();
    if(on)
    {
        if(engine.Empty())
        {
            rebuildEffect();
        }
        frame_sent = false;   /* first tick always repaints + pushes */
        play_clock->start();
        play_timer->start();
        tick();     /* evaluate immediately — don't wait for the timer */
    }
    else
    {
        play_timer->stop();
    }
    emit playingChanged();
}

void SceneBridge::stopEffect()
{
    setPlaying(false);
    doc.effect.preset.clear();
    markDirty();
    frame.clear();
    engine.SetLayers({});
    emit presetChanged();
    /* Return preview + hardware to the painted scene. */
    for(const SceneObject& o : doc.objects)
    {
        emit emittersChanged(QString::fromStdString(o.id));
    }
    schedulePush();
}

void SceneBridge::remix()
{
    if(doc.effect.preset.empty())
    {
        return;
    }
    /* A fresh seed re-rolls every bounded random choice in the
       preset; the seed persists with the scene, so a remix is
       reproducible. */
    doc.effect.seed = HashU32(doc.effect.seed ^ 0x5D15A5E9u) + 1u;
    markDirty();
    rebuildEffect();
    setPlaying(true);
    setStatus(QStringLiteral("remix seed %1").arg(doc.effect.seed));
    emit presetChanged();
}

void SceneBridge::setEffectSpeedPct(int pct)
{
    doc.effect.speed = qBound(10, pct, 400) / 100.0f;
    markDirty();
    rebuildEffect();
    emit effectParamsChanged();
}

void SceneBridge::setEffectIntensityPct(int pct)
{
    doc.effect.intensity = qBound(0, pct, 100) / 100.0f;
    markDirty();
    rebuildEffect();
    emit effectParamsChanged();
}

void SceneBridge::tick()
{
    if(!playing_state)
    {
        return;
    }
    if(play_clock->isValid())
    {
        play_t = play_t.load() + play_clock->nsecsElapsed() / 1e9;
    }
    play_clock->restart();

    /* Snapshot input signals; resolve key VK codes to emitter world
       positions (unmapped keys stay position-less and spawn at the
       layer's origin). */
    InputState in = input_bus.Snapshot(6.0);
    for(InputEvent& e : in.events)
    {
        if(e.source == "key" && !e.has_pos)
        {
            const auto it = key_pos.find(e.code);
            if(it != key_pos.end())
            {
                e.pos     = it->second;
                e.has_pos = true;
            }
        }
    }

    engine.Evaluate(doc, play_t.load(), frame, &in);

    /* An identical frame skips repaint + push — a static preset (or
       a silent reactive one) would otherwise re-write every device
       at the full tick rate for zero visible change. */
    if(!frame_sent || frame != last_frame)
    {
        last_frame = frame;
        frame_sent = true;
        emitFrameChanged();
        schedulePush();
    }

    /* Surface provider state transitions (listening / capture
       unavailable / off) without spamming repeats. */
    std::string ins;
    if(audio_on)  { ins += audio_in.Status(); }
    if(key_on)    { if(!ins.empty()) { ins += "  "; } ins += key_in.Status(); }
    if(screen_on) { if(!ins.empty()) { ins += "  "; } ins += screen_in->Status().toStdString(); }
    if(ins != last_input_status)
    {
        last_input_status = ins;
        if(!ins.empty())
        {
            setStatus(QString::fromStdString(ins));
        }
    }
}

void SceneBridge::emitFrameChanged()
{
    for(const auto& kv : frame)
    {
        emit emittersChanged(QString::fromStdString(kv.first));
        for(const SceneObject& o : doc.objects)
        {
            if(o.kind == ObjectKind::Linked && o.mirror_of == kv.first)
            {
                emit emittersChanged(QString::fromStdString(o.id));
            }
        }
    }
}

/* Newest-frame push: at most one worker in flight per lane plus one
   pending request. Intermediate ticks while a lane is busy collapse
   into a single follow-up push of the latest frame.

   Frame pushes are split across two lanes because one serial worker
   can't keep fast devices smooth while a 300+ ms I2C write is in
   flight: lane 1 owns I2C/SMBus bindings (and unmeasured ones — a
   first push late beats stalling the fast lane), lane 0 owns
   measured-fast USB/HID bindings. Lanes share no bus, so concurrent
   writes can't interleave mid-transaction. Static scene pushes stay
   monolithic under both mutexes. */
void SceneBridge::schedulePush()
{
    if(!live_output || api == nullptr)
    {
        return;
    }
    if(!frame.empty() && !doc.effect.preset.empty())
    {
        scheduleLane(0);
        scheduleLane(1);
        return;
    }
    if(push_in_flight.exchange(true))
    {
        push_again = true;
        return;
    }
    const SceneDocument doc_copy = doc;
    std::thread([this, doc_copy]()
    {
        std::string err;
        {
            /* Both lane mutexes: a lane worker can still be finishing
               a write when the effect just stopped. */
            QMutexLocker lock(&io_mutex);
            QMutexLocker lock_fast(&fast_io_mutex);
            err = adapter.PushAll(doc_copy, nullptr);
        }
        push_in_flight = false;
        if(push_again.exchange(false))
        {
            QMetaObject::invokeMethod(this, [this]() { schedulePush(); },
                                      Qt::QueuedConnection);
        }
        if(!err.empty() || !last_push_err.empty())
        {
            const std::string e = err;
            QMetaObject::invokeMethod(this, [this, e]()
            {
                if(e == last_push_err)
                {
                    return;
                }
                last_push_err = e;
                setStatus(e.empty() ? QStringLiteral("live output on")
                                    : QString::fromStdString(e));
            }, Qt::QueuedConnection);
        }
    }).detach();
}

void SceneBridge::scheduleLane(int lane)
{
    std::atomic<bool>& in_flight = lane_in_flight[lane];
    std::atomic<bool>& again     = lane_again[lane];
    if(in_flight.exchange(true))
    {
        again = true;
        return;
    }
    if(!live_output)
    {
        /* Re-armed after live went off (probe or user): drop the
           push instead of spawning a worker that would write late. */
        in_flight = false;
        return;
    }
    /* Most ticks find every binding still inside its pacing budget —
       don't spawn a worker for an empty sweep. Lane membership and
       due checks mirror runPushLane; the worker re-checks anyway, so
       a stale read only costs a harmless spawn, never a wrong write. */
    {
        QMutexLocker pl(&pace_mutex);
        const auto now = std::chrono::steady_clock::now();
        bool any_due = false;
        for(const DeviceBinding& b : doc.bindings)
        {
            const PushPace& p = push_pace[b.id];
            const int eff = (p.lane < 0) ? 1 : p.lane;
            if((BindingIsI2C(b.id) ? 1 : eff) == lane && p.due_after <= now)
            {
                any_due = true;
                break;
            }
        }
        if(!any_due)
        {
            in_flight = false;
            return;
        }
    }
    const SceneDocument doc_copy   = doc;
    const FrameColors   frame_copy = frame;
    std::thread([this, lane, doc_copy, frame_copy]()
    {
        runPushLane(lane, doc_copy, frame_copy);
        lane_in_flight[lane] = false;
        if(lane_again[lane].exchange(false))
        {
            QMetaObject::invokeMethod(this, [this, lane]() { scheduleLane(lane); },
                                      Qt::QueuedConnection);
        }
    }).detach();
}

bool SceneBridge::BindingIsI2C(const std::string& binding_id) const
{
    const ResolvedBinding* r = adapter.Resolution(binding_id);
    if(r == nullptr || r->status != BindingStatus::Resolved)
    {
        return false;
    }
    std::string loc = adapter.Snapshot()[r->controller_index].location;
    for(char& c : loc) { c = (char)tolower((unsigned char)c); }
    return loc.find("i2c") != std::string::npos
        || loc.find("smbus") != std::string::npos;
}

double SceneBridge::BindingMinPace(const std::string& binding_id) const
{
    /* The Lian Li wireless runtime only transmits inside its 300 ms
       poll tick — writes just enqueue a desired upload, so pushing
       faster than ~300 ms is pure churn. */
    const ResolvedBinding* r = adapter.Resolution(binding_id);
    if(r != nullptr && r->status == BindingStatus::Resolved
       && adapter.Snapshot()[r->controller_index].location.find("Wireless:") == 0)
    {
        return 280.0;
    }
    /* Fast transports may update at the ~60 fps tick rate; the
       measured-cost budget still throttles anything slower. */
    return 16.0;
}

void SceneBridge::runPushLane(int lane, const SceneDocument& dc,
                              const FrameColors& fc)
{
    using clock = std::chrono::steady_clock;
    /* Lane mutex serializes writes on this transport class;
       pace_mutex guards the shared bookkeeping map only — the slow
       PushBinding call itself stays outside it. */
    std::unique_lock<QMutex> lock(lane == 1 ? io_mutex : fast_io_mutex);
    const auto now = clock::now();
    std::string err;
    for(const DeviceBinding& b : dc.bindings)
    {
        PushPace*  pace = nullptr;
        bool       i2c  = false;
        int        eff  = 1;
        {
            QMutexLocker pl(&pace_mutex);
            PushPace& p = push_pace[b.id];
            i2c = BindingIsI2C(b.id);
            eff = (p.lane < 0) ? 1 : p.lane;
            p.min_pace_ms = BindingMinPace(b.id);
            if((i2c ? 1 : eff) == lane && p.due_after <= now)
            {
                pace = &p;   /* std::map nodes are stable */
            }
        }
        if(pace == nullptr)
        {
            continue;
        }
        const auto t0 = clock::now();
        const std::string e = adapter.PushBinding(dc, b.id, &fc);
        const auto t1 = clock::now();
        const double cost =
            std::chrono::duration<double, std::milli>(t1 - t0).count();
        {
            QMutexLocker pl(&pace_mutex);
            pace->budget_ms = std::min(750.0,
                std::max(pace->min_pace_ms, cost * 1.3));
            /* Period-based pacing: the write's own duration counts
               toward the budget, so a fast device reaches ~60 Hz
               instead of cost+33 ms. Rest after the write is still
               >= 30% of its cost, so the bus is never saturated. */
            pace->due_after = t0 + std::chrono::milliseconds((long long)pace->budget_ms);
            pace->lane      = i2c ? 1 : (cost > 80.0 ? 1 : (cost < 40.0 ? 0 : eff));
        }
        if(!e.empty() && e.find("no mapped emitters") == std::string::npos)
        {
            err += b.id + ": " + e + "\n";
        }
    }
    lock.unlock();
    if(err.empty())
    {
        return;
    }
    QMetaObject::invokeMethod(this, [this, err]()
    {
        if(err == last_push_err)
        {
            return;
        }
        last_push_err = err;
        setStatus(QString::fromStdString(err));
    }, Qt::QueuedConnection);
}

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
    markDirty();
    /* With an effect frame up, the frame owns the output — the painted
       base under it flows through the next push anyway. */
    if(frame.empty()) { pushLive(owner_id); } else { schedulePush(); }
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
    markDirty();
    if(frame.empty()) { pushLive(owner_id); } else { schedulePush(); }
}

void SceneBridge::applyBrightness(float brightness)
{
    doc.brightness = brightness;
    emit brightnessChanged();
    for(const SceneObject& o : doc.objects)
    {
        emit emittersChanged(QString::fromStdString(o.id));
    }
    markDirty();
    if(frame.empty()) { pushLiveAll(); } else { schedulePush(); }
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
        obj.emitters = layout::KeyboardMatrix(rows, cols, map, 0xFFFFFFFFu,
                                            pitch, pitch, origin, obj.id);
    }
}

/*---------------------------------------------------------*\
||| Stage 3 — reactive inputs                                |
|||                                                           |
|||   Providers write into the bus from their own threads; |
|||   tick() snapshots into the engine. Scenes stay fully  |
|||   editable with every source off — reactive primitives |
|||   simply contribute nothing on an empty InputState.    |
\*---------------------------------------------------------*/
void SceneBridge::setAudioInput(bool on)
{
    if(audio_on == on)
    {
        return;
    }
    audio_on = on;
    if(on)
    {
        audio_in.SetSensitivity(audio_sens_pct / 100.0f);
        audio_in.Start(&input_bus);
    }
    else
    {
        audio_in.Stop();
        input_bus.SetAudioLevel(0.0f);
    }
    last_input_status.clear();
    markDirty();
    emit inputsChanged();
}

void SceneBridge::setKeyInput(bool on)
{
    if(key_on == on)
    {
        return;
    }
    key_on = on;
    if(on)
    {
        key_in.Start(&input_bus);
    }
    else
    {
        key_in.Stop();
    }
    last_input_status.clear();
    markDirty();
    emit inputsChanged();
}

void SceneBridge::setScreenInput(bool on)
{
    if(screen_on == on)
    {
        return;
    }
    screen_on = on;
    if(on)
    {
        screen_in->Start(screen_index);
    }
    else
    {
        screen_in->Stop();
    }
    last_input_status.clear();
    markDirty();
    emit inputsChanged();
}

void SceneBridge::setScreenIndex(int index)
{
    if(index < 0 || index == screen_index)
    {
        return;
    }
    screen_index = index;
    if(screen_on)
    {
        screen_in->SetScreenIndex(index);
    }
    markDirty();
    emit inputsChanged();
}

void SceneBridge::setAudioSensitivityPct(int pct)
{
    audio_sens_pct = qBound(25, pct, 200);
    audio_in.SetSensitivity(audio_sens_pct / 100.0f);
    markDirty();
    emit inputsChanged();
}

void SceneBridge::setRippleDecayPct(int pct)
{
    ripple_decay_pct = qBound(50, pct, 300);
    markDirty();
    rebuildEffect();
    emit inputsChanged();
}

/* vk -> world position of that key's emitter, built from the bound
   keyboard's own LED names ("Key: Q" ...) — the hardware layout, not
   a guessed grid. Rebuilt on device refresh. */
void SceneBridge::rebuildKeyLookup()
{
    key_pos.clear();
    /* Resolved world transforms — a keyboard that moved (or whose
       parent group moved) ripples from where its keys actually are.
       Any future transform-edit path must rerun this (refreshDevices
       covers the rebuild for now). */
    const std::map<std::string, Mat4> world = ResolveWorldMatrices(doc);
    for(const SceneObject& obj : doc.objects)
    {
        if(obj.kind != ObjectKind::Device || obj.layout != "matrix_map"
           || obj.binding.empty())
        {
            continue;
        }
        for(const Emitter& e : obj.emitters)
        {
            const int vk = VkForKeyName(adapter.LEDName(obj.binding, e.address));
            if(vk >= 0)
            {
                /* First emitter wins if two LEDs share a name. */
                key_pos.emplace(vk, TransformPoint(world.at(obj.id), e.local_pos));
            }
        }
    }
}

void SceneBridge::setStatus(const QString& text)
{
    status = text;
    emit statusChanged();
    emit statusMessage(text);
}

} /* namespace studio */

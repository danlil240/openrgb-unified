/*---------------------------------------------------------*\
|| SceneBridge.cpp                                           |
||                                                           |
||   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "SceneBridge.h"

#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QThread>
#include <QTimer>
#include <QUndoCommand>
#include <QUndoStack>

#include "../scene/DefaultDesk.h"
#include "../scene/EmitterLayout.h"
#include "../scene/SceneGraph.h"
#include "../scene/SceneJson.h"
#include "../scene/SceneResolver.h"
#include "../config/ConfigStore.h"
#include "../presets/PresetBundle.h"
#include "../editor/EffectLayerModel.h"
#include "../editor/PresetListModel.h"
#include "../editor/SceneObjectModel.h"
#include "../effects/EffectJson.h"
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

    /* Type library: the packaged *.device.json files are the
       authoritative defaults underneath whatever the workspace
       presets dir holds (file types are loaded on each Reload). */
    LoadPresetDefaults();
    /* Look library: same layering for presets/effects/*.effect.json
       (defaults from the bundled qrc; file layer lands on Reload). */
    LoadEffectDefaults();

    /* Start on the default compact workspace resolved through the
       registry — resolution cannot fail on the bundled types, but
       guard anyway. */
    workspace = using_fallback_types ? BuildFallbackWorkspace()
                                     : BuildDefaultWorkspace();
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

    /* Device-type library model — snapshots PresetRegistry::List()
       and reads favorites live from `meta`. ReloadPresets (and the
       new type commands) refresh it; the initial snapshot lands
       here because the ctor's LoadPresetDefaults ran before the
       model existed. */
    preset_model = new PresetListModel(this);
    preset_model->Bind(&registry, &meta);
    preset_model->Reload();

    /* Effect-layer editing: the effective-stack model fills on the
       first rebuildEffect; the resolver lets the Qt-free controller
       materialize a named look's resolved stack on first edit
       without linking the registry itself. */
    layer_model = new EffectLayerModel(this);
    editor.SetLayerResolver(
        [](const std::string& preset_id, unsigned int seed,
           std::vector<EffectLayer>& out, void*) -> bool {
            out = BuildPreset(preset_id, seed);
            return !out.empty();
        }, nullptr);
    RefreshEffectModel();   /* initial effective stack */

    refreshDevices();
}

SceneBridge::~SceneBridge()
{
    /* Quiesce FIRST: shutting_down makes late pausePushes() calls
       fail, lane/push workers bail between bindings, and probe loops
       reading closing() abort their write cycles — the join below
       then stays bounded by at most one in-flight driver write. */
    shutting_down = true;

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

    /* Detached push workers borrow `this` (lane mutexes, adapter,
       queued completion callbacks). There is no safe "give up and
       free anyway" — a worker mid-write would dereference dead
       members — so wait for ALL of them (push_workers counts every
       spawn site, including the unflagged pushLive/pushLiveAll
       workers) plus any probe sitting between pausePushes() and
       resumePushes() (probe_active). Queued invokeMethod calls aimed
       at this object are dropped by ~QObject, so callbacks that
       arrive after the join are harmless. */
    play_timer->stop();
    for(int i = 0;
        push_workers.load() > 0 || probe_active.load() > 0;
        i++)
    {
        if(i > 0 && i % 500 == 0)
        {
            qWarning("DesktopLightingStudio: ~SceneBridge waiting on"
                     " %d push worker(s), %d probe(s) — a wedged"
                     " driver write is holding shutdown",
                     push_workers.load(), probe_active.load());
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
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
       enabled (output.live_on_startup is a JSON-edited preference).
       GUI thread only; the flag itself is atomic because
       pausePushes() also flips it from probe threads. */
    if(live_output.load() == on)
    {
        return;
    }
    live_output = on;
    /* Probes snapshot this (pausePushes): a real toggle mid-probe
       must disarm the probe's restore. */
    live_generation.fetch_add(1);
    emit liveChanged();
    if(on)
    {
        schedulePush();
        /* Live output is a frame consumer even while hidden — a
           paused-for-visibility play timer must resume so requested
           playback keeps pushing. */
        if(playing_state && !play_timer->isActive())
        {
            play_clock->restart();
            play_timer->start();
        }
    }
    else if(!preview_visible)
    {
        /* Live off + hidden = no consumer left; idle the tick until
           something re-arms it (show, live on). */
        play_timer->stop();
    }
}

bool SceneBridge::pausePushes()
{
    /* Register before checking shutting_down (seq_cst pair with the
       dtor): a probe that reads closing==false had already counted
       itself, so ~SceneBridge's drain loop must see it. The re-check
       after probe_serial covers the probe that was queued behind an
       active one while shutdown began. */
    probe_active.fetch_add(1);
    /* If the lock allocation throws the count would strand — the
       dtor's drain loop counts on it. Disarmed once the probe is
       registered; the counted exits below subtract directly. */
    struct SpawnCount { std::atomic<int>& c; bool armed = true;
        ~SpawnCount() { if(armed) c.fetch_sub(1); } } spawn{probe_active};
    if(shutting_down.load())
    {
        probe_active.fetch_sub(1);
        spawn.armed = false;
        return false;
    }
    probe_serial_lock = std::make_unique<std::unique_lock<QMutex>>(probe_serial);
    spawn.armed = false;
    if(shutting_down.load())
    {
        /* Move off the member before unlocking — see resumePushes(). */
        auto serial = std::move(probe_serial_lock);
        serial.reset();
        probe_active.fetch_sub(1);
        return false;
    }

    /* Flip live off directly — the flag is atomic, so no GUI-thread
       round-trip is needed (a BlockingQueuedConnection here
       deadlocks the probe forever when the bridge thread is inside
       ~SceneBridge). The notify is queued so liveChanged still fires
       on the bridge thread; probe_serial is held first, so a second
       probe records probe_was_live only after the first restored it —
       overlapping probes can no longer leave live stuck off. */
    /* Snapshot the toggle generation BEFORE the exchange: a
       setLive that flips the flag in between still bumps the
       generation, so resumePushes() can't restore over it. */
    probe_live_generation = live_generation.load();
    probe_was_live = live_output.exchange(false);
    if(probe_was_live)
    {
        if(QThread::currentThread() == thread())
        {
            emit liveChanged();
        }
        else
        {
            QMetaObject::invokeMethod(this, [this]() { emit liveChanged(); },
                                      Qt::QueuedConnection);
        }
    }

    /* Once live_output is false no new push can start — schedulePush,
       scheduleLane, pushLive, and pushLiveAll all gate on it. Holding
       both lane mutexes for the probe's duration drains in-flight
       workers and blocks any straggler that slipped the gate. */
    probe_lane_locks[0] = std::make_unique<std::unique_lock<QMutex>>(io_mutex);
    probe_lane_locks[1] = std::make_unique<std::unique_lock<QMutex>>(fast_io_mutex);
    return true;
}

void SceneBridge::resumePushes()
{
    /* Move the lock holders off the members BEFORE unlocking: a
       reset() on the member would store nullptr AFTER the mutex
       release inside ~unique_lock, racing the next probe's member
       assignment the instant it acquires. Moving first makes every
       member write happen-before the unlock. */
    auto lane0  = std::move(probe_lane_locks[0]);
    auto lane1  = std::move(probe_lane_locks[1]);
    auto serial = std::move(probe_serial_lock);
    lane0.reset();
    lane1.reset();

    /* Restore only when the pause itself turned live off and no
       user toggle landed during the probe — the generation must
       match the snapshot, because the flag alone can't tell "still
       paused" from a user on->off sequence (both read false; the
       old check re-enabled live against the user's last explicit
       action). The flag store MUST be synchronous here, before
       probe_serial is released: queueing the whole setLive(true)
       let a second probe acquire probe_serial and exchange() a
       still-false live_output, record probe_was_live=false, and
       leave live output off forever after both probes finished.
       The flag is atomic; only the GUI side-effects (notify,
       first push, timer re-arm) may ride the event queue. */
    const bool restore = probe_was_live && !live_output.load()
        && live_generation.load() == probe_live_generation;
    if(restore)
    {
        live_output = true;
        auto rearm = [this]()
        {
            /* Same side-effects as setLive(true) — the flag store
               already happened on the probe thread. */
            emit liveChanged();
            schedulePush();
            if(playing_state && !play_timer->isActive())
            {
                play_clock->restart();
                play_timer->start();
            }
        };
        if(QThread::currentThread() == thread())
        {
            rearm();
        }
        else
        {
            QMetaObject::invokeMethod(this, std::move(rearm),
                                      Qt::QueuedConnection);
        }
    }
    serial.reset();
    probe_active.fetch_sub(1);
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
    /* Refuse under a live transform gesture — the stack op would
       write beneath the snapshot the gesture is previewing against
       (the same refusal contract the EditorController ops use). The
       gesture survives and keeps previewing; the status hint tells
       the user why nothing happened. The QML shortcut/button gates
       catch this first — this guard is the contract for every
       caller. The layer gesture previews against a snapshot the
       same way, so it refuses too. */
    if(editor.GestureActive() || editor.LayerGestureActive())
    {
        emit statusMessage(
            QStringLiteral("undo refused — finish the drag first"));
        return;
    }
    undo_stack->undo();
    PruneSelection();
    emit undoChanged();
}

void SceneBridge::redo()
{
    /* Same mid-gesture refusal as undo(). */
    if(editor.GestureActive() || editor.LayerGestureActive())
    {
        emit statusMessage(
            QStringLiteral("redo refused — finish the drag first"));
        return;
    }
    undo_stack->redo();
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
    editor.CancelLayerGesture();
    workspace = w;
    doc       = w.scene;
    doc.name  = w.meta.name;
    meta      = w.meta;
    emit cameraChanged();      /* loaded prefs replace the live pose */
    emit renderPrefsChanged(); /* loaded render tier/bloom too */
    if(preset_model != nullptr)
    {
        preset_model->RefreshFavorites();   /* loaded ui.favorites */
    }
    emit presetLibraryChanged();

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

void SceneBridge::LoadPresetDefaults()
{
    /* The bundled presets/devices/*.device.json files are the
       authoritative type library — enumerate the qrc, parse +
       validate each, and layer them under the file layer. A bad
       packaged file is reported and skipped without aborting the
       rest. Only when NOTHING readable ships (missing qrc, every
       file failing) does the minimal C++ set stand in so the
       recovery desk can still resolve. */
    std::vector<DevicePreset> packaged;
    QStringList               bad;
    QDirIterator it(QStringLiteral(":/studio/presets/devices"),
                    { QStringLiteral("*.device.json") }, QDir::Files);
    while(it.hasNext())
    {
        const QString res = it.next();
        QFile f(res);
        if(!f.open(QIODevice::ReadOnly))
        {
            bad << res;
            continue;
        }
        const QByteArray bytes = f.readAll();
        const nlohmann::json j = nlohmann::json::parse(
            bytes.constBegin(), bytes.constEnd(), nullptr, false);
        DevicePreset p;
        std::vector<std::string> errs;
        const QString base =
            QFileInfo(res).fileName().section('.', 0, 0);
        if(j.is_discarded()
           || !DevicePresetFromJson(j, p, &errs)
           || p.id != base.toStdString())
        {
            bad << res;
            qWarning() << "packaged preset" << res << "rejected:"
                       << (errs.empty()
                               ? QStringLiteral("invalid")
                               : QString::fromStdString(errs.front()));
            continue;
        }
        packaged.push_back(p);
    }
    if(packaged.empty())
    {
        registry.SetDefaults(DefaultDevicePresets());
        using_fallback_types = true;
        setStatus(QStringLiteral(
            "packaged device presets unreadable — minimal recovery set"));
        return;
    }
    registry.SetDefaults(std::move(packaged));
    using_fallback_types = false;
    if(!bad.isEmpty())
    {
        setStatus(QStringLiteral("presets: %1 packaged file(s) invalid"
                                 " (%2)")
                      .arg(bad.size())
                      .arg(bad.first()));
    }
}

void SceneBridge::LoadEffectDefaults()
{
    /* The bundled presets/effects/*.effect.json files are the
       authoritative look library — enumerate the qrc, parse +
       validate each, and layer them under the file layer. Only
       when NOTHING readable ships does the one minimal built-in
       look stand in (see Presets.cpp's fallback). */
    std::vector<EffectDocument> packaged;
    QStringList                 bad;
    QDirIterator it(QStringLiteral(":/studio/presets/effects"),
                    { QStringLiteral("*.effect.json") }, QDir::Files);
    while(it.hasNext())
    {
        const QString res = it.next();
        QFile f(res);
        if(!f.open(QIODevice::ReadOnly))
        {
            bad << res;
            continue;
        }
        const QByteArray bytes = f.readAll();
        const nlohmann::ordered_json j = nlohmann::ordered_json::parse(
            bytes.constBegin(), bytes.constEnd(), nullptr, false);
        EffectDocument d;
        std::vector<std::string> errs;
        const QString base =
            QFileInfo(res).fileName().section('.', 0, 0);
        if(j.is_discarded()
           || !EffectDocumentFromJson(j, d, &errs)
           || d.id != base.toStdString())
        {
            bad << res;
            qWarning() << "packaged effect" << res << "rejected:"
                       << (errs.empty()
                               ? QStringLiteral("invalid")
                               : QString::fromStdString(errs.front()));
            continue;
        }
        packaged.push_back(d);
    }
    if(packaged.empty())
    {
        /* Keep whatever the lazy probe already installed (source-
           tree files in a dev tree, else the single fallback look). */
        using_fallback_effects =
            EffectLooks().Ids().size() == 1
            && EffectLooks().Find("fallback") != nullptr;
        setStatus(QStringLiteral(
            "packaged effect looks unreadable — minimal fallback"));
        return;
    }
    EffectLooks().SetDefaults(std::move(packaged));
    using_fallback_effects = false;
    if(!bad.isEmpty())
    {
        setStatus(QStringLiteral("effects: %1 packaged file(s) invalid"
                                 " (%2)")
                      .arg(bad.size())
                      .arg(bad.first()));
    }
}

void SceneBridge::ReloadPresets()
{
    /* Packaged defaults underneath the file layer — a missing or
       removed type file falls back to the shipped definition, so
       the default desk is always recoverable. Bad files report
       errors but never block the rest of the library. */
    LoadPresetDefaults();
    registry.ClearFiles();
    /* Same layering for effect looks: bundled defaults under the
       workspace's presets/effects/ files. */
    LoadEffectDefaults();
    EffectLooks().ClearFiles();
    if(store != nullptr)
    {
        std::vector<std::string> errs;
        registry.LoadDirectory(store->PresetDir().toStdString(), &errs);
        EffectLooks().LoadDirectory(store->EffectPresetDir().toStdString(),
                                    &errs);
        if(!errs.empty())
        {
            emit statusMessage(QStringLiteral("presets: %1")
                .arg(QString::fromStdString(errs.front())));
        }
    }
    /* Re-resolved stacks may differ — refresh a preset-driven
       effect (an untouched inline stack keeps user edits). When the
       named look vanished from the library entirely (its file was
       deleted), keep the last-good engine stack instead of blanking
       the desk mid-session — the file-error status already reports
       the loss. */
    const bool lost_look = doc.effect.layers.empty()
        && !doc.effect.preset.empty()
        && !EffectLooks().Contains(doc.effect.preset);
    if(lost_look)
    {
        emit statusMessage(QStringLiteral(
            "effect look '%1' no longer resolves — keeping last-good"
            " output").arg(QString::fromStdString(doc.effect.preset)));
        RefreshEffectModel();   /* model shows the now-empty resolve */
    }
    else
    {
        rebuildEffect();
    }
    /* The library panel's rows come from List() — re-snapshot so a
       reload (Reload button, landed variant file) shows it. */
    if(preset_model != nullptr)
    {
        preset_model->Reload();
    }
    emit presetLibraryChanged();
    emit presetChanged();
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

QObject* SceneBridge::presetModel() const
{
    return preset_model;
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
    if(!e.bindings.Empty())
    {
        /* The edit minted or dropped a bindings entry — re-resolve
           the adapter's bindings now or Resolution()/ZoneMatrix()/
           LEDName() can't see the change (and PushZone reports
           "unresolved: no binding") until an unrelated hardware
           refresh. Same io lock pair refreshDevices() uses; runs
           before the matrix/key rebuilds below, which read adapter
           state. applyEdit funnels through here, so undo/redo of a
           bind edit refreshes identically. */
        QMutexLocker lock(&io_mutex);
        QMutexLocker lock_fast(&fast_io_mutex);
        adapter.Refresh(doc);
    }
    rebuildMatrixLayouts();
    rebuildKeyLookup();      /* moved keyboards ripple from the new pos */
    if(e.has_effect)
    {
        /* The resolved scene carries the edited effect state
           (ResolveScene copies workspace.effect wholesale) — the
           engine rebuild makes preview + pushed output follow, and
           the layer editor's model refreshes inside. */
        rebuildEffect();
        emit presetChanged();
    }
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

QStringList SceneBridge::deletePreview() const
{
    /* Same kill set DeleteSelected() computes — read-only, so the
       tree's confirm prompt can list the children before commit. */
    QStringList out;
    const std::set<std::string> kill =
        editor.DeleteCascade(editor.Selection());
    for(const std::string& id : kill)
    {
        out << QString::fromStdString(id);
    }
    return out;
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

/*---------------------------------------------------------*\
|| Task 4.2 — device library commands.                    ||
||                                                           ||
||   Same shape as every editor op above: validate first   ||
||   (a refusal touches NOTHING — no workspace mutation,   ||
||   no file write, no output), then sync -> controller op ||
||   -> commitEdit -> resolve. Favorites are the exception ||
||   — a UI pref like camera state: dirty path, no undo.   ||
\*---------------------------------------------------------*/
void SceneBridge::addDeviceInstance(const QString& typeId,
                                    double x, double y, double z)
{
    const std::string tid = typeId.toStdString();
    if(!IsPresetId(tid))
    {
        setStatus(QStringLiteral("add refused: '%1' is not a valid type id")
                      .arg(typeId));
        return;
    }
    const DevicePreset* p = registry.Find(tid);
    if(p == nullptr)
    {
        setStatus(QStringLiteral("add refused: unknown type '%1'")
                      .arg(typeId));
        return;
    }
    /* Sit ON the surface, never inside it: the caller's y is the
       desk height the drop measured — lift the origin by the
       type's local footprint floor (List() floor_y). */
    float floor_y = 0.0f;
    if(preset_model != nullptr)
    {
        if(const PresetRegistry::PresetInfo* info =
               preset_model->InfoFor(typeId))
        {
            floor_y = info->floor_y;
        }
    }
    SyncWorkspace();
    std::optional<EditorEdit> e =
        editor.AddInstance(tid, { (float)x, (float)y - floor_y, (float)z });
    if(!e.has_value())
    {
        setStatus(editor.LastError().empty()
            ? QStringLiteral("add refused — finish the drag first")
            : QString::fromStdString(editor.LastError()));
        return;
    }
    commitEdit(std::move(*e));
    selected = QString::fromStdString(editor.PrimarySelection());
    emit selectionChanged();
    setStatus(QStringLiteral("added %1 (%2)")
                  .arg(selected, QString::fromStdString(p->name)));
}

void SceneBridge::setTypeFavorite(const QString& typeId, bool fav)
{
    const std::string tid = typeId.toStdString();
    if(!IsPresetId(tid) || !registry.Contains(tid))
    {
        setStatus(QStringLiteral("favorite refused: unknown type '%1'")
                      .arg(typeId));
        return;
    }
    /* UI preference like camera state — dirty/autosave, never undo.
       Erase-then-append keeps first-favorited-first ordering and
       dedupes any stale repeats in one pass. */
    std::vector<std::string>& favs = meta.ui.favorites;
    favs.erase(std::remove(favs.begin(), favs.end(), tid), favs.end());
    if(fav)
    {
        favs.push_back(tid);
    }
    markDirty();
    if(preset_model != nullptr)
    {
        preset_model->RefreshFavorites();
    }
    emit presetLibraryChanged();
    setStatus(fav ? QStringLiteral("%1 starred").arg(typeId)
                  : QStringLiteral("%1 unstarred").arg(typeId));
}

void SceneBridge::saveInstanceAsVariant(const QString& instanceId,
                                        const QString& newTypeId,
                                        const QString& displayName)
{
    const std::string iid =
        EditorController::InstanceOf(instanceId.toStdString());
    const std::string tid = newTypeId.toStdString();
    const std::string nm  = displayName.trimmed().toStdString();
    const auto dit = workspace.devices.find(iid);
    if(dit == workspace.devices.end())
    {
        setStatus(QStringLiteral("variant refused: '%1' is not an instance")
                      .arg(instanceId));
        return;
    }
    const DevicePreset* src = registry.Find(dit->second.type);
    if(src == nullptr)
    {
        setStatus(QStringLiteral(
            "variant refused: '%1' has unresolvable type '%2'")
                .arg(instanceId, QString::fromStdString(dit->second.type)));
        return;
    }
    if(!IsPresetId(tid))
    {
        setStatus(QStringLiteral(
            "variant refused: '%1' is not a valid type id").arg(newTypeId));
        return;
    }
    if(registry.Contains(tid))
    {
        setStatus(QStringLiteral(
            "variant refused: type '%1' already exists").arg(newTypeId));
        return;
    }
    if(nm.empty())
    {
        setStatus(QStringLiteral("variant refused: name required"));
        return;
    }
    /* A repoint must survive resolution: a mirrored instance (or a
       mirror target) is type-locked to its partner, so the edit
       would roll back AFTER the file landed — refuse before any
       write. */
    {
        const auto sit = workspace.device_settings.find(iid);
        if(sit != workspace.device_settings.end()
           && !sit->second.mirror_of.empty())
        {
            setStatus(QStringLiteral(
                "variant refused: '%1' mirrors %2 — repoint would break the link")
                    .arg(instanceId,
                         QString::fromStdString(sit->second.mirror_of)));
            return;
        }
        for(const auto& kv : workspace.device_settings)
        {
            if(kv.second.mirror_of == iid)
            {
                setStatus(QStringLiteral(
                    "variant refused: '%1' is mirrored by %2 — repoint would break the link")
                        .arg(instanceId, QString::fromStdString(kv.first)));
                return;
            }
        }
    }
    /* The resolved type definition is the variant's seed — entities,
       zones, binding_hints and authored appearance hints carry over
       verbatim; only id/name change. No expanded entity data ever
       lands in studio.json (only the repointed `type` string).
       Sync first so workspace.object_colors carries the live paint,
       then bake the instance's painted look onto the variant's
       entity appearance.body_color — the saved type reproduces what
       the user sees. Emitter paint stays workspace data. */
    SyncWorkspace();
    DevicePreset v = *src;
    v.id   = tid;
    v.name = nm;
    editor.BakePaintedColors(iid, v);
    QString werr;
    if(store == nullptr || !store->WritePresetFile(v, &werr))
    {
        setStatus(QStringLiteral("variant write failed: %1")
                      .arg(werr.isEmpty() ? QStringLiteral("store unavailable")
                                          : werr));
        return;
    }
    /* The file is durable — a valid library asset even if the
       repoint below is refused (e.g. the instance is a mirror and
       the resolve keeps it tied to its owner's type). Reload first
       so the repoint resolves against a registry that knows tid. */
    ReloadPresets();
    SyncWorkspace();
    std::optional<EditorEdit> e = editor.Retype(iid, tid);
    if(e.has_value())
    {
        commitEdit(std::move(*e));
        setStatus(QStringLiteral("saved variant '%1' — %2 now uses it")
                      .arg(newTypeId, instanceId));
    }
    else
    {
        setStatus(QStringLiteral("saved variant '%1' — %2 kept its type%3")
                      .arg(newTypeId, instanceId,
                           editor.LastError().empty()
                               ? QString()
                               : QStringLiteral(" (%1)").arg(
                                     QString::fromStdString(editor.LastError()))));
    }
}

void SceneBridge::createTypeFromSelection(const QStringList& instanceIds,
                                          const QString& newTypeId,
                                          const QString& displayName)
{
    const std::string tid = newTypeId.toStdString();
    const std::string nm  = displayName.trimmed().toStdString();
    if(instanceIds.isEmpty())
    {
        setStatus(QStringLiteral(
            "create preset refused: nothing selected"));
        return;
    }
    if(!IsPresetId(tid))
    {
        setStatus(QStringLiteral(
            "create preset refused: '%1' is not a valid type id")
                      .arg(newTypeId));
        return;
    }
    if(registry.Contains(tid))
    {
        setStatus(QStringLiteral(
            "create preset refused: type '%1' already exists")
                      .arg(newTypeId));
        return;
    }
    if(nm.empty())
    {
        setStatus(QStringLiteral("create preset refused: name required"));
        return;
    }
    /* Child-reference type: one `type`-ref entity per listed root
       instance carrying its world transform — see
       BuildPresetFromInstances for the shared-origin choice and the
       root-id gate (it validates `ids` field by field; nested paths
       refuse). No workspace edit is needed: the placed instances
       stay put (the preset is a new library entry, not a rewrite of
       the desk), so there is nothing to undo — deleting the file
       removes it. */
    std::vector<std::string> ids;
    ids.reserve((size_t)instanceIds.size());
    for(const QString& q : instanceIds)
    {
        ids.push_back(q.toStdString());
    }
    DevicePreset p;
    if(!editor.BuildPresetFromInstances(ids, tid, nm, registry, p))
    {
        setStatus(QString::fromStdString(editor.LastError()));
        return;
    }
    QString werr;
    if(store == nullptr || !store->WritePresetFile(p, &werr))
    {
        setStatus(QStringLiteral("create preset failed: %1")
                      .arg(werr.isEmpty() ? QStringLiteral("store unavailable")
                                          : werr));
        return;
    }
    ReloadPresets();
    setStatus(QStringLiteral("saved type '%1' from %2 instance(s)")
                  .arg(newTypeId).arg((int)p.entities.size()));
}

/*---------------------------------------------------------*\
|| Task 4.3 — device preset editor commands.              ||
||                                                          ||
||   The candidate is a QVariantMap in the *.device.json    ||
||   shape that lives entirely inside the QML editor.       ||
||   validate/preview run against a PRIVATE registry copy   ||
||   and a throwaway document — doc, workspace, bindings    ||
||   and live output are never touched. Save goes through   ||
||   ConfigStore::WritePresetFile (validate -> atomic write ||
||   -> re-read -> re-validate) so only schema-clean files  ||
||   ever land under presets/devices/.                      ||
\*---------------------------------------------------------*/
namespace
{

nlohmann::json VariantToJson(const QVariant& v)
{
    const QJsonDocument d = QJsonDocument::fromVariant(v);
    if(d.isNull())
    {
        return nlohmann::json();   /* discarded below */
    }
    return nlohmann::json::parse(
        d.toJson(QJsonDocument::Compact).constData(),
        nullptr, /*allow_exceptions*/ false);
}

QVariant JsonToVariant(const nlohmann::json& j)
{
    const QJsonDocument d =
        QJsonDocument::fromJson(QByteArray::fromStdString(j.dump()));
    return d.toVariant();
}

QVariantList ErrList(const std::vector<std::string>& errs)
{
    QVariantList out;
    for(const std::string& e : errs)
    {
        out.push_back(QString::fromStdString(e));
    }
    return out;
}

} /* namespace */

QVariantMap SceneBridge::presetDocument(const QString& typeId) const
{
    QVariantMap out;
    const std::string tid = typeId.toStdString();
    const DevicePreset* p = registry.Find(tid);
    out["exists"] = (p != nullptr);
    if(p == nullptr)
    {
        return out;
    }
    bool from_file = false;
    for(const PresetRegistry::PresetInfo& info : registry.List())
    {
        if(info.id == tid)
        {
            from_file = info.from_file;
            break;
        }
    }
    out["fromFile"] = from_file;
    /* Resolved without a file => the packaged defaults layer (or
       the minimal built-in set) supplied it — a save creates a
       user override that shadows it. */
    out["packaged"] = !from_file;
    out["doc"]      = JsonToVariant(ToJson(*p));
    int instances = 0;
    for(const auto& kv : workspace.devices)
    {
        if(kv.second.type == tid)
        {
            instances++;
        }
    }
    out["instances"] = instances;
    return out;
}

QVariantMap SceneBridge::validatePreset(const QVariantMap& candidate) const
{
    QVariantMap out;
    const nlohmann::json j = VariantToJson(candidate);
    DevicePreset p;
    std::vector<std::string> errs;
    if(j.is_discarded())
    {
        errs.push_back("candidate is not a preset document");
    }
    if(!errs.empty() || !DevicePresetFromJson(j, p, &errs))
    {
        out["ok"]     = false;
        out["errors"] = ErrList(errs);
        return out;
    }
    /* Dependency rules are file-layer rules — check the candidate
       against a throwaway copy so the live library is untouched. */
    PresetRegistry tmp = registry;
    if(!tmp.Add(p, &errs))
    {
        out["ok"]     = false;
        out["errors"] = ErrList(errs);
        return out;
    }
    out["ok"]     = true;
    out["errors"] = ErrList(errs);
    return out;
}

QVariantMap SceneBridge::previewPreset(const QVariantMap& candidate) const
{
    QVariantMap out;
    const QVariantMap v = validatePreset(candidate);
    if(!v["ok"].toBool())
    {
        out["ok"]     = false;
        out["errors"] = v["errors"];
        return out;
    }
    const nlohmann::json j = VariantToJson(candidate);
    DevicePreset p;
    std::vector<std::string> errs;
    DevicePresetFromJson(j, p, &errs);   /* known-good post-validate */

    /* Throwaway scene: private registry copy + one root instance
       at the origin. No doc/workspace/binding/hardware contact. */
    PresetRegistry tmp = registry;
    if(!tmp.Add(p, &errs))
    {
        out["ok"]     = false;
        out["errors"] = ErrList(errs);
        return out;
    }
    StudioDocument w;
    w.meta.name = "preview";
    DeviceInstance inst;
    inst.type = p.id;
    w.devices["__preview__"] = inst;
    SceneDocument resolved;
    if(!ResolveScene(w, tmp, resolved, &errs))
    {
        out["ok"]     = false;
        out["errors"] = ErrList(errs);
        return out;
    }

    /* Same row shape objectList() emits — minus binding state —
       plus the emitter positions inline (static preview color is
       the renderer's business). Parents precede children. */
    QVariantList objs;
    for(const SceneObject* po : TopologicalOrder(resolved))
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
        const Quat q = RotationQuat(o.transform.rotation_deg);
        m["qw"] = q.w; m["qx"] = q.x; m["qy"] = q.y; m["qz"] = q.z;
        m["sx"] = o.transform.scale.x;
        m["sy"] = o.transform.scale.y;
        m["sz"] = o.transform.scale.z;
        m["dx"] = o.size_m.x;
        m["dy"] = o.size_m.y;
        m["dz"] = o.size_m.z;
        const Vec3 body = ResolvedBodySize(o);
        m["bx"] = body.x;
        m["by"] = body.y;
        m["bz"] = body.z;
        m["visible"] = o.visible;
        m["emitterCount"] = (int)o.emitters.size();
        QVariantList ems;
        ems.reserve((int)o.emitters.size());
        for(size_t i = 0; i < o.emitters.size(); i++)
        {
            QVariantMap em;
            em["x"] = o.emitters[i].local_pos.x;
            em["y"] = o.emitters[i].local_pos.y;
            em["z"] = o.emitters[i].local_pos.z;
            em["i"] = (int)i;
            em["a"] = o.emitters[i].address;
            ems.push_back(em);
        }
        m["emitters"] = ems;
        objs.push_back(m);
    }
    out["ok"]      = true;
    out["objects"] = objs;
    out["errors"]  = QVariantList();
    return out;
}

QVariantMap SceneBridge::savePresetType(const QVariantMap& candidate,
                                        bool asNew)
{
    QVariantMap out;
    const nlohmann::json j = VariantToJson(candidate);
    DevicePreset p;
    std::vector<std::string> errs;
    if(j.is_discarded() || !DevicePresetFromJson(j, p, &errs))
    {
        if(errs.empty())
        {
            errs.push_back("candidate is not a preset document");
        }
        out["ok"]     = false;
        out["errors"] = ErrList(errs);
        return out;
    }
    if(asNew && registry.Contains(p.id))
    {
        out["ok"] = false;
        out["errors"] = ErrList({ "id: type '" + p.id
                                + "' already exists" });
        return out;
    }
    /* Same dependency check a file load would enforce — private
       copy so a failure leaves the live registry alone. */
    {
        PresetRegistry tmp = registry;
        if(!tmp.Add(p, &errs))
        {
            out["ok"]     = false;
            out["errors"] = ErrList(errs);
            return out;
        }
    }
    QString werr;
    if(store == nullptr || !store->WritePresetFile(p, &werr))
    {
        errs.push_back("write failed: "
            + (werr.isEmpty() ? std::string("store unavailable")
                              : werr.toStdString()));
        out["ok"]     = false;
        out["errors"] = ErrList(errs);
        return out;
    }
    ReloadPresets();

    /* Bound-hardware warnings: for every instance of this type,
       compare each bound zone's expected hardware count against
       the NEW zone's generated emitter count. The save already
       succeeded — these are informational "rebind in Inspector"
       lines, never a resize. A removed zone leaves a dangling
       binding row the resolver will refuse on — report it. */
    QVariantList warns;
    int instances = 0;
    for(const auto& kv : workspace.devices)
    {
        if(kv.second.type != p.id)
        {
            continue;
        }
        instances++;
        const auto sit = workspace.device_settings.find(kv.first);
        if(sit == workspace.device_settings.end())
        {
            continue;
        }
        for(const auto& zk : sit->second.zones)
        {
            if(zk.second.binding.empty())
            {
                continue;
            }
            const DeviceZone* z = nullptr;
            for(const DeviceZone& zz : p.zones)
            {
                if(zz.id == zk.first)
                {
                    z = &zz;
                    break;
                }
            }
            const auto bit = workspace.bindings.find(zk.second.binding);
            const unsigned int hw = (bit == workspace.bindings.end())
                                  ? 0 : bit->second.zone_leds;
            if(z == nullptr)
            {
                warns.push_back(QStringLiteral(
                    "%1: zone '%2' no longer exists on the type — "
                    "its binding is dangling; rebind or remove it")
                    .arg(QString::fromStdString(kv.first),
                         QString::fromStdString(zk.first)));
                continue;
            }
            if(hw == 0)
            {
                continue;   /* count unchecked — nothing to compare */
            }
            /* Dynamic matrix: the bound hardware owns the mapping —
               the type generates zero emitters by design, so a count
               comparison would warn "now has 0 LEDs" on every save. */
            if(z->layout.type == "matrix" && z->layout.dynamic)
            {
                continue;
            }
            const size_t n = GenerateZoneEmitters(*z, "", 0).size();
            if(n < hw)
            {
                warns.push_back(QStringLiteral(
                    "%1: zone %2 now has %3 LEDs; bound hardware "
                    "expects %4 — rebind in Inspector")
                    .arg(QString::fromStdString(kv.first),
                         QString::fromStdString(zk.first))
                    .arg((int)n)
                    .arg((int)hw));
            }
        }
    }

    /* Existing instances adopt the new shape — re-resolve and reset
       the model (a type change is never transforms-only). A resolve
       failure keeps the file saved but the scene on the last good
       document; the error is returned so the editor shows it. */
    SyncWorkspace();
    SceneDocument resolved;
    std::vector<std::string> rerrs;
    if(ResolveScene(workspace, registry, resolved, &rerrs))
    {
        doc      = resolved;
        doc.name = workspace.meta.name;
        /* Same adoption path reloadDeviceTypes() uses — adapter
           re-resolve + matrix/key rebuilds + model reset, so a
           type save that renames or drops zones re-resolves the
           affected bindings in-session instead of waiting for a
           hardware refresh. refreshDevices() touches no editor,
           gesture or selection state, so a live drag or the
           current selection is undisturbed. */
        refreshDevices();
        setStatus(QStringLiteral("saved type '%1'%2")
            .arg(QString::fromStdString(p.id),
                 instances > 0
                     ? QStringLiteral(" — %1 instance(s) updated")
                           .arg(instances)
                     : QString()));
    }
    else
    {
        warns.push_front(QStringLiteral(
            "saved, but the scene no longer resolves: %1")
            .arg(QString::fromStdString(
                rerrs.empty() ? "unknown" : rerrs.front())));
        setStatus(QStringLiteral("saved type '%1' — scene resolve failed")
                      .arg(QString::fromStdString(p.id)));
    }

    out["ok"]        = true;
    out["saved"]     = QString::fromStdString(p.id);
    out["instances"] = instances;
    out["warnings"]  = warns;
    out["errors"]    = QVariantList();
    return out;
}

QVariantMap SceneBridge::convertZoneToPoints(const QVariantMap& candidate,
                                             int zoneIndex)
{
    QVariantMap out;
    const nlohmann::json j = VariantToJson(candidate);
    DevicePreset p;
    std::vector<std::string> errs;
    if(j.is_discarded() || !DevicePresetFromJson(j, p, &errs))
    {
        if(errs.empty())
        {
            errs.push_back("candidate is not a preset document");
        }
        out["ok"]     = false;
        out["errors"] = ErrList(errs);
        return out;
    }
    if(zoneIndex < 0 || zoneIndex >= (int)p.zones.size())
    {
        out["ok"]     = false;
        out["errors"] = ErrList({ "zones: index out of range" });
        return out;
    }
    if(!ZoneLayoutToPoints(p.zones[(size_t)zoneIndex]))
    {
        out["ok"] = false;
        out["errors"] = ErrList({ "zones[" + std::to_string(zoneIndex)
            + "]: nothing to convert (already points or dynamic)" });
        return out;
    }
    out["ok"]        = true;
    out["candidate"] = JsonToVariant(ToJson(p));
    out["errors"]    = QVariantList();
    return out;
}

QVariantList SceneBridge::hardwareControllers() const
{
    QVariantList out;
    const std::vector<ControllerSnapshot>& snap = adapter.Snapshot();
    for(size_t i = 0; i < snap.size(); i++)
    {
        QVariantMap c;
        c["index"]  = (int)i;
        c["name"]   = QString::fromStdString(snap[i].name);
        c["vendor"] = QString::fromStdString(snap[i].vendor);
        QVariantList zones;
        for(size_t z = 0; z < snap[i].zones.size(); z++)
        {
            QVariantMap zm;
            zm["index"] = (int)z;
            zm["name"]  = QString::fromStdString(snap[i].zones[z].name);
            zm["leds"]  = (int)snap[i].zones[z].leds_count;
            zones.push_back(zm);
        }
        c["zones"] = zones;
        out.push_back(c);
    }
    return out;
}

QVariantMap SceneBridge::instanceZoneState(const QString& instanceId) const
{
    QVariantMap out;
    const std::string iid =
        EditorController::InstanceOf(instanceId.toStdString());
    const auto dit = workspace.devices.find(iid);
    if(dit == workspace.devices.end())
    {
        out["ok"] = false;
        return out;
    }
    out["ok"]   = true;
    out["type"] = QString::fromStdString(dit->second.type);
    const DevicePreset* p = registry.Find(dit->second.type);
    const auto sit = workspace.device_settings.find(iid);
    QVariantList zones;
    if(p != nullptr)
    {
        for(const DeviceZone& z : p->zones)
        {
            QVariantMap zm;
            zm["id"]       = QString::fromStdString(z.id);
            zm["ledCount"] = (int)z.led_count;
            zm["layout"]   = QString::fromStdString(z.layout.type);
            zm["dynamic"]  = z.layout.type == "matrix" && z.layout.dynamic;
            QString binding_id;
            int    addr = 0;
            bool   ver  = false;
            if(sit != workspace.device_settings.end())
            {
                const auto zit = sit->second.zones.find(z.id);
                if(zit != sit->second.zones.end())
                {
                    binding_id = QString::fromStdString(zit->second.binding);
                    addr = zit->second.addr_base;
                    ver  = zit->second.verified;
                }
            }
            zm["binding"]  = binding_id;
            zm["addrBase"] = addr;
            zm["verified"] = ver;
            QString label;
            const auto bit =
                workspace.bindings.find(binding_id.toStdString());
            if(bit != workspace.bindings.end())
            {
                label = QString::fromStdString(bit->second.controller_name)
                      + " / "
                      + QString::fromStdString(bit->second.zone_name);
            }
            zm["bindingLabel"] = label;
            zones.push_back(zm);
        }
    }
    out["zones"] = zones;
    return out;
}

void SceneBridge::bindZoneToController(const QString& instanceId,
                                       const QString& zoneId,
                                       int controller, int zone,
                                       int addrBase, bool verified)
{
    const std::string iid =
        EditorController::InstanceOf(instanceId.toStdString());
    const std::string zid = zoneId.toStdString();
    const auto dit = workspace.devices.find(iid);
    if(dit == workspace.devices.end())
    {
        setStatus(QStringLiteral("bind refused: '%1' is not an instance")
                      .arg(instanceId));
        return;
    }
    const DevicePreset* p = registry.Find(dit->second.type);
    bool zone_known = false;
    if(p != nullptr)
    {
        for(const DeviceZone& z : p->zones)
        {
            if(z.id == zid)
            {
                zone_known = true;
                break;
            }
        }
    }
    if(!zone_known)
    {
        setStatus(QStringLiteral("bind refused: type '%1' has no zone '%2'")
            .arg(QString::fromStdString(dit->second.type), zoneId));
        return;
    }
    const std::vector<ControllerSnapshot>& snap = adapter.Snapshot();
    if(controller < 0 || controller >= (int)snap.size()
       || zone < 0 || zone >= (int)snap[(size_t)controller].zones.size())
    {
        setStatus(QStringLiteral("bind refused: hardware pick out of range"));
        return;
    }
    const ControllerSnapshot& cs = snap[(size_t)controller];
    const ZoneSnapshot&       zs = cs.zones[(size_t)zone];

    /* Reuse an existing binding for the same physical pick —
       controller identity + zone name — else mint a stable slug. */
    std::string bid;
    for(const auto& kv : workspace.bindings)
    {
        if(kv.second.controller_name == cs.name
           && kv.second.zone_name == zs.name
           && kv.second.serial == cs.serial
           && kv.second.location == cs.location)
        {
            bid = kv.first;
            break;
        }
    }
    if(bid.empty())
    {
        std::string base;
        const std::string seed = cs.name + "_" + zs.name;
        for(char ch : seed)
        {
            const unsigned char c = (unsigned char)ch;
            base += std::isalnum(c) ? (char)std::tolower(c) : '_';
        }
        if(base.empty())
        {
            base = "binding";
        }
        bid = base;
        for(int n = 2; workspace.bindings.count(bid); n++)
        {
            bid = base + "_" + std::to_string(n);
        }
    }
    DeviceBinding b;
    b.id              = bid;
    b.controller_name = cs.name;
    b.vendor          = cs.vendor;
    b.serial          = cs.serial;
    b.location        = cs.location;
    b.device_type     = cs.device_type;
    b.zone_name       = zs.name;
    b.zone_leds       = zs.leds_count;

    SyncWorkspace();
    std::optional<EditorEdit> e =
        editor.BindZone(iid, zid, b, addrBase, verified);
    if(!e.has_value())
    {
        setStatus(editor.LastError().empty()
            ? QStringLiteral("bind refused")
            : QString::fromStdString(editor.LastError()));
        return;
    }
    commitEdit(std::move(*e));
    setStatus(QStringLiteral("bound %1/%2 -> %3 %4")
        .arg(instanceId, zoneId,
             QString::fromStdString(cs.name),
             QString::fromStdString(zs.name)));
}

void SceneBridge::unbindZone(const QString& instanceId,
                             const QString& zoneId)
{
    const std::string iid =
        EditorController::InstanceOf(instanceId.toStdString());
    SyncWorkspace();
    std::optional<EditorEdit> e =
        editor.UnbindZone(iid, zoneId.toStdString());
    if(!e.has_value())
    {
        if(!editor.LastError().empty())
        {
            setStatus(QString::fromStdString(editor.LastError()));
        }
        return;
    }
    commitEdit(std::move(*e));
    setStatus(QStringLiteral("unbound %1/%2").arg(instanceId, zoneId));
}

void SceneBridge::setZoneParams(const QString& instanceId,
                                const QString& zoneId,
                                int addrBase, bool verified)
{
    const std::string iid =
        EditorController::InstanceOf(instanceId.toStdString());
    SyncWorkspace();
    std::optional<EditorEdit> e =
        editor.SetZoneParams(iid, zoneId.toStdString(), addrBase, verified);
    if(!e.has_value())
    {
        if(!editor.LastError().empty())
        {
            setStatus(QString::fromStdString(editor.LastError()));
        }
        return;
    }
    commitEdit(std::move(*e));
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
    /* Rides along on the prefs read so the QML router can honor the
       middle_drag: pan|orbit control pref without a second API. */
    m["middle_drag"] = QString::fromStdString(meta.controls.middle_drag);
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

void SceneBridge::setRenderQuality(const QString& q)
{
    /* Same dirty/autosave path as camera prefs — editor chrome,
       never undo history, never the scene. */
    if(q != "low" && q != "balanced" && q != "high")
    {
        return;
    }
    if(meta.render.quality == q.toStdString())
    {
        return;
    }
    meta.render.quality = q.toStdString();
    markDirty();
    emit renderPrefsChanged();
}

void SceneBridge::setRenderBloom(bool on)
{
    if(meta.render.bloom == on)
    {
        return;
    }
    meta.render.bloom = on;
    markDirty();
    emit renderPrefsChanged();
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

/*---------------------------------------------------------*\
||| Task 4.4 — portable export/import + validated reload   ||
|||                                                           ||
|||   The bundle core is Qt-free (presets/PresetBundle);    ||
|||   these slots are the Qt glue: they translate the       ||
|||   filesystem paths and the conflict choices, then run   ||
|||   the imported candidate through the SAME               ||
|||   resolve-then-apply path LoadWorkspace uses.           ||
\*---------------------------------------------------------*/
QVariantMap SceneBridge::inspectBundle(const QString& dirPath)
{
    QVariantMap out;
    if(store == nullptr || dirPath.isEmpty())
    {
        out["ok"]    = false;
        out["error"] = QStringLiteral("workspace unavailable");
        return out;
    }
    ImportPlan plan;
    std::vector<std::string> errors;
    if(!InspectBundle(dirPath.toStdString(), registry, plan, &errors))
    {
        out["ok"]    = false;
        out["error"] = QString::fromStdString(
            errors.empty() ? "invalid bundle" : errors.front());
        return out;
    }
    QVariantList types;
    QStringList  conflicts;
    for(const BundleType& bt : plan.types)
    {
        QVariantMap t;
        t["id"]   = QString::fromStdString(bt.id);
        t["name"] = QString::fromStdString(bt.preset.name);
        t["status"] = (bt.status == BundleType::Status::New)       ? "new"
                    : (bt.status == BundleType::Status::Identical) ? "identical"
                                                                   : "conflict";
        if(bt.has_local)
        {
            t["localName"] = QString::fromStdString(bt.local.name);
        }
        types.push_back(t);
        if(bt.status == BundleType::Status::Conflict)
        {
            conflicts << QString::fromStdString(bt.id);
        }
    }
    QStringList warns;
    for(const std::string& w : plan.warnings)
    {
        warns << QString::fromStdString(w);
    }
    out["ok"]        = true;
    out["types"]     = types;
    out["conflicts"] = conflicts;
    out["devices"]   = (int)plan.workspace.devices.size();
    out["assets"]    = (int)plan.assets.size();
    out["warnings"]  = warns;
    return out;
}

bool SceneBridge::exportBundle(const QString& dirPath, bool overwrite)
{
    if(store == nullptr || dirPath.isEmpty())
    {
        setStatus(QStringLiteral("workspace unavailable — cannot export"));
        return false;
    }
    std::vector<std::string> errors, warnings;
    unsigned int ntypes = 0, nassets = 0;
    if(!ExportBundle(dirPath.toStdString(), CurrentWorkspace(), registry,
                     store->PresetDir().toStdString(), &errors, &warnings,
                     overwrite, &ntypes, &nassets))
    {
        setStatus(QStringLiteral("export failed: %1")
            .arg(QString::fromStdString(
                errors.empty() ? "unknown" : errors.front())));
        return false;
    }
    for(const std::string& w : warnings)
    {
        emit statusMessage(QStringLiteral("export: %1")
                               .arg(QString::fromStdString(w)));
    }
    setStatus(QStringLiteral("exported %1 type(s) + %2 asset(s) to %3")
                  .arg(ntypes).arg(nassets).arg(dirPath));
    return true;
}

bool SceneBridge::importBundle(const QString& dirPath,
                               const QVariantMap& choices)
{
    if(api == nullptr || store == nullptr || dirPath.isEmpty())
    {
        setStatus(QStringLiteral("workspace unavailable — cannot import"));
        return false;
    }
    ImportPlan plan;
    std::vector<std::string> errors;
    if(!InspectBundle(dirPath.toStdString(), registry, plan, &errors))
    {
        setStatus(QStringLiteral("import rejected: %1")
            .arg(QString::fromStdString(
                errors.empty() ? "invalid bundle" : errors.front())));
        return false;
    }
    std::map<std::string, std::string> cmap;
    for(auto it = choices.constBegin(); it != choices.constEnd(); ++it)
    {
        cmap[it.key().toStdString()] = it.value().toString().toStdString();
    }
    StudioDocument candidate;
    std::vector<std::string> warnings;
    if(!ApplyImport(plan, cmap, store->PresetDir().toStdString(),
                    candidate, &errors, &warnings))
    {
        setStatus(QStringLiteral("import failed: %1")
            .arg(QString::fromStdString(
                errors.empty() ? "unknown" : errors.front())));
        return false;
    }
    /* Belt and suspenders: ApplyImport already sanitizes, but the
       runtime switch is ours to guard — imported content never
       arms live output. */
    candidate.meta.live_on_startup = false;

    /* Pick up the just-written type files, then resolve — same
       candidate->resolve->apply ordering as LoadWorkspace. A
       rejected candidate leaves the current scene/inputs/output
       untouched. */
    ReloadPresets();
    SceneDocument resolved;
    std::vector<std::string> rerrs;
    if(!ResolveScene(candidate, registry, resolved, &rerrs))
    {
        setStatus(QStringLiteral("imported workspace failed to resolve"
                                 " — scene kept: %1")
            .arg(QString::fromStdString(
                rerrs.empty() ? "unknown" : rerrs.front())));
        return false;
    }
    /* Only a fully-resolved candidate replaces the doc — kill the
       live gesture HERE so a rejected import keeps it alive. */
    editor.Cancel();
    candidate.scene = resolved;
    ApplyWorkspace(candidate);
    /* The imported doc differs from studio.json until saved. */
    store->MarkDirty();

    for(const std::string& w : plan.warnings)
    {
        emit statusMessage(QStringLiteral("import: %1")
                               .arg(QString::fromStdString(w)));
    }
    for(const std::string& w : warnings)
    {
        emit statusMessage(QStringLiteral("import: %1")
                               .arg(QString::fromStdString(w)));
    }
    int n_new = 0, n_conflict = 0;
    for(const BundleType& bt : plan.types)
    {
        if(bt.status == BundleType::Status::New)       { n_new++; }
        if(bt.status == BundleType::Status::Conflict)  { n_conflict++; }
    }
    setStatus(QStringLiteral("imported %1 device(s): %2 new type(s),"
                             " %3 remapped — bindings unverified,"
                             " resolve them locally")
                  .arg((int)candidate.devices.size())
                  .arg(n_new).arg(n_conflict));
    return true;
}

void SceneBridge::reloadDeviceTypes()
{
    /* Re-read presets/devices/ over the packaged defaults, then
       re-resolve the workspace: every instance of an edited type
       updates together while the devices section (placements) is
       never rewritten. A workspace that no longer resolves keeps
       the current scene. */
    editor.Cancel();
    /* Same convention as every other resolve path: runtime state
       (effect playing/params, inputs, painted colors) lives in
       doc/meta, not workspace — sync first or the re-resolve
       silently reverts it and desyncs doc from the live engine. */
    SyncWorkspace();
    ReloadPresets();
    SceneDocument resolved;
    if(!ResolveWorkspace(resolved))
    {
        return;    /* ResolveWorkspace already set the status */
    }
    doc      = resolved;
    doc.name = workspace.meta.name;
    /* adapter re-resolve + matrix/key rebuilds + model reset. */
    refreshDevices();
    setStatus(QStringLiteral("device types reloaded (%1 types)")
                  .arg((int)registry.Ids().size()));
}

bool SceneBridge::presetIdAvailable(const QString& id) const
{
    const std::string s = id.toStdString();
    return IsPresetId(s) && !registry.Contains(s);
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
    editor.CancelLayerGesture();
    editor.ClearSelection();
    if(!selected.isEmpty())
    {
        selected.clear();
    }
    emit selectionChanged();
    workspace = using_fallback_types ? BuildFallbackWorkspace()
                                     : BuildDefaultWorkspace();
    SceneDocument resolved;
    doc = ResolveScene(workspace, registry, resolved, nullptr)
        ? resolved : BuildDefaultDesk();
    doc.name = workspace.meta.name;
    frame.clear();
    rebuildEffect();             /* model + engine follow the reset */
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
        /* Input-requirement badge + provenance for the strip:
           "audio" | "key" | "screen" | "" and whether a file look
           supplies this row. */
        m["needs"]       = QString::fromStdString(p.needs);
        m["fromFile"]    = p.from_file;
        out.push_back(m);
    }
    return out;
}

void SceneBridge::rebuildEffect()
{
    std::vector<EffectLayer> layers;
    /* The authored inline stack wins when present — `preset` keeps
       provenance (which look the stack was remixed from). An empty
       stack resolves the named look through the registry. Global
       speed/intensity and the ripple-decay slider apply to the
       engine COPY — the authored stack persists unscaled. */
    if(!doc.effect.layers.empty())
    {
        layers = doc.effect.layers;
    }
    else if(!doc.effect.preset.empty())
    {
        layers = BuildPreset(doc.effect.preset, doc.effect.seed);
    }
    if(!layers.empty())
    {
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
    /* Single choke point for the layer editor: every path that
       rebuilds (commit, undo/redo, preset pick, reload, param
       sliders, workspace load) re-snapshots the effective stack. */
    RefreshEffectModel();
    emit effectLayersChanged();
}

void SceneBridge::playPreset(const QString& presetId)
{
    const std::string id = presetId.toStdString();
    if(FindPreset(id) == nullptr)
    {
        setStatus(QStringLiteral("unknown preset %1").arg(presetId));
        return;
    }
    if(editor.GestureActive() || editor.LayerGestureActive())
    {
        /* A stray card click mid-drag must not clobber the live
           gesture or silently re-write the stack — same refusal
           contract as undo()/redo(). */
        emit statusMessage(QStringLiteral(
            "look pick refused — finish the drag first"));
        return;
    }
    const bool preset_swap = (doc.effect.preset != id);
    /* Selecting a preset (re-selecting included) clears the authored
       inline stack — the named look is the whole point of the pick.
       Routed through the editor so preset swap + stack clear +
       seed reset land as ONE undoable record: undo restores the
       stack, the previous preset id and the previous seed together.
       (Was: a direct doc write autosave persisted irreversibly —
       a stray click permanently deleted the authored stack.) */
    SyncWorkspace();
    ApplyEffectOp(editor.SelectPreset(id));
    if(preset_swap)
    {
        play_t = 0.0;
        input_bus.ClearEvents();   /* old-clock events would age wrong */
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
        /* A hidden preview with live OFF has no frame consumer —
           keep the timer off (setPreviewVisible re-arms it on show).
           With live on it must run: the tick is what pushes. */
        if(preview_visible || live_output.load())
        {
            play_timer->start();
        }
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
    if(editor.GestureActive() || editor.LayerGestureActive())
    {
        /* Same refusal as undo(): a mid-drag Stop used to silently
           delete the authored stack with no way back. */
        emit statusMessage(QStringLiteral(
            "stop refused — finish the drag first"));
        return;
    }
    setPlaying(false);
    /* "no effect" clears preset + stack — through the editor now,
       so the wipe is an undoable record (undo restores both)
       instead of a direct doc write autosave persisted. */
    SyncWorkspace();
    ApplyEffectOp(editor.SetLayers({}, "", "clear effect"));
    frame.clear();
    rebuildEffect();             /* empty stack + model refresh */
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

void SceneBridge::applyEffectLayers(
    const std::vector<EffectLayer>& layers)
{
    /* Whole-stack replacement — one undoable authored edit.
       Callers hand over VALIDATED resolved literals (EffectJson's
       grammar); the stack wins over `preset` at rebuild while the
       preset id stays as provenance. AdoptResolved rebuilds the
       engine + layer model and ApplyEffectOp starts playback when
       the new stack produces output. */
    SyncWorkspace();
    ApplyEffectOp(editor.SetLayers(
        layers, workspace.effect.preset));
}

/*---------------------------------------------------------*\
|| Task 5.2 — effect-layer editing                          ||
||                                                           ||
||   Reads answer the EFFECTIVE stack (authored inline      ||
||   layers when present, else the resolved named look).    ||
||   Every write runs SyncWorkspace -> controller op ->     ||
||   ApplyEffectOp, which either pushes the returned        ||
||   record through commitEdit (discrete edit / gesture     ||
||   commit) or mirrors a live gesture preview into doc —   ||
||   so drags re-render the preview every move but land     ||
||   exactly one undo command.                              ||
\*---------------------------------------------------------*/
QObject* SceneBridge::effectLayerModel() const
{
    return layer_model;
}

std::vector<EffectLayer> SceneBridge::EffectiveLayers() const
{
    if(!doc.effect.layers.empty())
    {
        return doc.effect.layers;
    }
    if(!doc.effect.preset.empty())
    {
        return BuildPreset(doc.effect.preset, doc.effect.seed);
    }
    return {};
}

void SceneBridge::RefreshEffectModel()
{
    if(layer_model != nullptr)
    {
        layer_model->SetStack(EffectiveLayers());
    }
}

void SceneBridge::PreviewEffectSync()
{
    /* Mirror the workspace's previewed effect state into doc —
       SyncWorkspace must NOT run here: doc.effect is stale during a
       layer gesture, and copying it back would clobber the preview
       stack the controller just wrote. */
    doc.effect.preset = workspace.effect.preset;
    doc.effect.seed   = workspace.effect.seed;
    doc.effect.layers = workspace.effect.layers;
    rebuildEffect();
}

void SceneBridge::ApplyEffectOp(std::optional<EditorEdit>&& e)
{
    if(e.has_value())
    {
        commitEdit(std::move(*e));
        /* An authored stack that produces output previews
           immediately — same contract as applyEffectLayers. */
        if(!engine.Empty() && !playing_state)
        {
            setPlaying(true);
        }
        return;
    }
    if(editor.LayerGestureActive())
    {
        /* Inside a gesture a nullopt means the write landed as
           preview state — mirror it so the viewport follows the
           pointer. */
        PreviewEffectSync();
        return;
    }
    if(!editor.LastError().empty())
    {
        emit statusMessage(
            QString::fromStdString(editor.LastError()));
    }
}

int SceneBridge::effectLayerCount() const
{
    return (int)EffectiveLayers().size();
}

QVariantMap SceneBridge::effectLayer(int index) const
{
    const std::vector<EffectLayer> layers = EffectiveLayers();
    QVariantMap m;
    if(index < 0 || index >= (int)layers.size())
    {
        return m;
    }
    const EffectLayer& l = layers[index];
    m["index"]     = index;
    m["primitive"] = QString::fromStdString(l.primitive);
    m["enabled"]   = l.enabled;
    m["blend"]     = l.blend == BlendMode::Add    ? "add"
                   : l.blend == BlendMode::Screen ? "screen"
                                                  : "replace";
    m["opacity"]   = l.opacity;
    m["space"]     = l.space == CoordSpace::Local ? "local" : "world";
    m["speed"]     = l.speed;
    m["scale"]     = l.scale;
    m["phase"]     = l.phase;
    m["density"]   = l.density;
    const auto vec = [](const Vec3& v) {
        QVariantMap o;
        o["x"] = v.x;  o["y"] = v.y;  o["z"] = v.z;
        return o;
    };
    m["origin"]    = vec(l.origin);
    m["direction"] = vec(l.direction);
    QVariantList path;
    for(const Vec3& p : l.path)
    {
        path.push_back(vec(p));
    }
    m["path"] = path;
    QVariantList stops;
    for(const PaletteStop& s : l.palette.stops)
    {
        QVariantMap st;
        st["pos"]   = s.pos;
        st["color"] = QString::fromStdString(
            SceneColorHex(ToSceneColor(s.color)));
        stops.push_back(st);
    }
    m["palette"] = stops;
    QStringList targets;
    for(const std::string& t : l.targets)
    {
        targets << QString::fromStdString(t);
    }
    m["targets"] = targets;
    m["source"]  = QString::fromStdString(l.source);
    m["seed"]    = (double)l.seed;
    return m;
}

QVariantList SceneBridge::effectTargetIds() const
{
    /* Valid layer-target terms: object ids + geometry tags + emitter
       groups — the dead-target guard in rebuildEffect matches
       against the same terms. */
    QVariantList out;
    std::set<std::string> seen;
    const auto add = [&out, &seen](const std::string& s) {
        if(!s.empty() && seen.insert(s).second)
        {
            out.push_back(QString::fromStdString(s));
        }
    };
    for(const SceneObject& o : doc.objects)
    {
        add(o.id);
        add(o.geometry);
        for(const Emitter& em : o.emitters)
        {
            add(em.group);
        }
    }
    return out;
}

QVariantMap SceneBridge::inputSourceState(const QString& source) const
{
    QVariantMap m;
    const QString s = source.toLower();
    m["name"] = s;
    if(s == "audio")
    {
        m["enabled"] = audio_on;
        m["ready"]   = audio_in.Running();
        m["status"]  = QString::fromStdString(audio_in.Status());
    }
    else if(s == "key")
    {
        m["enabled"] = key_on;
        m["ready"]   = key_in.Running();
        m["status"]  = QString::fromStdString(key_in.Status());
    }
    else if(s == "screen")
    {
        m["enabled"] = screen_on;
        m["ready"]   = screen_in->Running();
        m["status"]  = screen_in->Status();
    }
    else
    {
        /* Unknown/none — the badge hides itself. */
        m["enabled"] = false;
        m["ready"]   = false;
        m["status"]  = QString();
    }
    return m;
}

/*---------------------------------------------------------*\
|| Effect gestures — one undo command per completed drag    ||
\*---------------------------------------------------------*/
void SceneBridge::beginEffectGesture()
{
    SyncWorkspace();
    editor.BeginLayerGesture();
}

void SceneBridge::commitEffectGesture(const QString& label)
{
    const bool was_active = editor.LayerGestureActive();
    std::optional<EditorEdit> e = editor.CommitLayerGesture(
        label.toStdString());
    if(e.has_value())
    {
        commitEdit(std::move(*e));
        if(!engine.Empty() && !playing_state)
        {
            setPlaying(true);
        }
        return;
    }
    if(was_active)
    {
        /* No-record commit — the controller restored the begin
           snapshot (a materialize-only gesture, or a scrub that
           returned to its start value). Mirror the restored state
           back into doc exactly like cancelEffectGesture does:
           without this, doc.effect keeps the previewed/materialized
           stack and a later autosave persists it with no recorded
           edit. */
        PreviewEffectSync();
    }
}

void SceneBridge::cancelEffectGesture()
{
    if(!editor.LayerGestureActive())
    {
        return;
    }
    editor.CancelLayerGesture();
    /* Mirror the restored snapshot back into doc so preview and
       engine drop the abandoned preview state. */
    PreviewEffectSync();
}

/*---------------------------------------------------------*\
|| Stack + per-layer ops                                    ||
\*---------------------------------------------------------*/
void SceneBridge::moveEffectLayer(int from, int to)
{
    if(from < 0 || to < 0)
    {
        return;
    }
    SyncWorkspace();
    ApplyEffectOp(editor.MoveLayer((size_t)from, (size_t)to));
}

void SceneBridge::addEffectLayer(const QString& primitive)
{
    SyncWorkspace();
    ApplyEffectOp(editor.AddLayer(primitive.toStdString()));
    /* Select-by-index is QML-side state; the new row is last. */
}

void SceneBridge::removeEffectLayer(int index)
{
    if(index < 0)
    {
        return;
    }
    SyncWorkspace();
    ApplyEffectOp(editor.RemoveLayer((size_t)index));
}

void SceneBridge::setEffectLayerEnabled(int index, bool on)
{
    if(index < 0)
    {
        return;
    }
    SyncWorkspace();
    ApplyEffectOp(editor.SetLayerEnabled((size_t)index, on));
}

void SceneBridge::setEffectLayerBlend(int index, const QString& blend)
{
    if(index < 0)
    {
        return;
    }
    const QString b = blend.toLower();
    BlendMode mode;
    if(b == "add")
    {
        mode = BlendMode::Add;
    }
    else if(b == "screen")
    {
        mode = BlendMode::Screen;
    }
    else if(b == "replace")
    {
        mode = BlendMode::Replace;
    }
    else
    {
        emit statusMessage(QStringLiteral(
            "blend must be replace|add|screen"));
        return;
    }
    SyncWorkspace();
    ApplyEffectOp(editor.SetLayerBlend((size_t)index, mode));
}

void SceneBridge::setEffectLayerOpacity(int index, double v)
{
    if(index < 0)
    {
        return;
    }
    SyncWorkspace();
    ApplyEffectOp(editor.SetLayerOpacity((size_t)index, (float)v));
}

void SceneBridge::setEffectLayerField(int index, const QString& field,
                                      double v)
{
    if(index < 0)
    {
        return;
    }
    const QString f = field.toLower();
    EditorController::LayerField lf;
    if(f == "speed")        { lf = EditorController::LayerField::Speed;   }
    else if(f == "scale")   { lf = EditorController::LayerField::Scale;   }
    else if(f == "phase")   { lf = EditorController::LayerField::Phase;   }
    else if(f == "density") { lf = EditorController::LayerField::Density; }
    else if(f == "seed")    { lf = EditorController::LayerField::Seed;    }
    else
    {
        emit statusMessage(QStringLiteral(
            "unknown layer field '%1'").arg(field));
        return;
    }
    SyncWorkspace();
    ApplyEffectOp(editor.SetLayerField((size_t)index, lf, v));
}

void SceneBridge::setEffectLayerSpace(int index, bool local)
{
    if(index < 0)
    {
        return;
    }
    SyncWorkspace();
    ApplyEffectOp(editor.SetLayerSpace(
        (size_t)index, local ? CoordSpace::Local : CoordSpace::World));
}

void SceneBridge::setEffectLayerOrigin(int index,
                                       double x, double y, double z)
{
    if(index < 0)
    {
        return;
    }
    SyncWorkspace();
    ApplyEffectOp(editor.SetLayerOrigin(
        (size_t)index, { (float)x, (float)y, (float)z }));
}

void SceneBridge::setEffectLayerDirection(int index,
                                          double x, double y, double z)
{
    if(index < 0)
    {
        return;
    }
    SyncWorkspace();
    ApplyEffectOp(editor.SetLayerDirection(
        (size_t)index, { (float)x, (float)y, (float)z }));
}

void SceneBridge::setEffectLayerSource(int index, const QString& source)
{
    if(index < 0)
    {
        return;
    }
    SyncWorkspace();
    ApplyEffectOp(editor.SetLayerSource((size_t)index,
                                        source.toStdString()));
}

void SceneBridge::setEffectLayerTargets(int index,
                                        const QStringList& targets)
{
    if(index < 0)
    {
        return;
    }
    std::vector<std::string> ts;
    ts.reserve(targets.size());
    for(const QString& t : targets)
    {
        const QString s = t.trimmed();
        if(!s.isEmpty())
        {
            ts.push_back(s.toStdString());
        }
    }
    SyncWorkspace();
    ApplyEffectOp(editor.SetLayerTargets((size_t)index, ts));
}

/*---------------------------------------------------------*\
|| Palette stops + path points                              ||
\*---------------------------------------------------------*/
void SceneBridge::addEffectLayerStop(int index, double pos,
                                     const QString& color)
{
    if(index < 0)
    {
        return;
    }
    SceneColor c = 0;
    if(!ParseSceneColor(nlohmann::json(color.toStdString()), c))
    {
        emit statusMessage(QStringLiteral(
            "stop color must be \"#RRGGBB\""));
        return;
    }
    SyncWorkspace();
    ApplyEffectOp(editor.AddLayerStop((size_t)index, (float)pos,
                                      ToColorF(c)));
}

void SceneBridge::removeEffectLayerStop(int index, int stop)
{
    if(index < 0 || stop < 0)
    {
        return;
    }
    SyncWorkspace();
    ApplyEffectOp(editor.RemoveLayerStop((size_t)index,
                                         (size_t)stop));
}

int SceneBridge::moveEffectLayerStop(int index, int stop, double pos)
{
    if(index < 0 || stop < 0)
    {
        return stop;
    }
    SyncWorkspace();
    ApplyEffectOp(editor.MoveLayerStop((size_t)index, (size_t)stop,
                                       (float)pos));
    /* The op re-sorted the palette — hand the dragged stop's NEW
       index back so a scrub keeps hold of the same stop across
       the sort (positions are unique, so the pos we just wrote —
       or a refused move — identifies it exactly). A refused write
       leaves no stop at `pos` → the incoming index stands. */
    if(index < (int)workspace.effect.layers.size())
    {
        const std::vector<PaletteStop>& stops =
            workspace.effect.layers[index].palette.stops;
        for(size_t k = 0; k < stops.size(); k++)
        {
            if(stops[k].pos == (float)pos)
            {
                return (int)k;
            }
        }
    }
    return stop;
}

void SceneBridge::setEffectLayerStopColor(int index, int stop,
                                          const QString& color)
{
    if(index < 0 || stop < 0)
    {
        return;
    }
    SceneColor c = 0;
    if(!ParseSceneColor(nlohmann::json(color.toStdString()), c))
    {
        emit statusMessage(QStringLiteral(
            "stop color must be \"#RRGGBB\""));
        return;
    }
    SyncWorkspace();
    ApplyEffectOp(editor.SetLayerStopColor((size_t)index,
                                           (size_t)stop,
                                           ToColorF(c)));
}

void SceneBridge::addEffectLayerPathPoint(int index,
                                          double x, double y, double z)
{
    if(index < 0)
    {
        return;
    }
    SyncWorkspace();
    ApplyEffectOp(editor.AddLayerPathPoint(
        (size_t)index, { (float)x, (float)y, (float)z }));
}

void SceneBridge::setEffectLayerPathPoint(int index, int pt,
                                          double x, double y, double z)
{
    if(index < 0 || pt < 0)
    {
        return;
    }
    SyncWorkspace();
    ApplyEffectOp(editor.SetLayerPathPoint(
        (size_t)index, (size_t)pt, { (float)x, (float)y, (float)z }));
}

void SceneBridge::removeEffectLayerPathPoint(int index, int pt)
{
    if(index < 0 || pt < 0)
    {
        return;
    }
    SyncWorkspace();
    ApplyEffectOp(editor.RemoveLayerPathPoint((size_t)index,
                                              (size_t)pt));
}

void SceneBridge::resetEffectLayers()
{
    SyncWorkspace();
    ApplyEffectOp(editor.SetLayers({}, workspace.effect.preset));
}

/*---------------------------------------------------------*\
|| Save-as personal look                                    ||
\*---------------------------------------------------------*/
QVariantMap SceneBridge::saveLookAs(const QString& id,
                                    const QString& name)
{
    QVariantMap out;
    out["ok"] = false;
    const std::string lid = id.trimmed().toStdString();
    if(!IsPresetId(lid))
    {
        out["errors"] = QStringList{ QStringLiteral(
            "bad look id '%1' — expected [A-Za-z0-9_-]").arg(id) };
        return out;
    }
    if(editor.LayerGestureActive() || editor.GestureActive())
    {
        /* Adopting the saved look is refused mid-gesture — refuse
           the whole save up front so ok:true never reports a
           half-done adopt. */
        out["errors"] = QStringList{ QStringLiteral(
            "finish the current edit first") };
        return out;
    }
    /* Same overwrite gate as the preset editor's save-as
       ("type id already exists"): an existing id — workspace file
       or shipped default — is refused. The user picks a fresh id;
       WriteEffectFile must never silently overwrite a look. */
    if(EffectLooks().Contains(lid))
    {
        out["errors"] = QStringList{ QStringLiteral(
            "look id '%1' already exists — pick a new id")
                .arg(id) };
        return out;
    }
    if(store == nullptr)
    {
        out["errors"] = QStringList{ QStringLiteral(
            "no workspace store") };
        return out;
    }

    /* The saved look's layers are the EFFECTIVE stack — resolved
       literals, no remix specs (a saved look is what the user sees,
       not a template). */
    const std::vector<EffectLayer> stack = EffectiveLayers();
    EffectDocument d;
    d.id   = lid;
    d.name = name.trimmed().isEmpty()
        ? lid : name.trimmed().toStdString();
    /* Derive `needs` from the layers: the first non-empty source
       wins; a screenfield primitive implies screen sampling even
       with no source set. */
    std::string needs;
    for(const EffectLayer& l : stack)
    {
        const std::string want =
            !l.source.empty() ? l.source
            : l.primitive == "screenfield" ? "screen" : "";
        if(!want.empty())
        {
            needs = want;
            break;
        }
    }
    d.needs = needs;
    /* Canonical field order keeps the file hand-readable — the
       plain-json serializer would sort keys alphabetically. */
    static const char* const order[] = {
        "primitive", "space", "blend", "opacity", "speed", "scale",
        "phase", "density", "origin", "direction", "path",
        "palette", "targets", "source", "seed", "enabled",
    };
    d.layers = nlohmann::ordered_json::array();
    for(const EffectLayer& l : stack)
    {
        const nlohmann::json jl = EffectLayerToJson(l);
        nlohmann::ordered_json ol;
        for(const char* k : order)
        {
            if(jl.contains(k))
            {
                ol[k] = jl[k];
            }
        }
        d.layers.push_back(ol);
    }

    QString werr;
    if(!store->WriteEffectFile(d, &werr))
    {
        out["errors"] = QStringList{ werr };
        return out;
    }

    /* Pick up the landed file (and any other edits to
       presets/effects/) before pointing the workspace at it. */
    ReloadPresets();
    if(!EffectLooks().Contains(lid))
    {
        out["errors"] = QStringList{ QStringLiteral(
            "saved look '%1' did not register").arg(id) };
        return out;
    }

    /* Adopt as one undoable edit: preset <- saved id, inline stack
       cleared (the file is now the definition). */
    SyncWorkspace();
    ApplyEffectOp(editor.SetLayers({}, lid, "save as look"));
    out["ok"]   = true;
    out["id"]   = QString::fromStdString(lid);
    out["path"] = store->EffectPresetDir() + "/"
                + QString::fromStdString(lid) + ".effect.json";
    return out;
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
       at the full tick rate for zero visible change. A hidden
       preview skips the emittersChanged repaint churn but still
       pushes — requested live playback continues while hidden
       (spec §8); the frame state stays current either way. */
    if(!frame_sent || frame != last_frame)
    {
        last_frame = frame;
        frame_sent = true;
        if(preview_visible)
        {
            emitFrameChanged();
        }
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

void SceneBridge::setPreviewVisible(bool on)
{
    /* GUI thread only (StudioTab show/hide + top-level window
       visibility). Hidden: tick() keeps evaluating + pushing for
       live output but skips the emittersChanged repaint. When live
       is ALSO off nothing consumes the frame at all — stop the play
       timer so hidden playback doesn't spin the engine; play_t
       freezes until the next show (on-demand idle rendering).
       Re-showing repaints the current frame once so the preview
       never displays a stale buffer. */
    if(preview_visible == on)
    {
        return;
    }
    preview_visible = on;
    if(on)
    {
        if(!frame.empty())
        {
            emitFrameChanged();
        }
        if(playing_state && !play_timer->isActive())
        {
            play_clock->restart();
            play_timer->start();
        }
    }
    else if(!live_output.load() && play_timer->isActive())
    {
        play_timer->stop();
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
    if(!live_output || api == nullptr || shutting_down.load())
    {
        return;
    }
    /* Frame pushes ride the lanes whenever the engine holds a stack
       — an inline layer stack counts even without a named preset. */
    if(!frame.empty() && !engine.Empty())
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
    push_workers.fetch_add(1);
    /* If the thread ctor throws the count would strand — the
       dtor's join counts on it. Once detached, the worker's own
       ExitCount owns the decrement. */
    struct SpawnCount { std::atomic<int>& c; bool armed = true;
        ~SpawnCount() { if(armed) c.fetch_sub(1); } } spawn{push_workers};
    std::thread([this, doc_copy]()
    {
        /* Decrement on every exit — the dtor's join relies on it.
           A driver exception must not escape a detached thread
           (that is terminate) nor strand the count. */
        struct ExitCount { std::atomic<int>& c;
            ~ExitCount() { c.fetch_sub(1); } } guard{push_workers};
        std::string err;
        try
        {
            /* Both lane mutexes: a lane worker can still be finishing
               a write when the effect just stopped. */
            QMutexLocker lock(&io_mutex);
            QMutexLocker lock_fast(&fast_io_mutex);
            err = adapter.PushAll(doc_copy, nullptr);
        }
        catch(...)
        {
            err = "push failed: driver threw";
        }
        push_in_flight = false;
        if(push_again.exchange(false) && !shutting_down.load())
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
    spawn.armed = false;
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
    push_workers.fetch_add(1);
    /* Same ctor-throw guard as schedulePush — a stranded count
       hangs the dtor's join. */
    struct SpawnCount { std::atomic<int>& c; bool armed = true;
        ~SpawnCount() { if(armed) c.fetch_sub(1); } } spawn{push_workers};
    std::thread([this, lane, doc_copy, frame_copy]()
    {
        /* Counted for the dtor's join; lane_in_flight stays the
           coalescing flag. A driver throw must not escape (that is
           terminate) nor strand either flag — a stuck in_flight
           would wedge the lane forever. */
        struct ExitCount { std::atomic<int>& c;
            ~ExitCount() { c.fetch_sub(1); } } guard{push_workers};
        try
        {
            runPushLane(lane, doc_copy, frame_copy);
        }
        catch(...)
        {
            QMetaObject::invokeMethod(this, [this]()
            {
                setStatus(QStringLiteral("push failed: driver threw"));
            }, Qt::QueuedConnection);
        }
        lane_in_flight[lane] = false;
        if(lane_again[lane].exchange(false) && !shutting_down.load())
        {
            QMetaObject::invokeMethod(this, [this, lane]() { scheduleLane(lane); },
                                      Qt::QueuedConnection);
        }
    }).detach();
    spawn.armed = false;
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
        /* Bail between bindings once teardown started — the dtor is
           waiting on this worker and a remaining sweep is dead
           work. Mid-write exits stay impossible to shortcut, so the
           join stays bounded by one driver write. */
        if(shutting_down.load())
        {
            break;
        }
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
    push_workers.fetch_add(1);
    /* Same ctor-throw guard as schedulePush — a stranded count
       hangs the dtor's join. */
    struct SpawnCount { std::atomic<int>& c; bool armed = true;
        ~SpawnCount() { if(armed) c.fetch_sub(1); } } spawn{push_workers};
    std::thread([this, snapshot, oid]()
    {
        struct ExitCount { std::atomic<int>& c;
            ~ExitCount() { c.fetch_sub(1); } } guard{push_workers};
        try
        {
            /* BOTH lane mutexes, not just io_mutex: a lane-0 worker
               (measured-fast binding) writes under fast_io_mutex —
               taking only io_mutex here would let a static push
               interleave on the very same controller mid-frame. */
            QMutexLocker lock(&io_mutex);
            QMutexLocker lock_fast(&fast_io_mutex);
            const std::string err = adapter.PushObject(snapshot, oid);
            if(!err.empty())
            {
                const QString msg = QString::fromStdString(err);
                QMetaObject::invokeMethod(this, [this, msg]() { setStatus(msg); },
                                          Qt::QueuedConnection);
            }
        }
        catch(...)
        {
            QMetaObject::invokeMethod(this, [this]()
            {
                setStatus(QStringLiteral("push failed: driver threw"));
            }, Qt::QueuedConnection);
        }
    }).detach();
    spawn.armed = false;
}

void SceneBridge::pushLiveAll()
{
    if(!live_output || api == nullptr)
    {
        return;
    }
    const SceneDocument snapshot = doc;
    push_workers.fetch_add(1);
    /* Same ctor-throw guard as schedulePush — a stranded count
       hangs the dtor's join. */
    struct SpawnCount { std::atomic<int>& c; bool armed = true;
        ~SpawnCount() { if(armed) c.fetch_sub(1); } } spawn{push_workers};
    std::thread([this, snapshot]()
    {
        struct ExitCount { std::atomic<int>& c;
            ~ExitCount() { c.fetch_sub(1); } } guard{push_workers};
        try
        {
            QMutexLocker lock(&io_mutex);
            QMutexLocker lock_fast(&fast_io_mutex);
            const std::string err = adapter.PushAll(snapshot);
            const QString msg = err.empty() ? QStringLiteral("live output on")
                                            : QString::fromStdString(err);
            QMetaObject::invokeMethod(this, [this, msg]() { setStatus(msg); },
                                      Qt::QueuedConnection);
        }
        catch(...)
        {
            QMetaObject::invokeMethod(this, [this]()
            {
                setStatus(QStringLiteral("push failed: driver threw"));
            }, Qt::QueuedConnection);
        }
    }).detach();
    spawn.armed = false;
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

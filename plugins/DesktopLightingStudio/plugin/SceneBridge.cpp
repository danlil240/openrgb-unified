/*---------------------------------------------------------*\
|| SceneBridge.cpp                                           |
||                                                           |
||   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "SceneBridge.h"

#include <QElapsedTimer>
#include <QThread>
#include <QTimer>
#include <QUndoCommand>
#include <QUndoStack>

#include "../scene/DefaultDesk.h"
#include "../scene/EmitterLayout.h"
#include "../scene/SceneJson.h"
#include "../effects/Presets.h"
#include "OpenRGBPluginInterface.h"

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

/*---------------------------------------------------------*\
|| Bridge                                                    |
\*---------------------------------------------------------*/
SceneBridge::SceneBridge(OpenRGBPluginAPIInterface* plugin_api, QObject* parent)
    : QObject(parent)
    , api(plugin_api)
    , adapter(plugin_api)
    , undo_stack(new QUndoStack(this))
{
    play_timer = new QTimer(this);
    play_timer->setInterval(33);
    connect(play_timer, &QTimer::timeout, this, &SceneBridge::tick);
    play_clock = new QElapsedTimer();

    doc = BuildDefaultDesk();
    refreshDevices();
}

SceneBridge::~SceneBridge()
{
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
        schedulePush();
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
    /* io_mutex + fast_io_mutex keep push workers on both lanes from
       touching the controller list mid-refresh. */
    {
        QMutexLocker lock(&io_mutex);
        QMutexLocker lock_fast(&fast_io_mutex);
        adapter.Refresh(doc);
    }
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

    /* A saved scene carries its effect state — loading restores the
       preset and resumes playback if it was playing (startup scene). */
    frame.clear();
    rebuildEffect();
    emit presetChanged();
    emit effectParamsChanged();
    refreshDevices();
    if(doc.effect.playing)
    {
        setPlaying(true);
    }
    setStatus(QStringLiteral("scene loaded (%1 objects)").arg((int)doc.objects.size()));
    if(live_output)
    {
        schedulePush();
    }
    return true;
}

void SceneBridge::resetScene()
{
    setPlaying(false);
    doc = BuildDefaultDesk();
    frame.clear();
    engine.SetLayers({});
    emit presetChanged();
    emit effectParamsChanged();
    undo_stack->clear();
    emit undoChanged();
    refreshDevices();
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
    }
    engine.SetLayers(layers);
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
        emit presetChanged();
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
    if(on)
    {
        if(engine.Empty())
        {
            rebuildEffect();
        }
        play_clock->start();
        play_timer->start();
        tick();     /* evaluate immediately — don't wait 33 ms */
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
    rebuildEffect();
    setPlaying(true);
    setStatus(QStringLiteral("remix seed %1").arg(doc.effect.seed));
    emit presetChanged();
}

void SceneBridge::setEffectSpeedPct(int pct)
{
    doc.effect.speed = qBound(10, pct, 400) / 100.0f;
    rebuildEffect();
    emit effectParamsChanged();
}

void SceneBridge::setEffectIntensityPct(int pct)
{
    doc.effect.intensity = qBound(0, pct, 100) / 100.0f;
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
        play_t += play_clock->nsecsElapsed() / 1e9;
    }
    play_clock->restart();

    engine.Evaluate(doc, play_t, frame);
    emitFrameChanged();
    schedulePush();
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
    return 33.0;
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
            pace->due_after = t1 + std::chrono::milliseconds((long long)pace->budget_ms);
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

/*---------------------------------------------------------*\
|| SceneBridge.h                                             |
||                                                           |
||   QObject bridge between the scene document and the      |
||   QML desk view. Owns the SceneDocument, the controller  |
||   adapter, the undo stack, and the live-output switch.   |
||                                                           |
||   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include <QObject>
#include <QColor>
#include <QMutex>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include "../scene/SceneTypes.h"
#include "../config/StudioConfig.h"
#include "../presets/PresetRegistry.h"
#include "../editor/EditorController.h"
#include "../output/ControllerAdapter.h"
#include "../effects/EffectEngine.h"
#include "../inputs/InputBus.h"
#include "../inputs/AudioLoopback.h"
#include "../inputs/KeyHook.h"

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>

class OpenRGBPluginAPIInterface;
class QElapsedTimer;
class QTimer;
class QUndoStack;

namespace studio
{
class ConfigStore;
class SceneObjectModel;
class ScreenSampler;

class SceneBridge : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList objectList READ objectList NOTIFY sceneChanged)
    Q_PROPERTY(QString selectedId READ selectedId NOTIFY selectionChanged)
    Q_PROPERTY(bool live READ live NOTIFY liveChanged)
    Q_PROPERTY(bool caseGhost READ caseGhost NOTIFY caseGhostChanged)
    Q_PROPERTY(int brightnessPct READ brightnessPct NOTIFY brightnessChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY undoChanged)
    Q_PROPERTY(QColor paintColor READ paintColor WRITE setPaintColor NOTIFY paintColorChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(QString activePreset READ activePreset NOTIFY presetChanged)
    Q_PROPERTY(int effectSpeedPct READ effectSpeedPct NOTIFY effectParamsChanged)
    Q_PROPERTY(int effectIntensityPct READ effectIntensityPct NOTIFY effectParamsChanged)
    Q_PROPERTY(QVariantList presetList READ presetList CONSTANT)
    Q_PROPERTY(bool audioInput READ audioInput NOTIFY inputsChanged)
    Q_PROPERTY(bool keyInput READ keyInput NOTIFY inputsChanged)
    Q_PROPERTY(bool screenInput READ screenInput NOTIFY inputsChanged)
    /* Workspace store — studio.json under the OpenRGB config dir. */
    Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged)
    Q_PROPERTY(QString documentPath READ documentPath CONSTANT)
    /* Editor (M2): stable object model + multi-selection. */
    Q_PROPERTY(QObject* objectModel READ objectModel CONSTANT)
    Q_PROPERTY(QVariantList selectedInstances READ selectedInstances NOTIFY selectionChanged)

public:
    explicit SceneBridge(OpenRGBPluginAPIInterface* api, QObject* parent = nullptr);
    ~SceneBridge() override;

    QVariantList    objectList() const;
    QString         selectedId() const { return selected; }
    bool            live() const { return live_output; }
    bool            caseGhost() const { return case_ghost; }
    int             brightnessPct() const;
    bool            canUndo() const;
    bool            canRedo() const;
    QColor          paintColor() const { return paint_color; }
    QString         statusText() const { return status; }

    /* Stage 2 — effect playback */
    bool            playing() const { return playing_state; }
    QString         activePreset() const { return QString::fromStdString(doc.effect.preset); }
    int             effectSpeedPct() const;
    int             effectIntensityPct() const;
    QVariantList    presetList() const;

    /* Stage 3 — reactive input sources */
    bool            audioInput()  const { return audio_on; }
    bool            keyInput()    const { return key_on; }
    bool            screenInput() const { return screen_on; }
    int             screenIndex() const { return screen_index; }
    int             audioSensitivityPct() const { return audio_sens_pct; }
    int             rippleDecayPct() const { return ripple_decay_pct; }

    /* Workspace persistence (config/ConfigStore). */
    bool            dirty() const;
    QString         workspaceDir() const;
    QString         documentPath() const;
    bool            hasRecovery() const;

    /* Editor (M2). */
    QObject*        objectModel() const;
    QVariantList    selectedInstances() const;

    /* Emitter dots for one object: [{x,y,z,c}] — linked objects
       return the owner's emitter layout and colors. */
    Q_INVOKABLE QVariantList emittersOf(const QString& objectId) const;

    /* Colors only, same order — the per-tick update path. Keeping it
       separate lets the QML repeater keep stable delegates instead of
       rebuilding every emitter model each frame. */
    Q_INVOKABLE QVariantList emitterColorsOf(const QString& objectId) const;

    Q_INVOKABLE QVariantMap objectInfo(const QString& objectId) const;
    Q_INVOKABLE QString     bindingReport() const;

    /* Probe support — StudioTab's flash/latency tools call these
       from their worker thread for exclusive hardware access.
       pausePushes() flips live off on the GUI thread, then holds
       both lane mutexes so no push worker can write until
       resumePushes() releases them and restores the prior state. */
    void pausePushes();
    void resumePushes();

public slots:
    void select(const QString& objectId);
    void setSelectedColor(const QColor& color);
    void paintEmitter(const QString& objectId, int index, const QColor& color);
    void setBrightnessPct(int pct);
    void setLive(bool on);
    void setCaseGhost(bool on);
    void setPaintColor(const QColor& color);
    void undo();
    void redo();
    void refreshDevices();
    bool saveScene();
    bool saveSceneAs(const QString& path);
    bool loadScene();
    bool reloadScene();
    bool restoreBackup();
    bool recoverAutosave();
    void discardRecovery();
    void resetScene();

    /* Stage 2 — effect playback */
    void playPreset(const QString& presetId);
    void setPlaying(bool on);
    void stopEffect();
    void remix();
    void setEffectSpeedPct(int pct);
    void setEffectIntensityPct(int pct);

    /* Stage 3 — reactive input sources */
    void setAudioInput(bool on);
    void setKeyInput(bool on);
    void setScreenInput(bool on);
    void setScreenIndex(int index);
    void setAudioSensitivityPct(int pct);
    void setRippleDecayPct(int pct);

    /* Milestone 2 — editor gestures and instance ops. Every edit
       lands on the authoring `workspace`, resolves into `doc`,
       and pushes ONE undo command on commit. */
    Q_INVOKABLE void selectInstance(const QString& id, bool additive);
    Q_INVOKABLE void clearEditorSelection();
    Q_INVOKABLE void beginTransformGesture();
    /* plane: 0 free, 1 desk XZ, 2 front XY, 3 side YZ. */
    Q_INVOKABLE void updateTransformGesture(double dx, double dy, double dz,
                                            int plane, bool snap);
    Q_INVOKABLE void updateRotateGesture(double degrees, bool snap);
    Q_INVOKABLE void updateRotateGestureAxis(double ax, double ay, double az,
                                             double degrees, bool snap);
    Q_INVOKABLE void commitTransformGesture();
    Q_INVOKABLE void cancelTransformGesture();
    Q_INVOKABLE bool setInstancePosition(const QString& id,
                                         double x, double y, double z);
    Q_INVOKABLE bool setInstanceRotation(const QString& id,
                                         double rx, double ry, double rz);
    Q_INVOKABLE bool renameInstance(const QString& id, const QString& newId);
    Q_INVOKABLE void setInstanceVisible(const QString& id, bool on);
    Q_INVOKABLE void setInstanceLocked(const QString& id, bool on);
    Q_INVOKABLE void groupSelected();
    Q_INVOKABLE void ungroupSelected();
    Q_INVOKABLE void deleteSelected();
    Q_INVOKABLE void duplicateMirrored();

signals:
    void sceneChanged();
    void emittersChanged(const QString& objectId);
    void selectionChanged();
    void liveChanged();
    void caseGhostChanged();
    void brightnessChanged();
    void undoChanged();
    void paintColorChanged();
    void statusChanged();
    void statusMessage(const QString& text);      /* -> results box        */
    void playingChanged();
    void presetChanged();
    void effectParamsChanged();
    void inputsChanged();
    void dirtyChanged();
    /* studio.json changed on disk (not our write); arg = dirty. */
    void externalChangeDetected(bool dirty);
    /* A valid autosave differing from studio.json exists. */
    void recoveryAvailable();

private:
    friend class SceneColorCommand;
    friend class SceneEmitterCommand;
    friend class SceneBrightnessCommand;
    friend class SceneEditCommand;

    /* Non-undoable core ops used by undo commands and public slots. */
    void applyObjectColor(const std::string& owner_id, SceneColor color);
    void applyEmitterColor(const std::string& owner_id, int index, SceneColor color);
    void applyBrightness(float brightness);

    /* Push a zone to hardware on a serialized worker when live. */
    void pushLive(const std::string& object_id);
    void pushLiveAll();

    /* Stage 2 playback */
    void rebuildEffect();                  /* layers from doc.effect      */
    void tick();                           /* evaluate + repaint + push   */
    void schedulePush();                   /* newest-frame coalesced push */
    void scheduleLane(int lane);           /* spawn a lane push worker    */
    void runPushLane(int lane, const SceneDocument& dc,
                     const FrameColors& fc);           /* worker: due sweep */
    bool BindingIsI2C(const std::string& binding_id) const;   /* read-only; UI + workers */
    double BindingMinPace(const std::string& binding_id) const; /* read-only; UI + workers */
    void emitFrameChanged();               /* emittersChanged for frame   */

    void rebuildMatrixLayouts();
    void rebuildKeyLookup();             /* vk -> emitter world pos    */
    void setStatus(const QString& text);

    /* Workspace document store. CurrentWorkspace snapshots runtime
       state; ApplyWorkspace commits a validated candidate (never
       touches live_output). markDirty drives the autosave.
       ReloadPresets refreshes the type library (packaged defaults
       under the workspace's presets/devices/ files) so a Reload
       picks up edited type files. */
    StudioDocument CurrentWorkspace() const;
    void           ApplyWorkspace(const StudioDocument& w);
    bool           LoadWorkspace();
    void           ReloadPresets();
    void           markDirty();

    /* Editor plumbing (M2). SyncWorkspace mirrors the runtime
       overlay (colors/effect/brightness/inputs) into `workspace`
       so controller edits never lose paint; ResolveWorkspace runs
       the authoring doc through SceneResolver; AdoptResolved
       swaps in the resolved scene and refreshes the model,
       matrix layouts and key lookup; applyEdit is the undo path;
       commitEdit resolves + adopts + pushes one undo command;
       previewAdopt is the dirty-free gesture path. */
    void SyncWorkspace();
    /* Drop selection ids the workspace no longer has (undo/redo,
       rollback paths). `selected` keeps its stored OBJECT id while
       its owning instance remains selected (sub-object granularity
       survives undo); it falls back to the primary instance id only
       when the object no longer resolves. */
    void PruneSelection();
    bool ResolveWorkspace(SceneDocument& out);
    void AdoptResolved(const SceneDocument& r, const EditorEdit& e);
    void applyEdit(const EditorEdit& e, bool reverse);
    void commitEdit(EditorEdit&& e);
    void previewAdopt(const std::set<std::string>& ids);

    OpenRGBPluginAPIInterface*  api;
    ControllerAdapter           adapter;
    SceneDocument               doc;              /* resolved runtime scene  */
    StudioDocument              workspace;        /* compact authoring state */
    PresetRegistry              registry;         /* device-type library     */
    EditorController            editor;           /* edits `workspace`       */
    SceneObjectModel*           obj_model = nullptr; /* stable list model    */
    QUndoStack*                 undo_stack;
    ConfigStore*                store = nullptr;
    WorkspaceMeta               meta;             /* prefs + retained sections */
    bool                        startup_load_done = false;

    QString                     selected;
    bool                        live_output = false;
    bool                        case_ghost  = false;
    QColor                      paint_color = Qt::white;
    QString                     status;
    QMutex                      io_mutex;

    /* Stage 2 playback state. `frame` holds the last evaluated effect
       colors (owner id -> per-emitter); it stays valid while paused so
       preview and hardware keep the frozen frame. `play_t` is atomic:
       input providers read it (via the bus clock) off the UI thread. */
    EffectEngine                engine;
    FrameColors                 frame;
    FrameColors                 last_frame;   /* last emitted+pushed frame  */
    bool                        frame_sent  = false;
    QTimer*                     play_timer  = nullptr;
    QElapsedTimer*              play_clock  = nullptr;
    std::atomic<double>         play_t      { 0.0 };
    bool                        playing_state = false;
    /* Frame pushes run on two serialized lanes so a slow transport
       can't starve fast devices: lane 1 owns I2C/SMBus bindings and
       anything not yet measured; lane 0 owns measured-fast USB/HID
       bindings. Each lane keeps newest-frame coalescing and paces its
       bindings independently — a binding's budget is its measured
       write cost * 1.3 (clamped 16..750 ms) counted from the write's
       start, and 40/80 ms hysteresis migrates non-I2C bindings
       between lanes. */
    struct PushPace
    {
        std::chrono::steady_clock::time_point due_after {};
        double                              budget_ms   = 16.0;
        double                              min_pace_ms = 16.0;
        int                                 lane        = -1;  /* -1: unmeasured -> slow */
    };
    std::map<std::string, PushPace>         push_pace;       /* under pace_mutex    */
    QMutex                                  pace_mutex;      /* guards push_pace    */
    std::atomic<bool>                       push_in_flight { false }; /* static PushAll */
    std::atomic<bool>                       push_again     { false };
    std::atomic<bool>                       lane_in_flight[2] { false, false };
    std::atomic<bool>                       lane_again[2]     { false, false };
    QMutex                                  fast_io_mutex;   /* lane 0 writes */
    std::string                             last_push_err;   /* UI thread */

    /* Held for a probe's whole run; see pausePushes()/resumePushes(). */
    std::unique_ptr<std::unique_lock<QMutex>> probe_lane_locks[2];
    bool                                      probe_was_live = false;

    /* Stage 3 — reactive inputs. The bus stamps events on the play
       clock so ring ages are consistent with evaluation time. */
    InputBus                              input_bus;
    AudioLoopback                         audio_in;
    KeyHook                               key_in;
    ScreenSampler*                        screen_in  = nullptr;
    bool                                  audio_on   = false;
    bool                                  key_on     = false;
    bool                                  screen_on  = false;
    int                                   screen_index      = 0;
    int                                   audio_sens_pct    = 100;
    int                                   ripple_decay_pct  = 100;
    std::map<int, Vec3>                   key_pos;     /* vk -> world pos */
    std::string                           last_input_status;
};

} /* namespace studio */

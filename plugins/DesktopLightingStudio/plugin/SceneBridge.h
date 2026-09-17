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
#include <QStringList>
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
#include <optional>
#include <set>
#include <string>

class OpenRGBPluginAPIInterface;
class QElapsedTimer;
class QTimer;
class QUndoStack;

namespace studio
{
class ConfigStore;
class EffectLayerModel;
class PresetListModel;
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
    /* Effect-layer editing (task 5.2): the EFFECTIVE stack as a
       list model (inline layers when authored, else the resolved
       look) plus whether the stack is authored inline. Both refresh
       on effectLayersChanged (emitted by rebuildEffect). */
    Q_PROPERTY(QObject* effectLayerModel READ effectLayerModel CONSTANT)
    Q_PROPERTY(bool effectStackInline READ effectStackInline NOTIFY effectLayersChanged)
    Q_PROPERTY(bool audioInput READ audioInput NOTIFY inputsChanged)
    Q_PROPERTY(bool keyInput READ keyInput NOTIFY inputsChanged)
    Q_PROPERTY(bool screenInput READ screenInput NOTIFY inputsChanged)
    /* Workspace store — studio.json under the OpenRGB config dir. */
    Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged)
    Q_PROPERTY(QString documentPath READ documentPath CONSTANT)
    /* Editor (M2): stable object model + multi-selection. */
    Q_PROPERTY(QObject* objectModel READ objectModel CONSTANT)
    Q_PROPERTY(QVariantList selectedInstances READ selectedInstances NOTIFY selectionChanged)
    /* Device-type library (task 4.2): PresetRegistry listing as a
       QAbstractListModel; presetLibraryChanged fires on reload and
       on favorite toggles so the QML panel re-reads rowAt(). */
    Q_PROPERTY(QObject* presetModel READ presetModel NOTIFY presetLibraryChanged)
    /* Camera pose/view — editor prefs persisted in meta.camera;
       never undo history, never the scene. */
    Q_PROPERTY(QVariantMap cameraState READ cameraState NOTIFY cameraChanged)
    /* Render prefs (meta.render) — the viewport quality tier and
       the display-only bloom toggle. */
    Q_PROPERTY(QString renderQuality READ renderQuality WRITE setRenderQuality NOTIFY renderPrefsChanged)
    Q_PROPERTY(bool renderBloom READ renderBloom WRITE setRenderBloom NOTIFY renderPrefsChanged)

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
    /* Q_INVOKABLE so the QML workspace shell (LookShelf inputs row)
       can seed its controls — the setters below are already slots. */
    Q_INVOKABLE int screenIndex() const { return screen_index; }
    Q_INVOKABLE int audioSensitivityPct() const { return audio_sens_pct; }
    Q_INVOKABLE int rippleDecayPct() const { return ripple_decay_pct; }

    /* Workspace ui prefs (meta.ui) — the shell's Theme reads the
       reduced-motion flag through this. */
    Q_INVOKABLE bool reducedMotion() const { return meta.ui.reduced_motion; }

    /* Workspace persistence (config/ConfigStore). */
    bool            dirty() const;
    QString         workspaceDir() const;
    QString         documentPath() const;
    bool            hasRecovery() const;

    /* Editor (M2). */
    QObject*        objectModel() const;
    QVariantList    selectedInstances() const;
    QVariantMap     cameraState() const;
    /* Device-type library (task 4.2). */
    QObject*        presetModel() const;

    /* Render prefs (meta.render): low|balanced|high tier +
       display-only bloom flag. Dirty/autosave path like
       setCameraState — never undo, never the scene. */
    QString         renderQuality() const { return QString::fromStdString(meta.render.quality); }
    bool            renderBloom() const { return meta.render.bloom; }
    void            setRenderQuality(const QString& q);
    void            setRenderBloom(bool on);

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
       pausePushes() flips live off (live_output is atomic; the
       liveChanged notify is queued so nothing blocks on the GUI
       thread pumping events — a BlockingQueuedConnection here
       deadlocks a probe when the bridge is mid-destruction), then
       holds a probe serializer + both lane mutexes so no push
       worker can write until resumePushes() releases them and
       restores live — only when nothing else touched the switch
       meanwhile (an explicit user toggle during the probe wins).
       Overlapping probes serialize on probe_serial: the second
       pauser records live AFTER the first restored it.
       pausePushes() returns false once shutdown began — callers
       must skip their hardware run; resumePushes() is only valid
       after a true return. */
    bool pausePushes();
    void resumePushes();
    /* True once ~SceneBridge began teardown — push workers bail
       between bindings and probe loops should abort their write
       cycles so the destructor's join stays bounded. */
    bool closing() const { return shutting_down.load(); }
    /* Preview visibility gate (GUI thread — StudioTab show/hide +
       window exposure). Hidden preview: tick() skips the
       emittersChanged repaint churn while live pushes continue;
       with live OFF the play timer stops entirely — nothing
       consumes the frame. */
    void setPreviewVisible(bool on);

public slots:
    void select(const QString& objectId);
    void setSelectedColor(const QColor& color);
    void paintEmitter(const QString& objectId, int index, const QColor& color);
    void setBrightnessPct(int pct);
    void setLive(bool on);
    void setCaseGhost(bool on);
    void setPaintColor(const QColor& color);
    /* Both refuse (statusMessage hint, gesture survives) while a
       transform gesture is live — see gestureActive(). */
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
    /* Milestone 5 — the persisted inline layer stack. When
       doc.effect.layers is non-empty it wins over `preset` at
       rebuild (preset keeps provenance); selecting a preset or
       stopping the effect clears it. applyEffectLayers replaces
       the stack wholesale — callers pass validated resolved
       literals (effects/EffectJson.h grammar); the 5.2 layer
       editor commits through here. */
    void applyEffectLayers(const std::vector<EffectLayer>& layers);
    const std::vector<EffectLayer>& effectLayers() const
    {
        return doc.effect.layers;
    }

    /* Task 5.2 — effect-layer editing. Read side: the EFFECTIVE
       stack (authored inline layers when present, else the resolved
       named-look stack) as a list model + per-layer detail maps.
       Write side: every op funnels through the Qt-free
       EditorController into commitEdit — discrete ops are one undo
       record each; continuous gestures (slider scrub, stop/origin/
       path drags) run begin -> preview writes -> commit, one record
       per completed gesture. The first edit on a preset-backed
       workspace materializes the resolved stack inline inside the
       same record. */
    QObject*        effectLayerModel() const;
    bool            effectStackInline() const
    {
        return !doc.effect.layers.empty();
    }
    Q_INVOKABLE int         effectLayerCount() const;
    Q_INVOKABLE QVariantMap effectLayer(int index) const;
    /* Valid target terms for the layer editor: object ids, geometry
       tags and emitter groups of the resolved scene. */
    Q_INVOKABLE QVariantList effectTargetIds() const;
    /* Input-source availability for one source name
       ("audio"|"key"|"screen"): {name, enabled, ready, status} —
       the editor's requirement badges read this so a disconnected
       provider is visible beside the layer/effect that needs it. */
    Q_INVOKABLE QVariantMap inputSourceState(const QString& source) const;
    Q_INVOKABLE bool effectGestureActive() const
    {
        return editor.LayerGestureActive();
    }
    Q_INVOKABLE void beginEffectGesture();
    Q_INVOKABLE void commitEffectGesture(const QString& label);
    Q_INVOKABLE void cancelEffectGesture();
    Q_INVOKABLE void moveEffectLayer(int from, int to);
    Q_INVOKABLE void addEffectLayer(const QString& primitive);
    Q_INVOKABLE void removeEffectLayer(int index);
    Q_INVOKABLE void setEffectLayerEnabled(int index, bool on);
    Q_INVOKABLE void setEffectLayerBlend(int index,
                                         const QString& blend);
    Q_INVOKABLE void setEffectLayerOpacity(int index, double v);
    /* field: "speed" | "scale" | "phase" | "density" | "seed" */
    Q_INVOKABLE void setEffectLayerField(int index,
                                         const QString& field,
                                         double v);
    Q_INVOKABLE void setEffectLayerSpace(int index, bool local);
    Q_INVOKABLE void setEffectLayerOrigin(int index,
                                          double x, double y, double z);
    Q_INVOKABLE void setEffectLayerDirection(int index,
                                             double x, double y, double z);
    Q_INVOKABLE void setEffectLayerSource(int index,
                                          const QString& source);
    Q_INVOKABLE void setEffectLayerTargets(int index,
                                           const QStringList& targets);
    /* Palette stops: pos in 0..1 (strictly increasing after the
       controller's re-sort), color "#RRGGBB". */
    Q_INVOKABLE void addEffectLayerStop(int index, double pos,
                                        const QString& color);
    Q_INVOKABLE void removeEffectLayerStop(int index, int stop);
    /* Returns the dragged stop's index AFTER the controller's
       re-sort so a position scrub keeps hold of the same stop —
       the QML side tracks it by this return, not by row index. */
    Q_INVOKABLE int  moveEffectLayerStop(int index, int stop,
                                         double pos);
    Q_INVOKABLE void setEffectLayerStopColor(int index, int stop,
                                             const QString& color);
    /* Comet path points, meters. */
    Q_INVOKABLE void addEffectLayerPathPoint(int index,
                                             double x, double y, double z);
    Q_INVOKABLE void setEffectLayerPathPoint(int index, int pt,
                                             double x, double y, double z);
    Q_INVOKABLE void removeEffectLayerPathPoint(int index, int pt);
    /* Reset-to-preset: clears the authored inline stack so the
       named look resolves again (one undoable record). */
    Q_INVOKABLE void resetEffectLayers();
    /* Save-as personal look: validates + writes
       presets/effects/<id>.effect.json through
       ConfigStore::WriteEffectFile, reloads the look registry, then
       adopts it as one undoable edit (effects.preset = id, inline
       stack cleared). Returns {ok, id, path} or {ok:false, errors}. */
    Q_INVOKABLE QVariantMap saveLookAs(const QString& id,
                                       const QString& name);

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
    /* True while a transform gesture is live (post-begin,
       pre-commit/cancel) — the QML input router and tests
       gate gesture promotion on it. */
    Q_INVOKABLE bool gestureActive() const { return editor.GestureActive(); }
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
    /* Instance ids a deleteSelected() would remove — the selected
       roots plus their cascade children (spec §4: deleting a group
       shows which children go with it). The device tree lists these
       in its confirm prompt; deleteSelected() still performs the
       real locked-descendant refusal. */
    Q_INVOKABLE QStringList deletePreview() const;
    Q_INVOKABLE void duplicateMirrored();

    /* Task 4.2 — device library. addDeviceInstance drops a new
       unbound root instance of `typeId` at (x, z); `y` is the desk
       surface height the caller measured — the bridge clamps so the
       type's footprint floor rests on it. One undoable op; the new
       instance becomes the selection. setTypeFavorite is a UI pref
       (meta.ui.favorites, dirty path, NOT undoable).
       saveInstanceAsVariant writes the instance's resolved type to
       presets/devices/<newTypeId>.device.json with the instance's
       painted object colors baked into entity appearance.body_color,
       then repoints the instance — one undoable structural edit.
       createTypeFromSelection writes a child-reference type from an
       explicit list of root instance ids (the caller passes the
       selection); writing the file is enough (the original
       placements stay put — undoing the write is just deleting the
       file). All four refuse invalid input without touching scene,
       workspace or files, and explain via statusMessage. */
    Q_INVOKABLE void addDeviceInstance(const QString& typeId,
                                       double x, double y, double z);
    Q_INVOKABLE void setTypeFavorite(const QString& typeId, bool fav);
    Q_INVOKABLE void saveInstanceAsVariant(const QString& instanceId,
                                           const QString& newTypeId,
                                           const QString& displayName);
    Q_INVOKABLE void createTypeFromSelection(
        const QStringList& instanceIds, const QString& newTypeId,
        const QString& displayName);

    /* Task 4.3 — device preset editor. The candidate lives in the
       QML editor as a plain JS object (the *.device.json shape);
       these calls validate / preview / persist it WITHOUT touching
       workspace, doc, bindings or hardware. previewPreset resolves
       a throwaway one-instance document against a private registry
       copy so the live scene and the library are never disturbed.
       savePresetType writes only presets/devices/<id>.device.json
       through ConfigStore::WritePresetFile (which re-validates on
       read-back), then reloads the library and re-resolves the
       workspace so existing instances adopt the new shape —
       led_count shrink vs bound hardware is returned as a warning,
       never auto-resized. */
    Q_INVOKABLE QVariantMap presetDocument(const QString& typeId) const;
    Q_INVOKABLE QVariantMap validatePreset(const QVariantMap& candidate) const;
    Q_INVOKABLE QVariantMap previewPreset(const QVariantMap& candidate) const;
    Q_INVOKABLE QVariantMap savePresetType(const QVariantMap& candidate,
                                           bool asNew);
    /* "Convert generated layout to points": bakes the zone's
       generated emitters (positions + addresses) into layout.points;
       returns {ok, candidate} or {ok:false, errors}. */
    Q_INVOKABLE QVariantMap convertZoneToPoints(const QVariantMap& candidate,
                                                int zoneIndex);
    /* Controller/zone pickers for the editor's Binding section —
       the adapter snapshot the binding resolver itself uses. */
    Q_INVOKABLE QVariantList hardwareControllers() const;
    /* Per-instance zone binding rows for the editor (type zones +
       current binding/param state). Empty map for unknown ids. */
    Q_INVOKABLE QVariantMap instanceZoneState(const QString& instanceId) const;
    /* Undoable per-instance binding writes — through the editor
       controller into device_settings.zones / bindings. The type's
       zones and hardware zone sizes are never touched. */
    Q_INVOKABLE void bindZoneToController(const QString& instanceId,
                                          const QString& zoneId,
                                          int controller, int zone,
                                          int addrBase, bool verified);
    Q_INVOKABLE void unbindZone(const QString& instanceId,
                                const QString& zoneId);
    Q_INVOKABLE void setZoneParams(const QString& instanceId,
                                   const QString& zoneId,
                                   int addrBase, bool verified);
    /* alignSelected(axis, mode): axis 0=X/1=Y/2=Z; mode 0=min,
       1=center, 2=max. distributeSelected(axis): even spacing,
       endpoints hold. Both are one undo record. */
    Q_INVOKABLE void alignSelected(int axis, int mode);
    Q_INVOKABLE void distributeSelected(int axis);
    /* Per-instance inspector state: authored local transform +
       visible/locked flags. Empty map for unknown ids. */
    Q_INVOKABLE QVariantMap instanceState(const QString& id) const;

    /* Task 4.4 — portable bundles + explicit type reload.
       exportBundle writes <dir>/studio.json (sanitized: no
       serials/locations, verified + live flags off) plus one file
       per used type and the referenced assets. inspectBundle
       validates a bundle without writing and reports per-type
       status (new|identical|conflict); importBundle applies it —
       `choices` maps each conflict id to its import-as id — then
       loads the returned candidate through the same
       resolve-then-apply path LoadWorkspace uses, so a rejected
       import leaves scene/inputs/output untouched. Imported
       bindings never carry verified/live state — the status line
       says they need local resolution. presetIdAvailable feeds
       the conflict dialog's new-id field. reloadDeviceTypes
       re-reads the type library and re-resolves so every instance
       of an edited type updates together — placements untouched. */
    Q_INVOKABLE QVariantMap inspectBundle(const QString& dirPath);
    Q_INVOKABLE bool        exportBundle(const QString& dirPath,
                                         bool overwrite);
    Q_INVOKABLE bool        importBundle(const QString& dirPath,
                                         const QVariantMap& choices);
    Q_INVOKABLE void        reloadDeviceTypes();
    Q_INVOKABLE bool        presetIdAvailable(const QString& id) const;
    /* Persist a camera gesture's final pose into meta.camera
       (editor prefs — dirty/autosave path, never undo). Keys:
       view, projection, tx/ty/tz, yaw, pitch, distance, span.
       cameraState() also carries controls.middle_drag so the
       QML router can honor the pan|orbit pref. */
    Q_INVOKABLE void setCameraState(const QVariantMap& state);

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
    /* Effective layer stack changed (commit, gesture preview,
       undo/redo, preset pick, reload) — the layer editor panels
       re-read effectLayerModel/effectLayer/effectStackInline. */
    void effectLayersChanged();
    void inputsChanged();
    void dirtyChanged();
    /* studio.json changed on disk (not our write); arg = dirty. */
    void externalChangeDetected(bool dirty);
    /* A valid autosave differing from studio.json exists. */
    void recoveryAvailable();
    /* meta.camera changed (setCameraState or a workspace load). */
    void cameraChanged();
    /* meta.render changed (setRenderQuality/setRenderBloom or a
       workspace load). */
    void renderPrefsChanged();
    /* Preset library content or ui.favorites changed — the
       DeviceLibrary panel re-reads presetModel. */
    void presetLibraryChanged();

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
    /* LoadPresetDefaults refreshes the packaged defaults layer
       from the bundled qrc presets/devices/*.device.json files
       (the authoritative type library); the minimal C++ set is
       the fallback when nothing readable ships. ReloadPresets
       re-reads defaults + the workspace's presets/devices/ files
       so a Reload picks up edited type files. */
    void           LoadPresetDefaults();
    /* LoadEffectDefaults refreshes the packaged effect-look
       defaults from bundled qrc presets/effects/*.effect.json
       (the authoritative look library); the single minimal
       built-in look is the fallback when nothing readable
       ships. ReloadPresets re-reads both defaults layers plus
       the workspace's presets/devices/ + presets/effects/ files. */
    void           LoadEffectDefaults();
    void           ReloadPresets();
    void           markDirty();

    /* Editor plumbing (M2). SyncWorkspace mirrors the runtime
       overlay (colors/effect/brightness/inputs) into `workspace`
       so controller edits never lose paint; ResolveWorkspace runs
       the authoring doc through SceneResolver; AdoptResolved
       swaps in the resolved scene and refreshes the model,
       matrix layouts and key lookup — plus the adapter's resolved
       bindings when the edit carries a bindings delta, so a
       freshly minted binding reaches output/verify in-session;
       applyEdit is the undo path; commitEdit resolves + adopts +
       pushes one undo command; previewAdopt is the dirty-free
       gesture path. */
    void SyncWorkspace();
    /* Effect-gesture preview: mirror the workspace's previewed
       effect state into doc + rebuild the engine — no dirty, no
       record. SyncWorkspace must NOT run here (doc.effect is stale
       mid-gesture; copying it back would clobber the preview). */
    void PreviewEffectSync();
    /* Route a controller op result: a record commits through
       commitEdit; no record inside a live layer gesture means
       preview — mirror it; a refusal reaches the status line. */
    void ApplyEffectOp(std::optional<EditorEdit>&& e);
    void RefreshEffectModel();
    /* The stack the engine runs: authored inline layers when
       present, else the resolved named-look stack (unscaled —
       global speed/intensity apply on the engine copy). */
    std::vector<EffectLayer> EffectiveLayers() const;
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
    /* True when LoadPresetDefaults had to fall back to the minimal
       C++ type set (no readable packaged *.device.json) — the
       default-workspace builder then swaps to the resolvable
       recovery desk. */
    bool                        using_fallback_types = false;
    /* True when LoadEffectDefaults found no readable packaged
       *.effect.json — the registry's single built-in look stands
       in (see Presets.cpp's fallback doc). */
    bool                        using_fallback_effects = false;
    EditorController            editor;           /* edits `workspace`       */
    SceneObjectModel*           obj_model = nullptr; /* stable list model    */
    PresetListModel*            preset_model = nullptr; /* type library rows  */
    EffectLayerModel*           layer_model = nullptr; /* effective fx stack */
    QUndoStack*                 undo_stack;
    ConfigStore*                store = nullptr;
    WorkspaceMeta               meta;             /* prefs + retained sections */
    bool                        startup_load_done = false;

    QString                     selected;
    /* Atomic: pausePushes() flips it off from a probe thread (the
       GUI-side notify is queued); all other access stays on the
       bridge thread. */
    std::atomic<bool>           live_output { false };
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

    /* Held for a probe's whole run; see pausePushes()/resumePushes().
       probe_serial goes first so overlapping probes serialize BEFORE
       either records live state — a second pauser then sees the
       restored switch, not the paused one. probe_active counts every
       pause attempt (incl. one blocked on probe_serial) so the dtor
       can wait probes out instead of freeing mutexes under them. */
    QMutex                                    probe_serial;
    std::unique_ptr<std::unique_lock<QMutex>> probe_serial_lock;
    std::unique_ptr<std::unique_lock<QMutex>> probe_lane_locks[2];
    bool                                      probe_was_live = false;
    std::atomic<int>                          probe_active  { 0 };

    /* Lifecycle quiesce: every detached push worker increments
       push_workers at spawn (GUI thread — the dtor runs on the same
       thread, so the count can't be raced up mid-teardown) and
       decrements on exit; shutting_down tells them to bail between
       bindings and makes late pausePushes() calls fail. */
    std::atomic<int>                          push_workers  { 0 };
    std::atomic<bool>                         shutting_down { false };

    /* Preview visibility — GUI thread only (setPreviewVisible).
       Gates the preview repaint in tick(); hardware pushes are
       unaffected. */
    bool                                      preview_visible = true;

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

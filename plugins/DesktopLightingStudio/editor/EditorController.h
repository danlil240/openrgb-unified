/*---------------------------------------------------------*\
|| EditorController.h                                        ||
||                                                           ||
||   Qt-free editor state: selection, transform gestures  ||
||   and every discrete workspace edit. All ops mutate    ||
||   the compact AUTHORING document (StudioDocument) —    ||
||   never the resolved runtime scene — and return an     ||
||   EditorEdit the caller pushes onto the undo stack.    ||
||   std::nullopt means the op was a no-op or rejected    ||
||   (missing/locked ids) and produces NO history entry.  ||
||                                                           ||
||   Rules this enforces:                                  ||
||   - Ops target ROOT instance ids only. A resolved      ||
||     object id maps to its owning instance through the  ||
||     first '/' segment (InstanceOf). Nested             ||
||     <parent>/<entity> child devices are type-owned     ||
||     internals — type authoring, not placement.         ||
||   - Locked instances (device_settings.locked) stay     ||
||     selectable for display but are excluded from the   ||
||     movable set: transform ops and grouping skip them. ||
||     Locks are protective beyond direct edits: Delete   ||
||     REFUSES when its descendant cascade would reach a  ||
||     locked instance; Ungroup refuses when the group    ||
||     holds a locked child; Rename refuses when the      ||
||     instance has a locked child (re-keying a child's   ||
||     parent is a reparent). LastError carries the       ||
||     refusal reason for the UI status line.             ||
||   - A gesture snapshots the movable instances once at  ||
||     BeginTransform; previews recompute from that       ||
||     snapshot; Commit yields ONE record; Cancel         ||
||     restores the snapshot with zero records. A second  ||
||     BeginTransform while a gesture is live cancels the ||
||     old one first. Commit/Cancel only touch ids still  ||
||     present in the document — a mid-gesture doc swap   ||
||     can never resurrect or corrupt instances.          ||
||   - Hierarchy (parent) and output ownership            ||
||     (mirror_of) stay separate; DuplicateMirrored only  ||
||     ever creates a Linked copy, never a second zone    ||
||     writer.                                            ||
||                                                           ||
||   SPDX-License-Identifier: GPL-2.0-or-later               ||
\*---------------------------------------------------------*/

#pragma once

#include "TransformCommands.h"

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace studio
{
class PresetRegistry;
struct DevicePreset;

/* Plane a translate gesture is constrained to; Free keeps all
   axes. DeskXZ is the desk top (the default drag plane). */
enum class EditPlane
{
    Free,
    DeskXZ,     /* Y locked — the desk surface            */
    FrontXY,    /* Z locked                               */
    SideYZ,     /* X locked                               */
};

class EditorController
{
public:
    /* `ws` is the authoring document this controller edits; it
       must outlive the controller. */
    explicit EditorController(StudioDocument& ws) : ws(ws) {}

    /* Owning root instance of a resolved object id: the first
       '/' segment ("fan0/ring" -> "fan0", "fan0" -> "fan0"). */
    static std::string InstanceOf(const std::string& object_id);

    bool Exists(const std::string& id) const;
    bool IsLocked(const std::string& id) const;
    bool IsVisible(const std::string& id) const;

    /* Human-readable reason of the most recent refusal that set one
       (delete reaching a locked descendant, ungroup/rename touching a
       locked child, ...). Empty when the last op did not refuse with a
       message. */
    const std::string& LastError() const { return last_error; }

    /*------------------------------------------------*\
    || Selection — ordered instance ids, back() is the ||
    || primary (most recent). Locked instances may be  ||
    || selected for display; Movable() filters them    ||
    || out of edit ops.                                ||
    \*------------------------------------------------*/
    bool Select(const std::string& object_or_instance_id,
                bool additive = false);
    void SetSelection(const std::vector<std::string>& ids);
    void ClearSelection();
    const std::vector<std::string>& Selection() const { return selection; }
    std::string PrimarySelection() const;
    bool IsSelected(const std::string& id) const;

    /*------------------------------------------------*\
    || Transform gesture: BeginTransform snapshots the ||
    || movable selection; Preview* recompute absolute  ||
    || transforms FROM that snapshot (the delta param  ||
    || is the total gesture delta, not an increment);  ||
    || Commit returns the single edit record; Cancel   ||
    || restores the snapshot.                          ||
    \*------------------------------------------------*/
    bool BeginTransform();
    bool GestureActive() const { return gesture.active; }
    void PreviewTranslate(const Vec3& world_delta,
                          EditPlane plane = EditPlane::DeskXZ,
                          bool snap = false);
    /* Rotate the movable selection `degrees` around `axis`
       through the shared pivot (world space; desk default is
       the Y axis). */
    void PreviewRotate(const Vec3& axis, float degrees, bool snap = false);
    std::optional<EditorEdit> Commit();
    void Cancel();
    const std::set<std::string>& GestureIds() const { return gesture.movable; }

    /*------------------------------------------------*\
    || Discrete ops — each applies to ws and returns   ||
    || the record (nullopt = rejected/no-op).          ||
    \*------------------------------------------------*/
    std::optional<EditorEdit> SetPosition(const std::string& id, const Vec3& pos);
    std::optional<EditorEdit> SetRotation(const std::string& id, const Vec3& rot_deg);
    /* World-space numeric edit — converted into the instance's
       parent frame. */
    std::optional<EditorEdit> SetPositionWorld(const std::string& id, const Vec3& pos);
    std::optional<EditorEdit> Rename(const std::string& id, const std::string& new_id);
    std::optional<EditorEdit> SetVisible(const std::string& id, bool on);
    std::optional<EditorEdit> SetLocked(const std::string& id, bool on);
    /* Cascade-deletes instances and their descendants; strips
       dangling mirror_of references. Locked ids are skipped. */
    std::optional<EditorEdit> Delete(const std::vector<std::string>& ids);
    std::optional<EditorEdit> DeleteSelected();
    /* Pure read of Delete's kill set: the ids' root instances
       (existing + unlocked) plus every descendant that would ride
       along. The UI lists this before confirming (spec §4); the
       real Delete still performs its own locked-descendant
       refusal. */
    std::set<std::string> DeleteCascade(const std::vector<std::string>& ids) const;
    /* New "group"-type instance at the selection's world pivot;
       children keep world placement via parent-local
       conversion. When every member shares the same non-empty
       parent the group nests under it (so a later move of that
       parent carries the members); mixed or root-level parents
       root the group. */
    /* Align the movable selection on a world axis: mode 0 = min,
       1 = center (mean), 2 = max. Each member keeps its other axes;
       the new world position is converted into its parent frame.
       One record for the whole op. */
    std::optional<EditorEdit> Align(int axis /*0=X,1=Y,2=Z*/, int mode);
    /* Evenly space the movable selection along a world axis — the
       two extremes keep their positions, interior members land at
       equal intervals in sorted order. Needs >= 3 movable. */
    std::optional<EditorEdit> Distribute(int axis);

    std::optional<EditorEdit> Group(const std::string& base_id = "group");
    /* Dissolves selected type-"group" instances: children move to
       the group's parent with world placement preserved.
       Non-group selections are skipped. */
    std::optional<EditorEdit> Ungroup();
    /* Mirrored visual copy per selected instance: new unique id,
       same type + parent, small offset, device_settings.mirror_of
       wired to the output owner — never zones/bindings (a second
       writer for the same LEDs is never created). */
    std::optional<EditorEdit> DuplicateMirrored();

    /*------------------------------------------------*\
    || Device-library ops (task 4.2). AddInstance drops  ||
    || a new UNBOUND root instance of `type_id` at pos   ||
    || (the caller resolves/clamps placement; the type   ||
    || id is validated against the registry up in the    ||
    || bridge). The generated id is UniqueId(type_id)    ||
    || and becomes the selection. Retype repoints an     ||
    || instance's type reference — the Save-variant      ||
    || follow-up; same-type and unknown ids are no-ops.  ||
    \*------------------------------------------------*/
    std::optional<EditorEdit> AddInstance(const std::string& type_id,
                                          const Vec3& pos);
    std::optional<EditorEdit> Retype(const std::string& id,
                                     const std::string& new_type);

    /* Read-only builder for "Create preset from selection": fills
       `out` with one CHILD-DEVICE REFERENCE entity per id — the
       entity carries only `type` + the instance's world transform
       re-expressed relative to the shared origin (centroid of the
       instances' world positions on the desk plane, y = the lowest
       instance's y — documented choice: the new type's origin sits
       under the arrangement's center at desk height). No entities
       are copied; `out.zones` stays empty (child types carry their
       own). Refuses (false + LastError) mid-gesture, on empty input,
       on ids that aren't existing root instances, and on instances
       whose type doesn't resolve in `reg`. */
    bool BuildPresetFromInstances(const std::vector<std::string>& ids,
                                  const std::string& new_id,
                                  const std::string& name,
                                  const PresetRegistry& reg,
                                  DevicePreset& out);

    /* Snapping — defaults come from ControlsPrefs (10 mm / 15 deg). */
    static Vec3  SnapTranslate(const Vec3& v, float step_m = 0.01f);
    static float SnapAngle(float deg, float step_deg = 15.0f);

private:
    struct Gesture
    {
        bool                          active = false;
        /* movable selection at begin (unlocked, topmost ancestors) */
        std::set<std::string>         movable;
        std::map<std::string, DeviceInstance> snapshot; /* id -> begin state */
        std::map<std::string, Mat4>   world;            /* id -> begin world */
        std::map<std::string, Mat4>   parent_world;     /* id -> parent's    */
        Vec3                          pivot;            /* shared rotate pivot */
    };

    /* Selection filtered to editable instances: existing, unlocked,
       and no selected ancestor (a selected ancestor already moves
       the subtree — applying the delta twice would double-move). */
    std::set<std::string> Movable() const;

    /* Instance id -> world matrix over the devices parent chain
       (all instance transforms are rigid T*R). */
    std::map<std::string, Mat4> InstanceWorlds() const;
    Mat4 ParentFrame(const std::map<std::string, Mat4>& worlds,
                     const std::string& id) const;

    /* Unique id: base, then base_2, base_3, ... */
    std::string UniqueId(const std::string& base) const;

    /* Shared re-key for rename/delete/ungroup: every settings or
       color key equal to `id` or under `id/` belongs to the
       instance subtree. */
    static bool PathBelongsTo(const std::string& key, const std::string& id);

    StudioDocument&         ws;
    std::vector<std::string> selection;
    Gesture                  gesture;
    std::string              last_error;
};

} /* namespace studio */

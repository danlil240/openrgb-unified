/*---------------------------------------------------------*\
|| EditorController.cpp                                      ||
||                                                           ||
||   SPDX-License-Identifier: GPL-2.0-or-later               ||
\*---------------------------------------------------------*/

#include "EditorController.h"

#include "../presets/DevicePreset.h"

#include <algorithm>
#include <cmath>

namespace studio
{
namespace
{

bool SameVec(const Vec3& a, const Vec3& b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

bool SameInstance(const DeviceInstance& a, const DeviceInstance& b)
{
    return a.type == b.type && a.parent == b.parent
        && SameVec(a.position, b.position)
        && SameVec(a.rotation_deg, b.rotation_deg);
}

Vec3 Add(const Vec3& a, const Vec3& b)
{
    return { a.x + b.x, a.y + b.y, a.z + b.z };
}

/* Rotation-only copy of a rigid world matrix. */
Mat4 RotationOnly(const Mat4& m)
{
    Mat4 r = m;
    r.m[12] = r.m[13] = r.m[14] = 0.0f;
    return r;
}

} /* anonymous namespace */

std::string EditorController::InstanceOf(const std::string& object_id)
{
    const size_t slash = object_id.find('/');
    return slash == std::string::npos ? object_id
                                      : object_id.substr(0, slash);
}

bool EditorController::Exists(const std::string& id) const
{
    return ws.devices.find(id) != ws.devices.end();
}

bool EditorController::IsLocked(const std::string& id) const
{
    const auto it = ws.device_settings.find(id);
    return it != ws.device_settings.end() && it->second.locked;
}

bool EditorController::IsVisible(const std::string& id) const
{
    const auto it = ws.device_settings.find(id);
    return it == ws.device_settings.end() || it->second.visible;
}

/*---------------------------------------------------------*\
|| Selection                                                ||
\*---------------------------------------------------------*/
bool EditorController::Select(const std::string& object_or_instance_id,
                              bool additive)
{
    const std::string id = InstanceOf(object_or_instance_id);
    if(!Exists(id))
    {
        if(!additive)
        {
            ClearSelection();
        }
        return false;
    }
    if(!additive)
    {
        selection = { id };
        return true;
    }
    const auto it = std::find(selection.begin(), selection.end(), id);
    if(it != selection.end())
    {
        /* Ctrl-style additive re-click toggles the instance off. */
        selection.erase(it);
        return true;
    }
    selection.push_back(id);
    return true;
}

void EditorController::SetSelection(const std::vector<std::string>& ids)
{
    selection.clear();
    for(const std::string& raw : ids)
    {
        const std::string id = InstanceOf(raw);
        if(Exists(id) && !IsSelected(id))
        {
            selection.push_back(id);
        }
    }
}

void EditorController::ClearSelection()
{
    selection.clear();
}

std::string EditorController::PrimarySelection() const
{
    return selection.empty() ? std::string() : selection.back();
}

bool EditorController::IsSelected(const std::string& id) const
{
    return std::find(selection.begin(), selection.end(), id)
        != selection.end();
}

bool EditorController::PathBelongsTo(const std::string& key,
                                     const std::string& id)
{
    return key == id
        || (key.size() > id.size()
            && key.compare(0, id.size(), id) == 0
            && key[id.size()] == '/');
}

std::string EditorController::UniqueId(const std::string& base) const
{
    if(!Exists(base))
    {
        return base;
    }
    for(int i = 2; ; i++)
    {
        const std::string cand = base + "_" + std::to_string(i);
        if(!Exists(cand))
        {
            return cand;
        }
    }
}

/*---------------------------------------------------------*\
|| Instance world matrices (devices parent chain, rigid)  ||
\*---------------------------------------------------------*/
std::map<std::string, Mat4> EditorController::InstanceWorlds() const
{
    std::map<std::string, Mat4> world;
    for(const auto& kv : ws.devices)
    {
        std::vector<std::string> chain;
        std::set<std::string>        seen;
        std::string                  cur = kv.first;
        while(!cur.empty() && world.find(cur) == world.end()
              && seen.insert(cur).second)
        {
            chain.push_back(cur);
            const auto it = ws.devices.find(cur);
            cur = (it == ws.devices.end()) ? std::string()
                                           : it->second.parent;
        }
        Mat4 pw = (world.find(cur) != world.end()) ? world[cur]
                                                  : Mat4Identity();
        for(auto it = chain.rbegin(); it != chain.rend(); ++it)
        {
            const DeviceInstance& d = ws.devices[*it];
            Transform t;
            t.position     = d.position;
            t.rotation_deg = d.rotation_deg;
            pw = Mat4Mul(pw, LocalMatrix(t));
            world[*it] = pw;
        }
    }
    return world;
}

Mat4 EditorController::ParentFrame(const std::map<std::string, Mat4>& worlds,
                                   const std::string& id) const
{
    const auto it = ws.devices.find(id);
    if(it == ws.devices.end() || it->second.parent.empty())
    {
        return Mat4Identity();
    }
    const auto pw = worlds.find(it->second.parent);
    return pw != worlds.end() ? pw->second : Mat4Identity();
}

std::set<std::string> EditorController::Movable() const
{
    std::set<std::string> sel;
    for(const std::string& id : selection)
    {
        if(Exists(id) && !IsLocked(id))
        {
            sel.insert(id);
        }
    }
    /* Drop ids a selected ancestor already carries — otherwise a
       translate would apply twice (once through the ancestor's
       frame, once through the explicit local write). */
    std::set<std::string> out = sel;
    for(const std::string& id : sel)
    {
        std::set<std::string> seen;
        std::string           p = ws.devices[id].parent;
        while(!p.empty() && seen.insert(p).second)
        {
            if(sel.count(p))
            {
                out.erase(id);
                break;
            }
            const auto it = ws.devices.find(p);
            p = (it == ws.devices.end()) ? std::string()
                                       : it->second.parent;
        }
    }
    return out;
}

/*---------------------------------------------------------*\
|| Transform gesture                                        ||
\*---------------------------------------------------------*/
bool EditorController::BeginTransform()
{
    gesture = Gesture{};
    gesture.movable = Movable();
    if(gesture.movable.empty())
    {
        return false;
    }
    const std::map<std::string, Mat4> worlds = InstanceWorlds();
    Vec3 sum {};
    for(const std::string& id : gesture.movable)
    {
        gesture.snapshot[id]      = ws.devices[id];
        gesture.world[id]         = worlds.at(id);
        gesture.parent_world[id]  = ParentFrame(worlds, id);
        sum = Add(sum, Mat4Translation(worlds.at(id)));
    }
    const float n = (float)gesture.movable.size();
    gesture.pivot  = { sum.x / n, sum.y / n, sum.z / n };
    gesture.active = true;
    return true;
}

void EditorController::PreviewTranslate(const Vec3& world_delta,
                                        EditPlane plane, bool snap)
{
    if(!gesture.active)
    {
        return;
    }
    Vec3 d = world_delta;
    switch(plane)
    {
    case EditPlane::DeskXZ:  d.y = 0.0f; break;
    case EditPlane::FrontXY: d.z = 0.0f; break;
    case EditPlane::SideYZ:  d.x = 0.0f; break;
    case EditPlane::Free:    break;
    }
    const float step = ws.meta.controls.move_snap_m;
    for(const std::string& id : gesture.movable)
    {
        const Vec3 wp  = Add(Mat4Translation(gesture.world[id]), d);
        Vec3       lp  = RigidInversePoint(gesture.parent_world[id], wp);
        if(snap)
        {
            lp = SnapTranslate(lp, step);
        }
        ws.devices[id].position = lp;
    }
}

void EditorController::PreviewRotate(const Vec3& axis, float degrees,
                                     bool snap)
{
    if(!gesture.active)
    {
        return;
    }
    const float deg = snap
        ? SnapAngle(degrees, ws.meta.controls.rotate_snap_deg)
        : degrees;
    const Quat qa = AxisAngleQuat(axis, deg);
    const Mat4 ra = QuatMatrix(qa);
    const Vec3 pv = gesture.pivot;
    for(const std::string& id : gesture.movable)
    {
        /* world pos -> around pivot -> back into the parent frame */
        const Vec3 wp  = Mat4Translation(gesture.world[id]);
        const Vec3 rel { wp.x - pv.x, wp.y - pv.y, wp.z - pv.z };
        const Vec3 rp  = RotateVec(qa, rel);
        const Vec3 nwp { pv.x + rp.x, pv.y + rp.y, pv.z + rp.z };
        ws.devices[id].position =
            RigidInversePoint(gesture.parent_world[id], nwp);

        /* world rot: qa * Rw; local rot: Rp^-1 * (qa * Rw) */
        const Mat4 rw = Mat4Mul(ra, RotationOnly(gesture.world[id]));
        const Mat4 rl = Mat4Mul(RigidInverse(gesture.parent_world[id]), rw);
        ws.devices[id].rotation_deg = EulerDegFromMat4(rl);
    }
}

std::optional<EditorEdit> EditorController::Commit()
{
    if(!gesture.active)
    {
        return std::nullopt;
    }
    EditorEdit e;
    for(const std::string& id : gesture.movable)
    {
        const DeviceInstance& before = gesture.snapshot[id];
        const DeviceInstance& after  = ws.devices[id];
        if(!SameInstance(before, after))
        {
            e.devices.before[id] = before;
            e.devices.after[id]  = after;
        }
    }
    const size_t n = e.devices.after.size();
    e.label = "transform " + std::to_string(n)
            + (n == 1 ? " instance" : " instances");
    gesture = Gesture{};
    if(e.Empty())
    {
        return std::nullopt;
    }
    return e;
}

void EditorController::Cancel()
{
    if(!gesture.active)
    {
        return;
    }
    for(const std::string& id : gesture.movable)
    {
        ws.devices[id] = gesture.snapshot[id];
    }
    gesture = Gesture{};
}

/*---------------------------------------------------------*\
|| Numeric edits                                            ||
\*---------------------------------------------------------*/
std::optional<EditorEdit>
EditorController::SetPosition(const std::string& id, const Vec3& pos)
{
    if(gesture.active || !Exists(id) || IsLocked(id))
    {
        return std::nullopt;
    }
    if(SameVec(ws.devices[id].position, pos))
    {
        return std::nullopt;
    }
    EditorEdit e;
    e.label = "set position " + id;
    SnapshotKey(e.devices, ws.devices, id);
    ws.devices[id].position = pos;
    CaptureKey(e.devices, ws.devices, id);
    return e;
}

std::optional<EditorEdit>
EditorController::SetRotation(const std::string& id, const Vec3& rot_deg)
{
    if(gesture.active || !Exists(id) || IsLocked(id))
    {
        return std::nullopt;
    }
    if(SameVec(ws.devices[id].rotation_deg, rot_deg))
    {
        return std::nullopt;
    }
    EditorEdit e;
    e.label = "set rotation " + id;
    SnapshotKey(e.devices, ws.devices, id);
    ws.devices[id].rotation_deg = rot_deg;
    CaptureKey(e.devices, ws.devices, id);
    return e;
}

std::optional<EditorEdit>
EditorController::SetPositionWorld(const std::string& id, const Vec3& pos)
{
    if(gesture.active || !Exists(id) || IsLocked(id))
    {
        return std::nullopt;
    }
    const std::map<std::string, Mat4> worlds = InstanceWorlds();
    const Vec3 lp = RigidInversePoint(ParentFrame(worlds, id), pos);
    return SetPosition(id, lp);
}

/*---------------------------------------------------------*\
|| Rename — re-keys every section that references the     ||
|| instance subtree.                                       ||
\*---------------------------------------------------------*/
std::optional<EditorEdit>
EditorController::Rename(const std::string& id, const std::string& new_id)
{
    if(gesture.active || !Exists(id) || new_id == id
       || !IsPresetId(new_id) || Exists(new_id))
    {
        return std::nullopt;
    }
    EditorEdit e;
    e.label = "rename " + id + " -> " + new_id;

    SnapshotKey(e.devices, ws.devices, id);
    const DeviceInstance inst = ws.devices[id];
    ws.devices.erase(id);
    ws.devices[new_id] = inst;
    CaptureKey(e.devices, ws.devices, new_id);

    /* children keep the instance as their parent frame */
    for(auto& kv : ws.devices)
    {
        if(kv.second.parent == id)
        {
            SnapshotKey(e.devices, ws.devices, kv.first);
            kv.second.parent = new_id;
            CaptureKey(e.devices, ws.devices, kv.first);
        }
    }

    /* settings keys under the instance subtree */
    std::vector<std::string> skeys;
    for(const auto& kv : ws.device_settings)
    {
        if(PathBelongsTo(kv.first, id))
        {
            skeys.push_back(kv.first);
        }
    }
    for(const std::string& k : skeys)
    {
        SnapshotKey(e.settings, ws.device_settings, k);
        const DeviceSettings v = ws.device_settings[k];
        ws.device_settings.erase(k);
        ws.device_settings[new_id + k.substr(id.size())] = v;
        CaptureKey(e.settings, ws.device_settings,
                   new_id + k.substr(id.size()));
    }

    /* mirror_of references pointing into the subtree */
    for(auto& kv : ws.device_settings)
    {
        if(!kv.second.mirror_of.empty()
           && PathBelongsTo(kv.second.mirror_of, id))
        {
            SnapshotKey(e.settings, ws.device_settings, kv.first);
            kv.second.mirror_of =
                new_id + kv.second.mirror_of.substr(id.size());
            CaptureKey(e.settings, ws.device_settings, kv.first);
        }
    }

    /* paint keyed by resolved instance/entity path */
    std::vector<std::string> ckeys;
    for(const auto& kv : ws.object_colors)
    {
        if(PathBelongsTo(kv.first, id))
        {
            ckeys.push_back(kv.first);
        }
    }
    for(const std::string& k : ckeys)
    {
        SnapshotKey(e.object_colors, ws.object_colors, k);
        const SceneColor v = ws.object_colors[k];
        ws.object_colors.erase(k);
        ws.object_colors[new_id + k.substr(id.size())] = v;
        CaptureKey(e.object_colors, ws.object_colors,
                   new_id + k.substr(id.size()));
    }
    ckeys.clear();
    for(const auto& kv : ws.emitter_colors)
    {
        if(PathBelongsTo(kv.first, id))
        {
            ckeys.push_back(kv.first);
        }
    }
    for(const std::string& k : ckeys)
    {
        SnapshotKey(e.emitter_colors, ws.emitter_colors, k);
        const auto v = ws.emitter_colors[k];
        ws.emitter_colors.erase(k);
        ws.emitter_colors[new_id + k.substr(id.size())] = v;
        CaptureKey(e.emitter_colors, ws.emitter_colors,
                   new_id + k.substr(id.size()));
    }

    for(std::string& s : selection)
    {
        if(s == id)
        {
            s = new_id;
        }
    }
    return e;
}

/*---------------------------------------------------------*\
|| Visibility / lock                                        ||
\*---------------------------------------------------------*/
std::optional<EditorEdit>
EditorController::SetVisible(const std::string& id, bool on)
{
    if(gesture.active || !Exists(id))
    {
        return std::nullopt;
    }
    const auto it = ws.device_settings.find(id);
    const bool cur = (it == ws.device_settings.end()) || it->second.visible;
    if(cur == on)
    {
        return std::nullopt;
    }
    EditorEdit e;
    e.label = on ? "show " + id : "hide " + id;
    SnapshotKey(e.settings, ws.device_settings, id);
    ws.device_settings[id].visible = on;
    CaptureKey(e.settings, ws.device_settings, id);
    return e;
}

std::optional<EditorEdit>
EditorController::SetLocked(const std::string& id, bool on)
{
    if(gesture.active || !Exists(id))
    {
        return std::nullopt;
    }
    const auto it = ws.device_settings.find(id);
    const bool cur = (it != ws.device_settings.end()) && it->second.locked;
    if(cur == on)
    {
        return std::nullopt;
    }
    EditorEdit e;
    e.label = on ? "lock " + id : "unlock " + id;
    SnapshotKey(e.settings, ws.device_settings, id);
    ws.device_settings[id].locked = on;
    CaptureKey(e.settings, ws.device_settings, id);
    return e;
}

/*---------------------------------------------------------*\
|| Delete — instance plus descendants, dangling mirror    ||
|| references cleared. Everything needed for revert is    ||
|| stored in the record.                                  ||
\*---------------------------------------------------------*/
std::optional<EditorEdit>
EditorController::Delete(const std::vector<std::string>& ids)
{
    if(gesture.active)
    {
        return std::nullopt;
    }
    std::set<std::string> kill;
    for(const std::string& id : ids)
    {
        const std::string iid = InstanceOf(id);
        if(Exists(iid) && !IsLocked(iid))
        {
            kill.insert(iid);
        }
    }
    if(kill.empty())
    {
        return std::nullopt;
    }
    /* Cascade: descendants ride along so no dangling parent is
       left behind. */
    bool grew = true;
    while(grew)
    {
        grew = false;
        for(const auto& kv : ws.devices)
        {
            if(!kv.second.parent.empty()
               && kill.count(kv.second.parent)
               && !kill.count(kv.first))
            {
                kill.insert(kv.first);
                grew = true;
            }
        }
    }

    EditorEdit e;
    e.label = "delete " + std::to_string(kill.size())
            + (kill.size() == 1 ? " instance" : " instances");

    for(const std::string& id : kill)
    {
        SnapshotKey(e.devices, ws.devices, id);
        ws.devices.erase(id);
    }
    for(const std::string& id : kill)
    {
        selection.erase(std::remove(selection.begin(), selection.end(), id),
                        selection.end());
    }

    /* settings + color entries under each removed subtree */
    std::vector<std::string> skeys;
    for(const auto& kv : ws.device_settings)
    {
        for(const std::string& id : kill)
        {
            if(PathBelongsTo(kv.first, id))
            {
                skeys.push_back(kv.first);
                break;
            }
        }
    }
    for(const std::string& k : skeys)
    {
        SnapshotKey(e.settings, ws.device_settings, k);
        ws.device_settings.erase(k);
    }

    /* mirror_of targets that no longer exist would fail
       resolution — clear them, recorded for revert. */
    for(auto& kv : ws.device_settings)
    {
        if(kv.second.mirror_of.empty())
        {
            continue;
        }
        for(const std::string& id : kill)
        {
            if(PathBelongsTo(kv.second.mirror_of, id))
            {
                SnapshotKey(e.settings, ws.device_settings, kv.first);
                kv.second.mirror_of.clear();
                CaptureKey(e.settings, ws.device_settings, kv.first);
                break;
            }
        }
    }

    std::vector<std::string> ckeys;
    for(const auto& kv : ws.object_colors)
    {
        for(const std::string& id : kill)
        {
            if(PathBelongsTo(kv.first, id))
            {
                ckeys.push_back(kv.first);
                break;
            }
        }
    }
    for(const std::string& k : ckeys)
    {
        SnapshotKey(e.object_colors, ws.object_colors, k);
        ws.object_colors.erase(k);
    }
    ckeys.clear();
    for(const auto& kv : ws.emitter_colors)
    {
        for(const std::string& id : kill)
        {
            if(PathBelongsTo(kv.first, id))
            {
                ckeys.push_back(kv.first);
                break;
            }
        }
    }
    for(const std::string& k : ckeys)
    {
        SnapshotKey(e.emitter_colors, ws.emitter_colors, k);
        ws.emitter_colors.erase(k);
    }
    return e;
}

std::optional<EditorEdit> EditorController::DeleteSelected()
{
    return Delete(selection);
}

/*---------------------------------------------------------*\
|| Group — new "group" instance at the shared pivot;      ||
|| children keep world placement.                         ||
\*---------------------------------------------------------*/
std::optional<EditorEdit>
EditorController::Group(const std::string& base_id)
{
    if(gesture.active)
    {
        return std::nullopt;
    }
    const std::set<std::string> mov = Movable();
    if(mov.empty())
    {
        return std::nullopt;
    }
    const std::map<std::string, Mat4> worlds = InstanceWorlds();
    Vec3 sum {};
    for(const std::string& id : mov)
    {
        sum = Add(sum, Mat4Translation(worlds.at(id)));
    }
    const float n = (float)mov.size();
    const Vec3 pivot { sum.x / n, sum.y / n, sum.z / n };
    const std::string gid = UniqueId(base_id);

    EditorEdit e;
    e.label = "group " + std::to_string(mov.size())
            + (mov.size() == 1 ? " instance" : " instances");

    /* The group keeps identity rotation — its local frame is the
       world frame translated to the pivot, so a child's new local
       transform is simply its world transform minus the pivot. */
    DeviceInstance g;
    g.type     = "group";
    g.position = pivot;
    SnapshotKey(e.devices, ws.devices, gid);
    ws.devices[gid] = g;
    CaptureKey(e.devices, ws.devices, gid);

    for(const std::string& id : mov)
    {
        SnapshotKey(e.devices, ws.devices, id);
        DeviceInstance& d   = ws.devices[id];
        const Mat4      w   = worlds.at(id);
        const Vec3      wp  = Mat4Translation(w);
        d.parent            = gid;
        d.position          = { wp.x - pivot.x, wp.y - pivot.y,
                                wp.z - pivot.z };
        d.rotation_deg      = EulerDegFromMat4(RotationOnly(w));
        CaptureKey(e.devices, ws.devices, id);
    }

    selection = { gid };
    return e;
}

/*---------------------------------------------------------*\
|| Ungroup — dissolve selected type-"group" instances,    ||
|| children reparented to the group's parent with world   ||
|| placement preserved.                                    ||
\*---------------------------------------------------------*/
std::optional<EditorEdit> EditorController::Ungroup()
{
    if(gesture.active)
    {
        return std::nullopt;
    }
    std::vector<std::string> targets;
    for(const std::string& id : selection)
    {
        const auto it = ws.devices.find(id);
        if(it != ws.devices.end() && it->second.type == "group"
           && !IsLocked(id))
        {
            targets.push_back(id);
        }
    }
    if(targets.empty())
    {
        return std::nullopt;
    }
    const std::map<std::string, Mat4> worlds = InstanceWorlds();

    EditorEdit e;
    e.label = "ungroup";

    for(const std::string& gid : targets)
    {
        const std::string new_parent = ws.devices[gid].parent;
        Mat4              npf        = Mat4Identity();
        if(!new_parent.empty())
        {
            const auto pw = worlds.find(new_parent);
            if(pw != worlds.end())
            {
                npf = pw->second;
            }
        }

        std::vector<std::string> children;
        for(const auto& kv : ws.devices)
        {
            if(kv.second.parent == gid)
            {
                children.push_back(kv.first);
            }
        }
        for(const std::string& c : children)
        {
            SnapshotKey(e.devices, ws.devices, c);
            DeviceInstance& d = ws.devices[c];
            const Mat4      cw = worlds.at(c);
            d.parent     = new_parent;
            d.position   = RigidInversePoint(npf, Mat4Translation(cw));
            const Mat4 rl = Mat4Mul(RigidInverse(npf), RotationOnly(cw));
            d.rotation_deg = EulerDegFromMat4(rl);
            CaptureKey(e.devices, ws.devices, c);
        }

        SnapshotKey(e.devices, ws.devices, gid);
        ws.devices.erase(gid);

        std::vector<std::string> skeys;
        for(const auto& kv : ws.device_settings)
        {
            if(PathBelongsTo(kv.first, gid))
            {
                skeys.push_back(kv.first);
            }
        }
        for(const std::string& k : skeys)
        {
            SnapshotKey(e.settings, ws.device_settings, k);
            ws.device_settings.erase(k);
        }
        for(auto& kv : ws.device_settings)
        {
            if(!kv.second.mirror_of.empty()
               && PathBelongsTo(kv.second.mirror_of, gid))
            {
                SnapshotKey(e.settings, ws.device_settings, kv.first);
                kv.second.mirror_of.clear();
                CaptureKey(e.settings, ws.device_settings, kv.first);
            }
        }

        std::vector<std::string> ckeys;
        for(const auto& kv : ws.object_colors)
        {
            if(PathBelongsTo(kv.first, gid))
            {
                ckeys.push_back(kv.first);
            }
        }
        for(const std::string& k : ckeys)
        {
            SnapshotKey(e.object_colors, ws.object_colors, k);
            ws.object_colors.erase(k);
        }
        ckeys.clear();
        for(const auto& kv : ws.emitter_colors)
        {
            if(PathBelongsTo(kv.first, gid))
            {
                ckeys.push_back(kv.first);
            }
        }
        for(const std::string& k : ckeys)
        {
            SnapshotKey(e.emitter_colors, ws.emitter_colors, k);
            ws.emitter_colors.erase(k);
        }

        selection.erase(
            std::remove(selection.begin(), selection.end(), gid),
            selection.end());
    }
    return e;
}

/*---------------------------------------------------------*\
|| DuplicateMirrored — Linked visual copy, never a        ||
|| second output writer.                                   ||
\*---------------------------------------------------------*/
std::optional<EditorEdit> EditorController::DuplicateMirrored()
{
    if(gesture.active)
    {
        return std::nullopt;
    }
    std::vector<std::string> sources;
    for(const std::string& id : selection)
    {
        if(Exists(id))
        {
            sources.push_back(id);
        }
    }
    if(sources.empty())
    {
        return std::nullopt;
    }

    EditorEdit e;
    e.label = "mirror " + std::to_string(sources.size())
            + (sources.size() == 1 ? " instance" : " instances");
    std::vector<std::string> created;
    for(const std::string& src : sources)
    {
        const DeviceInstance& s = ws.devices[src];

        /* Output ownership: mirror the source's effective owner
           (mirroring a mirror would form a chain the resolver
           rejects). zones stays empty — no second writer. The new
           id derives from that owner so repeated duplicates read
           fan0_mirror, fan0_mirror_2, ... */
        DeviceSettings ns;
        const auto sit = ws.device_settings.find(src);
        if(sit != ws.device_settings.end())
        {
            ns.visible   = sit->second.visible;
            ns.mirror_of = sit->second.mirror_of.empty()
                        ? src : sit->second.mirror_of;
        }
        else
        {
            ns.mirror_of = src;
        }
        const std::string nid =
            UniqueId(InstanceOf(ns.mirror_of) + "_mirror");

        DeviceInstance n = s;
        n.position.x += 0.03f;      /* offset so the copy is visible */
        SnapshotKey(e.devices, ws.devices, nid);
        ws.devices[nid] = n;
        CaptureKey(e.devices, ws.devices, nid);
        SnapshotKey(e.settings, ws.device_settings, nid);
        ws.device_settings[nid] = ns;
        CaptureKey(e.settings, ws.device_settings, nid);

        created.push_back(nid);
    }
    selection = created;
    return e;
}

/*---------------------------------------------------------*\
|| Snapping                                                 ||
\*---------------------------------------------------------*/
Vec3 EditorController::SnapTranslate(const Vec3& v, float step_m)
{
    if(step_m <= 0.0f)
    {
        return v;
    }
    return {
        std::round(v.x / step_m) * step_m,
        std::round(v.y / step_m) * step_m,
        std::round(v.z / step_m) * step_m,
    };
}

float EditorController::SnapAngle(float deg, float step_deg)
{
    if(step_deg <= 0.0f)
    {
        return deg;
    }
    return std::round(deg / step_deg) * step_deg;
}

} /* namespace studio */

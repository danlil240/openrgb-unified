/*---------------------------------------------------------*\
|| ControllerAdapter.h                                       |
||                                                           |
||   Bridges the scene document to live OpenRGB             |
||   controllers. Snapshots controllers, resolves bindings, |
||   and pushes only mapped addresses to zones whose        |
||   active mode supports per-LED color. Never resizes      |
||   zones. Writes are serialized by the caller's mutex.    |
||                                                           |
||   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include "../scene/BindingResolver.h"
#include "../scene/SceneTypes.h"

class OpenRGBPluginAPIInterface;
class RGBControllerInterface;

namespace studio
{

class ControllerAdapter
{
public:
    explicit ControllerAdapter(OpenRGBPluginAPIInterface* api);

    /* Re-snapshot controllers and re-resolve all bindings. Call on
       startup and whenever the device list may have changed. */
    void Refresh(const SceneDocument& doc);

    /* Resolved state for a binding id; nullptr if unknown id. */
    const ResolvedBinding* Resolution(const std::string& binding_id) const;

    /* Live controller for a resolved binding, or nullptr. */
    RGBControllerInterface* ControllerFor(const std::string& binding_id) const;
    int                     ZoneIndexFor(const std::string& binding_id) const;

    /* Zone matrix map for a resolved binding (for matrix_map layout).
       Returns false when unavailable. */
    bool ZoneMatrix(const std::string& binding_id,
                    unsigned int& rows, unsigned int& cols,
                    std::vector<unsigned int>& map) const;

    /* Write every mapped emitter of `object` (and objects sharing its
       resolved zone) to hardware. Returns a human-readable status;
       empty string = success. Refuses unverified objects and zones
       without per-LED color. */
    std::string PushObject(const SceneDocument& doc, const std::string& object_id);

    /* Write all resolved zones touched by the document. */
    std::string PushAll(const SceneDocument& doc);

    const std::vector<ControllerSnapshot>& Snapshot() const { return snapshot; }

private:
    /* Build the color buffer for one resolved zone: every verified
       Device object bound to it contributes its emitters. */
    std::string PushZone(const SceneDocument& doc, const std::string& binding_id);

    OpenRGBPluginAPIInterface*          api;
    std::vector<ControllerSnapshot>     snapshot;
    std::vector<RGBControllerInterface*> live;
    std::vector<ResolvedBinding>        resolved;
    std::map<std::string, int>          binding_slot;   /* id -> index */
};

} /* namespace studio */

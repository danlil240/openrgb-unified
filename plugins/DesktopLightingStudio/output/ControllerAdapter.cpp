/*---------------------------------------------------------*\
|| ControllerAdapter.cpp                                     |
||                                                           |
||   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "ControllerAdapter.h"

#include "OpenRGBPluginInterface.h"

namespace studio
{

ControllerAdapter::ControllerAdapter(OpenRGBPluginAPIInterface* plugin_api)
    : api(plugin_api)
{
}

void ControllerAdapter::Refresh(const SceneDocument& doc)
{
    snapshot.clear();
    live.clear();
    resolved.clear();
    binding_slot.clear();

    if(api == nullptr)
    {
        return;
    }

    live = api->GetRGBControllers();
    snapshot.reserve(live.size());

    for(RGBControllerInterface* ctrl : live)
    {
        ControllerSnapshot snap;
        snap.name        = ctrl->GetName();
        snap.vendor      = ctrl->GetVendor();
        snap.serial      = ctrl->GetSerial();
        snap.location    = ctrl->GetLocation();
        snap.device_type = ctrl->GetDeviceType();

        for(unsigned int z = 0; z < ctrl->GetZoneCount(); z++)
        {
            ZoneSnapshot zs;
            zs.name      = ctrl->GetZoneName(z);
            zs.leds_count = ctrl->GetZoneLEDsCount(z);
            zs.start_idx = ctrl->GetZoneStartIndex(z);
            zs.type      = (int)ctrl->GetZoneType(z);

            const int active = ctrl->GetZoneActiveMode(z);
            zs.has_per_led = (active >= 0)
                && (ctrl->GetZoneModeFlags(z, active) & MODE_FLAG_HAS_PER_LED_COLOR);

            snap.zones.push_back(zs);
        }
        snapshot.push_back(snap);
    }

    resolved = ResolveBindings(doc.bindings, snapshot);
    for(size_t i = 0; i < doc.bindings.size(); i++)
    {
        binding_slot[doc.bindings[i].id] = (int)i;
    }
}

const ResolvedBinding* ControllerAdapter::Resolution(const std::string& binding_id) const
{
    auto it = binding_slot.find(binding_id);
    if(it == binding_slot.end())
    {
        return nullptr;
    }
    return &resolved[it->second];
}

RGBControllerInterface* ControllerAdapter::ControllerFor(const std::string& binding_id) const
{
    const ResolvedBinding* r = Resolution(binding_id);
    if(r == nullptr || r->status != BindingStatus::Resolved)
    {
        return nullptr;
    }
    return live[r->controller_index];
}

int ControllerAdapter::ZoneIndexFor(const std::string& binding_id) const
{
    const ResolvedBinding* r = Resolution(binding_id);
    if(r == nullptr || r->status != BindingStatus::Resolved)
    {
        return -1;
    }
    /* No zone name requested -> default to zone 0 (single-zone devices). */
    if(r->zone_index < 0 && !snapshot[r->controller_index].zones.empty())
    {
        return 0;
    }
    return r->zone_index;
}

bool ControllerAdapter::ZoneMatrix(const std::string& binding_id,
                                   unsigned int& rows, unsigned int& cols,
                                   std::vector<unsigned int>& map) const
{
    RGBControllerInterface* ctrl = ControllerFor(binding_id);
    const int zi = ZoneIndexFor(binding_id);
    if(ctrl == nullptr || zi < 0)
    {
        return false;
    }
    rows = ctrl->GetZoneMatrixMapHeight(zi);
    cols = ctrl->GetZoneMatrixMapWidth(zi);
    const unsigned int* data = ctrl->GetZoneMatrixMapData(zi);
    if(rows == 0 || cols == 0 || data == nullptr)
    {
        return false;
    }
    map.assign(data, data + (size_t)rows * cols);
    return true;
}

static SceneColor ScaleColor(SceneColor c, float brightness)
{
    const unsigned int r = (unsigned int)((c & 0xFF) * brightness) & 0xFF;
    const unsigned int g = (unsigned int)(((c >> 8) & 0xFF) * brightness) & 0xFF;
    const unsigned int b = (unsigned int)(((c >> 16) & 0xFF) * brightness) & 0xFF;
    return (b << 16) | (g << 8) | r;
}

std::string ControllerAdapter::PushZone(const SceneDocument& doc,
                                        const std::string& binding_id,
                                        const FrameColors* frame)
{
    RGBControllerInterface* ctrl = ControllerFor(binding_id);
    if(ctrl == nullptr)
    {
        const ResolvedBinding* r = Resolution(binding_id);
        return (r != nullptr && r->status == BindingStatus::Ambiguous)
            ? "ambiguous: " + r->reason
            : "unresolved: " + (r ? r->reason : std::string("no binding"));
    }

    const int zi = ZoneIndexFor(binding_id);
    if(zi < 0)
    {
        return "zone not found";
    }

    ZoneSnapshot& zs = snapshot[Resolution(binding_id)->controller_index].zones[zi];

    /* Collect writes from verified Device objects BEFORE touching
       modes — a zone bound only to unverified objects must not get a
       mode switch it can't use. */
    std::vector<std::pair<unsigned int, SceneColor>> writes;
    for(const SceneObject& obj : doc.objects)
    {
        if(obj.kind != ObjectKind::Device || obj.binding != binding_id
           || !obj.verified)
        {
            continue;
        }
        for(size_t e = 0; e < obj.emitters.size(); e++)
        {
            const int addr = obj.emitters[e].address;
            if(addr < 0 || (unsigned int)addr >= zs.leds_count)
            {
                continue;
            }
            SceneColor c = EmitterColor(doc, obj.id, (int)e);
            if(frame != nullptr)
            {
                const auto fit = frame->find(obj.id);
                if(fit != frame->end() && e < fit->second.size())
                {
                    c = fit->second[e];
                }
            }
            writes.emplace_back(zs.start_idx + (unsigned int)addr,
                                ScaleColor(c, doc.brightness));
        }
    }

    if(writes.empty())
    {
        return "no mapped emitters (unverified or unbound)";
    }

    if(!EnsurePerLedMode(ctrl, zi, zs))
    {
        return "no per-LED mode available on this zone";
    }

    for(const auto& w : writes)
    {
        ctrl->SetColor(w.first, w.second);
    }
    ctrl->UpdateZoneLEDs(zi);
    return "";
}

bool ControllerAdapter::EnsurePerLedMode(RGBControllerInterface* ctrl,
                                         int zone_index, ZoneSnapshot& zs)
{
    if(zs.has_per_led)
    {
        return true;
    }

    /* Per-zone modes when the controller supports them (motherboard
       ARGB headers, multi-zone devices). */
    if(ctrl->SupportsPerZoneModes())
    {
        const unsigned int count = ctrl->GetZoneModeCount((unsigned int)zone_index);
        for(unsigned int m = 0; m < count; m++)
        {
            if(ctrl->GetZoneModeFlags((unsigned int)zone_index, m)
               & MODE_FLAG_HAS_PER_LED_COLOR)
            {
                ctrl->SetZoneActiveMode((unsigned int)zone_index, (int)m);
                ctrl->UpdateZoneMode(zone_index);
                zs.has_per_led = true;
                return true;
            }
        }
        return false;
    }

    /* Device-level modes (keyboards, mice, DIMMs). */
    const unsigned int count = ctrl->GetModeCount();
    for(unsigned int m = 0; m < count; m++)
    {
        if(ctrl->GetModeFlags(m) & MODE_FLAG_HAS_PER_LED_COLOR)
        {
            if(ctrl->GetActiveMode() != (int)m)
            {
                ctrl->SetActiveMode((int)m);
                ctrl->UpdateMode();
            }
            zs.has_per_led = true;
            return true;
        }
    }
    return false;
}

std::string ControllerAdapter::PushObject(const SceneDocument& doc,
                                          const std::string& object_id,
                                          const FrameColors* frame)
{
    const SceneObject* owner = OutputOwner(doc, object_id);
    if(owner == nullptr || owner->kind != ObjectKind::Device || owner->binding.empty())
    {
        return "not a bound device";
    }
    if(!owner->verified)
    {
        return "unverified — writes disabled";
    }
    return PushZone(doc, owner->binding, frame);
}

std::string ControllerAdapter::PushAll(const SceneDocument& doc,
                                       const FrameColors* frame)
{
    std::string problems;
    for(const DeviceBinding& b : doc.bindings)
    {
        const std::string err = PushZone(doc, b.id, frame);
        if(!err.empty() && err.find("no mapped emitters") == std::string::npos)
        {
            problems += b.id + ": " + err + "\n";
        }
    }
    return problems;
}

} /* namespace studio */

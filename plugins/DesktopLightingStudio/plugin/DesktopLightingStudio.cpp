/*---------------------------------------------------------*\
|| DesktopLightingStudio.cpp                                 |
||                                                           |
||   Desktop Lighting Studio — OpenRGB plugin (API 5)        |
||   Stage 1: desk scene + bound devices + static color      |
||                                                           |
||   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "DesktopLightingStudio.h"
#include "StudioTab.h"

OpenRGBPluginInfo DesktopLightingStudio::GetPluginInfo()
{
    OpenRGBPluginInfo info;

    info.Name           = "Desktop Lighting Studio";
    info.Description    = "3D desk scene with mapped real-world lighting";
    info.Version        = "0.2.0-stage1";
    info.Commit         = "";
    info.URL            = "https://github.com/danlil240/openrgb-unified";

    info.Location       = OPENRGB_PLUGIN_LOCATION_TOP;
    info.Label          = "Studio";
    info.TabIconString  = "";

    info.ProtocolVersion = OPENRGB_PLUGIN_API_VERSION;

    return info;
}

unsigned int DesktopLightingStudio::GetPluginAPIVersion()
{
    return OPENRGB_PLUGIN_API_VERSION;
}

void DesktopLightingStudio::Load(OpenRGBPluginAPIInterface* plugin_api_ptr)
{
    plugin_api = plugin_api_ptr;
}

QWidget* DesktopLightingStudio::GetWidget()
{
    if(tab == nullptr)
    {
        tab = new StudioTab(plugin_api);
    }
    return tab;
}

QMenu* DesktopLightingStudio::GetTrayMenu()
{
    return nullptr;
}

void DesktopLightingStudio::Unload()
{
}

void DesktopLightingStudio::OnProfileAboutToLoad()
{
}

void DesktopLightingStudio::OnProfileLoad(nlohmann::json profile_data)
{
    (void)profile_data;
}

nlohmann::json DesktopLightingStudio::OnProfileSave()
{
    return nlohmann::json();
}

unsigned char* DesktopLightingStudio::OnSDKCommand(unsigned int pkt_id, unsigned char* pkt_data, unsigned int* pkt_size)
{
    (void)pkt_id;
    (void)pkt_data;
    (void)pkt_size;
    return nullptr;
}

void DesktopLightingStudio::ProfileManagerUpdated(unsigned int update_reason)
{
    (void)update_reason;
}

void DesktopLightingStudio::ResourceManagerUpdated(unsigned int update_reason)
{
    (void)update_reason;
    if(tab != nullptr)
    {
        tab->OnDevicesChanged();
    }
}

void DesktopLightingStudio::SettingsManagerUpdated(unsigned int update_reason)
{
    (void)update_reason;
}

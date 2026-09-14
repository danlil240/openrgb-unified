/*---------------------------------------------------------*\
|| DesktopLightingStudio.h                                   |
||                                                           |
||   Desktop Lighting Studio — OpenRGB plugin (API 5)        |
||   Stage 0 probe: Studio tab + Qt Quick 3D scene           |
||                                                           |
||   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include <QObject>
#include <QWidget>

#include "OpenRGBPluginInterface.h"

class StudioTab;

class DesktopLightingStudio : public QObject, public OpenRGBPluginInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID OpenRGBPluginInterface_IID FILE "metadata.json")
    Q_INTERFACES(OpenRGBPluginInterface)

public:
    DesktopLightingStudio()  = default;
    ~DesktopLightingStudio() = default;

    /*-----------------------------------------------------*\
    | Plugin Information                                    |
    \*-----------------------------------------------------*/
    OpenRGBPluginInfo   GetPluginInfo()       override;
    unsigned int        GetPluginAPIVersion() override;

    /*-----------------------------------------------------*\
    | Plugin Functionality                                  |
    \*-----------------------------------------------------*/
    void                Load(OpenRGBPluginAPIInterface* plugin_api_ptr) override;
    QWidget*            GetWidget()           override;
    QMenu*              GetTrayMenu()         override;
    void                Unload()              override;

    void                OnProfileAboutToLoad()                            override;
    void                OnProfileLoad(nlohmann::json profile_data)        override;
    nlohmann::json      OnProfileSave()                                   override;
    unsigned char*      OnSDKCommand(unsigned int pkt_id, unsigned char* pkt_data, unsigned int* pkt_size) override;

    /*-----------------------------------------------------*\
    | Update Signals                                        |
    \*-----------------------------------------------------*/
    void                ProfileManagerUpdated(unsigned int update_reason)  override;
    void                ResourceManagerUpdated(unsigned int update_reason) override;
    void                SettingsManagerUpdated(unsigned int update_reason) override;

private:
    OpenRGBPluginAPIInterface*  plugin_api = nullptr;
    StudioTab*                  tab        = nullptr;
};

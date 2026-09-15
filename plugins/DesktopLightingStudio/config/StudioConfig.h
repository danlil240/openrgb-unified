/*---------------------------------------------------------*\
|| StudioConfig.h                                            |
||                                                           |
||   StudioDocument — the authoritative workspace doc      |
||   persisted as studio.json. Qt-free: candidate          |
||   validation and deterministic serialization run under  |
||   plain cl so tests/studio_config_test.cpp covers them. |
||                                                           |
||   Layout (schema v2):                                    |
||     schema_version, name                                 |
||     ui/camera/controls/render  — editor preferences      |
||     inputs  — reactive input source settings             |
||     output  — brightness + live_on_startup               |
||     scene   — SceneJson v2 doc (name/brightness/effect   |
||               are hoisted to the top-level sections)     |
||     effects — preset/seed/speed/intensity/playing        |
|||             + layers (reserved, retained verbatim)      |
||     definitions — device/effect preset snapshots         |
||     extensions — third-party data, retained verbatim     |
||                                                           |
||   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include "../scene/SceneTypes.h"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace studio
{

/* Workspace schema version. Older documents are migrated on load;
   newer ones are rejected without rewriting (no silent downgrade). */
constexpr int STUDIO_SCHEMA_VERSION = 2;

/* Content caps — malformed community content must not be able to
   destabilize the host. Generous vs. the current ~30-object desk. */
constexpr unsigned int STUDIO_MAX_OBJECTS   = 2048;
constexpr unsigned int STUDIO_MAX_EMITTERS  = 65536;
constexpr unsigned int STUDIO_MAX_BINDINGS  = 512;

struct UiPrefs
{
    std::string theme = "graphite";
    bool        reduced_motion = false;
};

struct CameraPrefs
{
    std::string view       = "desk";          /* desk|top|front|case|free */
    std::string projection = "orthographic";  /* orthographic|perspective */
};

struct ControlsPrefs
{
    std::string middle_drag      = "pan";     /* pan|orbit */
    float       move_snap_m      = 0.01f;
    float       rotate_snap_deg  = 15.0f;
};

struct RenderPrefs
{
    std::string quality = "balanced";         /* low|balanced|high */
    bool        bloom   = true;
};

/* Reactive input-source settings — persisted beside the scene so a
   saved workspace restores the sources it was using. */
struct InputSettings
{
    bool audio        = false;
    bool keys         = false;
    bool screen       = false;
    int  screen_index = 0;
    int  sens_pct     = 100;    /* audio sensitivity, 25..200   */
    int  decay_pct    = 100;    /* ripple decay scale, 50..300  */
};

/* Non-scene, non-input workspace state. Kept whole so preference
   sections the editor doesn't use yet still round-trip. */
struct WorkspaceMeta
{
    std::string    name;
    UiPrefs        ui;
    CameraPrefs    camera;
    ControlsPrefs  controls;
    RenderPrefs    render;
    /* output.live_on_startup — defaults false; migration and
       imported documents may never enable it. */
    bool           live_on_startup = false;
    /* Preset-library snapshots embedded in the workspace; retained
       verbatim until the preset registry owns them. */
    nlohmann::json definitions;
    /* Third-party extension data, retained verbatim. */
    nlohmann::json extensions;
    /* effects.layers — JSON effect layer definitions reserved for
       milestone 5; an array retained verbatim so hand-authored
       layers survive a save untouched. */
    nlohmann::json layers = nlohmann::json::array();
};

struct StudioDocument
{
    int            schema_version = STUDIO_SCHEMA_VERSION;
    WorkspaceMeta  meta;
    InputSettings  inputs;
    /* Scene doc owns objects/bindings/colors/effect and brightness.
       At the document level those serialize under "scene" +
       "effects"/"output" — never duplicated. */
    SceneDocument  scene;
};

/* Deterministic serialization (nlohmann orders keys; the store
   writes dump(2)). Colors serialize as "#RRGGBB". */
nlohmann::json ToJson(const StudioDocument& doc);

/* Whole-document validation into a candidate. On success `doc`
   receives the candidate and the function returns true; on failure
   `doc` is untouched and `errors` holds field-path messages.
   `warnings`, when given, collects non-fatal notes (unknown fields,
   unknown effect presets). Never throws. */
bool FromJson(const nlohmann::json& j, StudioDocument& doc,
              std::vector<std::string>* errors   = nullptr,
              std::vector<std::string>* warnings = nullptr);

} /* namespace studio */

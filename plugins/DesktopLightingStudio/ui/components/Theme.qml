/*---------------------------------------------------------*\
||| Theme.qml                                               ||
|||                                                         ||
|||   Shared workspace design tokens (spec §3): graphite   ||
|||   surfaces, one restrained accent, 8 px spacing        ||
|||   rhythm, 8–12 px corners, 120–180 ms motion. Device   ||
|||   lighting provides the color — chrome stays quiet.    ||
|||                                                         ||
|||   Plain QtObject component (matches the repo's         ||
|||   no-qmldir convention): each panel instantiates one.  ||
|||   reducedMotion reads the workspace's persisted        ||
|||   ui.reduced_motion pref through the bridge.           ||
||\*.--------------------------------------------------------*/
import QtQuick

QtObject {
    /* --- surfaces & ink --- */
    readonly property color bg:        "#101014"
    readonly property color panel:     "#18181e"
    readonly property color panelAlt:  "#1e1e26"
    readonly property color field:     "#14141a"
    readonly property color border:    "#2c2c36"
    readonly property color borderHi:  "#3a3a44"
    readonly property color text:      "#d8d8e0"
    readonly property color textDim:   "#9a9aa5"
    readonly property color textFaint: "#77777f"

    /* --- accents (restrained — hardware LEDs carry the color) --- */
    readonly property color accent:    "#6a9ad0"
    readonly property color accentBg:  "#3a5a8c"
    readonly property color warn:      "#c8a037"
    readonly property color ok:        "#58b058"
    readonly property color bad:       "#e06060"
    readonly property color live:      "#ff6060"
    readonly property color selRow:    "#26303c"

    /* --- spacing rhythm (8 px) & corners (8–12 px) --- */
    readonly property int sp:     8
    readonly property int spHalf: 4
    readonly property int sp2:    16
    readonly property int radius:    8
    readonly property int radiusSm:  6
    readonly property int radiusLg: 12

    /* --- type scale --- */
    readonly property int fontSmall: 10
    readonly property int fontBody:  12
    readonly property int fontTitle: 13

    /* --- motion: panel feedback 120–180 ms; zero under the
       workspace's reduced-motion pref (meta.ui.reduced_motion,
       read once at construction — a reload re-reads it). --- */
    readonly property bool reducedMotion:
        (typeof bridge !== "undefined") ? bridge.reducedMotion() : false
    readonly property int dur:     reducedMotion ? 0 : 150
    readonly property int durFast: reducedMotion ? 0 : 120

    /* Status-chip palette is paired with text labels — never
       color alone (spec §3). */
    function statusColor(kind, bound) {
        if (kind === "linked")        return accent
        if (bound === "ok")           return ok
        if (bound === "ambiguous")    return warn
        if (bound === "unresolved")   return bad
        if (kind === "device")        return warn   /* unmapped */
        return textFaint
    }
    function statusText(kind, bound) {
        if (kind === "linked")        return "Mirrored"
        if (bound === "ok")           return "Connected"
        if (bound === "ambiguous")    return "Ambiguous"
        if (bound === "unresolved")   return "Missing"
        if (kind === "device")        return "Unmapped"
        if (kind === "group")         return "Group"
        return ""
    }
}

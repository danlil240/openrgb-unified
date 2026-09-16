/*---------------------------------------------------------*\
||| StudioEnvironment.qml                                 ||
|||                                                       ||
|||   View3D environment + quality tiers (spec §3        ||
|||   "Rendering"). One instance per scene; `quality`    ||
|||   switches the whole post stack:                     ||
|||                                                     ||
|||     low       FXAA, no AO, no glow, no shadows       ||
|||     balanced  MSAA Medium, SSAO contact shading,     ||
|||               restrained glow                        ||
|||     high      MSAA High + TAA + specular AA, wider   ||
|||               AO, high-quality bicubic glow, key-    ||
|||               light shadows                          ||
|||                                                     ||
|||   Bloom/glow is a DISPLAY effect — it only reads the ||
|||   rendered frame, so it can never raise physical LED ||
|||   output. Emitter dots are NoLighting materials and  ||
|||   keep their authored color under every tier.        ||
|||                                                       ||
|||   Root is QtQuick3D.Helpers' ExtendedSceneEnvironment||
|||   (a QQuick3DSceneEnvironment subclass that bundles  ||
|||   the post-process effect chain) — assignable        ||
|||   straight to View3D.environment.                    ||
\*---------------------------------------------------------*/
import QtQuick
import QtQuick3D
import QtQuick3D.Helpers

ExtendedSceneEnvironment {
    id: env

    /* low | balanced | high — persisted as meta.render.quality via
       bridge.renderQuality; anything else reads as balanced. */
    property string quality: "balanced"
    /* meta.render.bloom — a document pref, not a tier constant. */
    property bool   bloomPref: true

    readonly property bool isLow:  quality === "low"
    readonly property bool isHigh: quality === "high"

    backgroundMode: SceneEnvironment.Color
    clearColor: "#101014"

    /* --- antialiasing ------------------------------------------------- */
    /* FXAA is the cheap Low-tier path (post-process, no MSAA buffers);
       Balanced/High use real MSAA, High adds temporal + specular AA. */
    fxaaEnabled: isLow
    antialiasingMode: isLow ? SceneEnvironment.NoAA
                            : SceneEnvironment.MSAA
    antialiasingQuality: isHigh ? SceneEnvironment.High
                                : SceneEnvironment.Medium
    temporalAAEnabled: isHigh
    temporalAAStrength: 0.3
    specularAAEnabled: isHigh

    /* --- soft contact shading (SSAO) ---------------------------------- */
    /* Scene units are meters here, so the occlusion radius stays in
       the centimeter range — enough for grounding contact shadow,
       not enough to smear small parts. */
    aoEnabled:    !isLow
    aoStrength:   isHigh ? 38 : 28
    aoDistance:   isHigh ? 0.08 : 0.05
    aoSoftness:   10
    aoBias:       0.01
    aoSampleRate: isHigh ? 4 : 2
    aoDither:     true

    /* --- restrained bloom --------------------------------------------- */
    /* Screen blend + a high HDR threshold: only saturated LED dots and
       emissive diffuser segments pick up a halo; chrome and bodies
       stay clean. Off at Low and when the document pref disables it. */
    glowEnabled:           bloomPref && !isLow
    glowBlendMode:         ExtendedSceneEnvironment.Screen
    glowStrength:          isHigh ? 0.55 : 0.4
    glowBloom:             0.3
    glowIntensity:         0.8
    glowHDRMinimumValue:   0.72
    glowHDRMaximumValue:   8.0
    glowHDRScale:          1.6
    glowQualityHigh:       isHigh
    glowUseBicubicUpscale: isHigh
}

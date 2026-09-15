/*---------------------------------------------------------*\
||| EffectEngine.cpp                                          |
|||                                                           |
|||   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "EffectEngine.h"

namespace studio
{

void EffectEngine::Evaluate(const SceneDocument& doc, double t,
                            FrameColors& frame, const InputState* input) const
{
    frame.clear();
    if(layers.empty())
    {
        return;
    }

    for(const SceneObject& obj : doc.objects)
    {
        /* Owners only — Linked copies share this object's group state
           and must never receive an independent evaluation (plan:
           mirrored outputs show identical colors). */
        if(obj.kind != ObjectKind::Device || obj.emitters.empty())
        {
            continue;
        }

        std::vector<SceneColor> colors(obj.emitters.size());
        bool touched = false;

        for(size_t i = 0; i < obj.emitters.size(); i++)
        {
            const Emitter& e = obj.emitters[i];

            EvalInput in;
            in.local   = e.local_pos;
            in.world   = TransformPoint(obj.transform, e.local_pos);
            in.object  = &obj;
            in.emitter = &e;
            in.t       = t;
            in.input   = input;

            ColorF acc = ToColorF(EmitterColor(doc, obj.id, (int)i));
            for(const EffectLayer& layer : layers)
            {
                if(!LayerMatches(layer, obj, e))
                {
                    continue;
                }
                touched  = true;
                ColorF r = EvalPrimitive(layer, in);
                r.a     *= layer.opacity;
                acc      = BlendOver(acc, r, layer.blend);
            }
            colors[i] = ToSceneColor(acc);
        }

        if(touched)
        {
            frame[obj.id] = std::move(colors);
        }
    }
}

} /* namespace studio */

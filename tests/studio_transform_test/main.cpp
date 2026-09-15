/*---------------------------------------------------------*\
|| studio_transform_test — transform parity fixture.       ||
||                                                          ||
||   Builds the same node tree the QML renderer builds:    ||
||   one QQuick3DNode per scene object, parented per       ||
||   parent_id, positioned/rotated via the quaternion the  ||
||   core computes from the stored XYZ degrees. Then       ||
||   compares Node.mapPositionToScene() against the core's ||
||   resolved world matrices for every emitter. Tolerance  ||
||   is 0.0001 m (design contract).                        ||
||                                                          ||
||   SPDX-License-Identifier: GPL-2.0-or-later             ||
\*---------------------------------------------------------*/

#include <QGuiApplication>
#include <QQuaternion>
#include <QVector3D>

#include <private/qquick3dnode_p.h>

#include "scene/SceneTypes.h"
#include "scene/SceneGraph.h"
#include "scene/DefaultDesk.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <string>

static int failures = 0;
static int checks   = 0;

#define CHECK(cond, name)                                              \
    do {                                                               \
        ++checks;                                                      \
        if(!(cond)) { ++failures; std::printf("FAIL: %s\n", name); }   \
    } while(0)

static float Vec3Diff(const QVector3D& a, const studio::Vec3& b)
{
    const float dx = a.x() - b.x;
    const float dy = a.y() - b.y;
    const float dz = a.z() - b.z;
    return std::sqrt(dx*dx + dy*dy + dz*dz);
}

/* Build a QQuick3DNode tree mirroring the document: the exact path
   StudioScene.qml takes — position + scale + the core quaternion
   bound to the node's `rotation` property. */
static std::map<std::string, QQuick3DNode*> BuildNodeTree(
        const studio::SceneDocument& doc, QQuick3DNode& scene_root)
{
    using namespace studio;
    std::map<std::string, QQuick3DNode*> nodes;
    for(const SceneObject& o : doc.objects)
    {
        QQuick3DNode* n = new QQuick3DNode();
        n->setPosition(QVector3D(o.transform.position.x,
                                 o.transform.position.y,
                                 o.transform.position.z));
        const Quat q = RotationQuat(o.transform.rotation_deg);
        n->setRotation(QQuaternion(q.w, q.x, q.y, q.z));
        n->setScale(QVector3D(o.transform.scale.x,
                              o.transform.scale.y,
                              o.transform.scale.z));
        nodes[o.id] = n;
    }
    for(const SceneObject& o : doc.objects)
    {
        QQuick3DObject* parent = &scene_root;
        if(!o.parent_id.empty())
        {
            auto it = nodes.find(o.parent_id);
            if(it != nodes.end())
            {
                parent = it->second;
            }
        }
        nodes[o.id]->setParentItem(parent);
    }
    return nodes;
}

static float WorstEmitterError(const studio::SceneDocument& doc,
                               const std::map<std::string, QQuick3DNode*>& nodes)
{
    using namespace studio;
    const auto world = ResolveWorldMatrices(doc);
    float worst = 0.0f;
    for(const SceneObject& o : doc.objects)
    {
        const Mat4& m = world.at(o.id);
        QQuick3DNode* node = nodes.at(o.id);
        for(const Emitter& e : o.emitters)
        {
            const Vec3 cpp = TransformPoint(m, e.local_pos);
            const QVector3D qt = node->mapPositionToScene(
                QVector3D(e.local_pos.x, e.local_pos.y, e.local_pos.z));
            const float d = Vec3Diff(qt, cpp);
            if(d > worst)
            {
                worst = d;
            }
        }
        /* object origin too — catches translation/rotation drift even
           for emitter-less objects like groups and decor */
        const Vec3 cpp = TransformPoint(m, { 0, 0, 0 });
        const QVector3D qt = node->mapPositionToScene(QVector3D(0, 0, 0));
        const float d = Vec3Diff(qt, cpp);
        if(d > worst)
        {
            worst = d;
        }
    }
    return worst;
}

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    using namespace studio;

    /*-----------------------------------------------------*\
    | Fixture: nested groups with mixed-axis rotations and  |
    | non-uniform scales — the cases that expose a wrong    |
    | Euler order or a broken parent composition.           |
    \*-----------------------------------------------------*/
    SceneDocument doc;
    {
        SceneObject grp;
        grp.id = "grp"; grp.kind = ObjectKind::Group;
        grp.transform.position    = { 0.30f, 0.10f, -0.20f };
        grp.transform.rotation_deg = { 30.0f, 45.0f, 60.0f };
        grp.transform.scale       = { 1.5f, 0.75f, 1.0f };
        doc.objects.push_back(grp);

        SceneObject sub;
        sub.id = "sub"; sub.kind = ObjectKind::Group;
        sub.parent_id = "grp";
        sub.transform.position    = { 0.05f, -0.02f, 0.03f };
        sub.transform.rotation_deg = { -20.0f, 15.0f, 35.0f };
        sub.transform.scale       = { 1.0f, 2.0f, 0.5f };
        doc.objects.push_back(sub);

        SceneObject dev;
        dev.id = "dev"; dev.kind = ObjectKind::Device;
        dev.parent_id = "sub";
        dev.transform.position    = { 0.01f, 0.02f, -0.01f };
        dev.transform.rotation_deg = { 70.0f, -10.0f, 130.0f };
        for(const Vec3 p : { Vec3{0, 0, 0}, Vec3{0.03f, -0.02f, 0.05f},
                             Vec3{-0.02f, 0.04f, 0.01f} })
        {
            Emitter e; e.local_pos = p; e.group = "dev";
            dev.emitters.push_back(e);
        }
        doc.objects.push_back(dev);

        SceneObject solo;
        solo.id = "solo"; solo.kind = ObjectKind::Device;
        solo.transform.position    = { -0.4f, 0.3f, 0.1f };
        solo.transform.rotation_deg = { -33.0f, 127.0f, -58.0f };
        solo.transform.scale       = { 0.8f, 1.2f, 1.0f };
        for(const Vec3 p : { Vec3{0.01f, 0, 0}, Vec3{0, 0.02f, -0.03f} })
        {
            Emitter e; e.local_pos = p; e.group = "solo";
            solo.emitters.push_back(e);
        }
        doc.objects.push_back(solo);

        SceneObject mirror;
        mirror.id = "mirror"; mirror.kind = ObjectKind::Linked;
        mirror.mirror_of = "dev";
        mirror.parent_id = "grp";
        mirror.transform.position    = { 0.10f, 0.05f, 0.0f };
        mirror.transform.rotation_deg = { 0, 0, 45.0f };
        Emitter e; e.local_pos = { 0.02f, 0.01f, 0.0f }; e.group = "mirror";
        mirror.emitters.push_back(e);
        doc.objects.push_back(mirror);
    }

    CHECK(ValidateSceneGraph(doc, nullptr), "fixture document validates");

    QQuick3DNode scene_root;
    const auto nodes = BuildNodeTree(doc, scene_root);
    const float err = WorstEmitterError(doc, nodes);
    std::printf("fixture worst emitter error: %.8f m\n", err);
    CHECK(err < 0.0001f, "QML node tree matches C++ world transforms");

    /*-----------------------------------------------------*\
    | The real default desk: every emitter of every object, |
    | groups included.                                      |
    \*-----------------------------------------------------*/
    SceneDocument desk = BuildDefaultDesk();
    CHECK(ValidateSceneGraph(desk, nullptr), "default desk validates");
    QQuick3DNode desk_root;
    const auto desk_nodes = BuildNodeTree(desk, desk_root);
    const float derr = WorstEmitterError(desk, desk_nodes);
    std::printf("default desk worst emitter error: %.8f m\n", derr);
    CHECK(derr < 0.0001f, "default desk emitter parity");

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}

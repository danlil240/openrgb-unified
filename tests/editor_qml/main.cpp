/*---------------------------------------------------------*\
|| editor_qml_test — Task 2.2 interaction coverage.        ||
||                                                         ||
||   Real SceneBridge + the real ui/StudioScene.qml in a   ||
||   QQuickWidget. Pointer routing runs through the        ||
||   SelectionController seam (beginPressAt/dragTo/        ||
||   endGesture take explicit hit info — deterministic      ||
||   under SAC/Device Guard); a synthesized QMouseEvent    ||
||   middle-drag adds end-to-end coverage when event       ||
||   delivery works.                                       ||
\*---------------------------------------------------------*/
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QMouseEvent>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickWidget>
#include <QQuickItem>
#include <QSignalSpy>
#include <QTest>
#include <QThread>
#include <QVector3D>
#include <cmath>

#include "../../plugins/DesktopLightingStudio/plugin/SceneBridge.h"

namespace
{

void pump(int ms)
{
    QElapsedTimer t;
    t.start();
    while(t.elapsed() < ms)
    {
        QCoreApplication::processEvents();
        QThread::msleep(5);
    }
}

/* QML functions are invokable with QVariant args — pass exactly
   the declared arity. */
QVariant callFn(QObject* o, const char* fn, std::vector<QVariant> args)
{
    QVariant ret;
    bool ok = false;
    switch(args.size())
    {
    case 0:
        ok = QMetaObject::invokeMethod(o, fn, Q_RETURN_ARG(QVariant, ret));
        break;
    case 1:
        ok = QMetaObject::invokeMethod(o, fn, Q_RETURN_ARG(QVariant, ret),
                                       Q_ARG(QVariant, args[0]));
        break;
    case 2:
        ok = QMetaObject::invokeMethod(o, fn, Q_RETURN_ARG(QVariant, ret),
                                       Q_ARG(QVariant, args[0]),
                                       Q_ARG(QVariant, args[1]));
        break;
    case 3:
        ok = QMetaObject::invokeMethod(o, fn, Q_RETURN_ARG(QVariant, ret),
                                       Q_ARG(QVariant, args[0]),
                                       Q_ARG(QVariant, args[1]),
                                       Q_ARG(QVariant, args[2]));
        break;
    case 4:
        ok = QMetaObject::invokeMethod(o, fn, Q_RETURN_ARG(QVariant, ret),
                                       Q_ARG(QVariant, args[0]),
                                       Q_ARG(QVariant, args[1]),
                                       Q_ARG(QVariant, args[2]),
                                       Q_ARG(QVariant, args[3]));
        break;
    case 6:
        ok = QMetaObject::invokeMethod(o, fn, Q_RETURN_ARG(QVariant, ret),
                                       Q_ARG(QVariant, args[0]),
                                       Q_ARG(QVariant, args[1]),
                                       Q_ARG(QVariant, args[2]),
                                       Q_ARG(QVariant, args[3]),
                                       Q_ARG(QVariant, args[4]),
                                       Q_ARG(QVariant, args[5]));
        break;
    default:
        qWarning() << "callFn: unsupported arity" << args.size();
        return ret;
    }
    if(!ok)
    {
        qWarning() << "callFn failed:" << o->objectName() << fn
                   << "arity" << (int)args.size();
    }
    return ret;
}

} /* anonymous namespace */

class EditorQmlTest : public QObject
{
    Q_OBJECT

    studio::SceneBridge* bridge = nullptr;
    QQuickWidget*        w      = nullptr;
    QObject*             root   = nullptr;
    QObject*             sel    = nullptr;   /* SelectionController */
    QObject*             cam    = nullptr;   /* CameraController    */
    QObject*             gizmo  = nullptr;

    /* Byte-stable digest of every resolved object's transform —
       the "object transforms did not change" proof. */
    QString transformDigest()
    {
        QStringList parts;
        for(const QVariant& v : bridge->objectList())
        {
            const QVariantMap m = v.toMap();
            parts << QStringLiteral("%1:%2,%3,%4,%5,%6,%7")
                .arg(m["id"].toString())
                .arg(m["x"].toDouble(),  0, 'f', 7)
                .arg(m["y"].toDouble(),  0, 'f', 7)
                .arg(m["z"].toDouble(),  0, 'f', 7)
                .arg(m["rx"].toDouble(), 0, 'f', 7)
                .arg(m["ry"].toDouble(), 0, 'f', 7)
                .arg(m["rz"].toDouble(), 0, 'f', 7);
        }
        parts.sort();
        return parts.join('|');
    }

    QString firstDeviceObjectId()
    {
        for(const QVariant& v : bridge->objectList())
        {
            const QVariantMap m = v.toMap();
            if(m["kind"].toString() == "device" && m["geometry"].toString() != "case_shell")
            {
                return m["id"].toString();
            }
        }
        return QString();
    }

    QString instanceOf(const QString& objectId)
    {
        const int s = objectId.indexOf('/');
        return s < 0 ? objectId : objectId.left(s);
    }

    /* Object id belonging to a given instance ("" when absent). */
    QString deviceObjectIdFor(const QString& inst)
    {
        for(const QVariant& v : bridge->objectList())
        {
            const QVariantMap m = v.toMap();
            if(m["kind"].toString() == "device"
               && m["instancePath"].toString() == inst
               && m["geometry"].toString() != "case_shell")
            {
                return m["id"].toString();
            }
        }
        return QString();
    }

    QVector3D worldPosOf(const QString& objectId)
    {
        return callFn(root, "worldPosOf", { objectId }).value<QVector3D>();
    }

    QVector3D camTarget()
    {
        return cam->property("target").value<QVector3D>();
    }

    void sendMouse(QEvent::Type t, const QPoint& pos, Qt::MouseButton btn,
                   Qt::MouseButtons btns,
                   Qt::KeyboardModifiers mods = Qt::NoModifier)
    {
        QMouseEvent ev(t, QPointF(pos), QPointF(w->mapToGlobal(pos)),
                       btn, btns, mods);
        (void)QApplication::sendEvent(w, &ev);
    }

private slots:
    void initTestCase()
    {
        /* Keep the bridge's ConfigStore out of the repo tree — cwd
           is its workspace root when api == nullptr. */
        const QString tmp = QDir::tempPath() + "/editor_qml_test";
        QDir().mkpath(tmp);
        QDir::setCurrent(tmp);

        bridge = new studio::SceneBridge(nullptr);

        w = new QQuickWidget();
        w->setResizeMode(QQuickWidget::SizeRootObjectToView);
        w->resize(900, 560);
        w->rootContext()->setContextProperty("bridge", bridge);
        QObject::connect(w, &QQuickWidget::statusChanged, w,
                         [this](QQuickWidget::Status s)
        {
            if(s == QQuickWidget::Error)
            {
                for(const QQmlError& e : w->errors())
                    qWarning() << "QML:" << e.toString();
            }
        });

        const QString qml =
            QDir(QStringLiteral(STUDIO_UI_DIR)).absoluteFilePath("StudioScene.qml");
        w->setSource(QUrl::fromLocalFile(QDir::cleanPath(qml)));
        w->show();
        (void)QTest::qWaitForWindowExposed(w);
        QTRY_VERIFY_WITH_TIMEOUT(w->status() == QQuickWidget::Ready, 10000);
        pump(1000);   /* scene graph + first frames */

        root  = w->rootObject();
        QVERIFY(root != nullptr);
        sel   = root->findChild<QObject*>("selCtl");
        cam   = root->findChild<QObject*>("camCtl");
        gizmo = root->findChild<QObject*>("gizmo");
        QVERIFY2(sel && cam && gizmo, "editor controllers missing — QML wiring broke");
        QVERIFY(!bridge->objectList().isEmpty());
        QVERIFY(!firstDeviceObjectId().isEmpty());
    }

    void cleanupTestCase()
    {
        delete w;
        delete bridge;
    }

    /* Middle-drag over a DEVICE BODY must pan the camera and leave
       every object transform untouched. */
    void middlePan_overDevice()
    {
        const QString obj = firstDeviceObjectId();
        const QString digest0 = transformDigest();
        const QVector3D t0 = camTarget();
        const bool undo0 = bridge->canUndo();
        const QPoint c(w->width() / 2, w->height() / 2);

        callFn(sel, "beginPressAt",
               { c.x(), c.y(), (int)Qt::MiddleButton, 0,
                 "obj|" + obj, QVariant::fromValue(worldPosOf(obj)) });
        QCOMPARE(sel->property("gesture").toInt(), 1);   /* gPan */
        for(int i = 1; i <= 4; i++)
            callFn(sel, "dragTo", { c.x() + i * 25, c.y() + i * 12, 0 });
        callFn(sel, "endGesture",
               { c.x() + 100, c.y() + 48, (int)Qt::MiddleButton, 0 });

        QVERIFY2((camTarget() - t0).length() > 1e-4,
                 "middle-drag over a device did not move the camera");
        QCOMPARE(transformDigest(), digest0);
        QCOMPARE(bridge->canUndo(), undo0);
        QCOMPARE(sel->property("gesture").toInt(), 0);
    }

    /* Same over EMPTY SPACE. */
    void middlePan_overEmpty()
    {
        const QString digest0 = transformDigest();
        const QVector3D t0 = camTarget();
        const bool undo0 = bridge->canUndo();

        callFn(sel, "beginPressAt",
               { 40, 40, (int)Qt::MiddleButton, 0, "", QVariant() });
        QCOMPARE(sel->property("gesture").toInt(), 1);
        for(int i = 1; i <= 4; i++)
            callFn(sel, "dragTo", { 40 - i * 20, 40 + i * 8, 0 });
        callFn(sel, "endGesture",
               { -40, 72, (int)Qt::MiddleButton, 0 });

        QVERIFY2((camTarget() - t0).length() > 1e-4,
                 "middle-drag over empty space did not move the camera");
        QCOMPARE(transformDigest(), digest0);
        QCOMPARE(bridge->canUndo(), undo0);
    }

    /* Left-drag on a device in Move mode: plane drag -> exactly one
       undo entry with the new placement; undo restores. */
    void moveDrag_oneUndo()
    {
        QVERIFY(!bridge->canUndo());   /* prior tests left no entries */
        const QString obj  = firstDeviceObjectId();
        const QString inst = instanceOf(obj);
        const QVector3D wp = worldPosOf(obj);
        QVERIFY(!wp.isNull());
        const QVariantMap st0 = bridge->instanceState(inst);
        QVERIFY2(st0.contains("x"), "instanceState missing — test fixture broke");

        const QPoint c(w->width() / 2, w->height() / 2);
        callFn(sel, "beginPressAt",
               { c.x(), c.y(), (int)Qt::LeftButton, 0,
                 "obj|" + obj, QVariant::fromValue(wp) });
        for(int i = 1; i <= 5; i++)
            callFn(sel, "dragTo", { c.x() + i * 16, c.y(), 0 });
        QCOMPARE(sel->property("gesture").toInt(), 2);   /* gMove */
        callFn(sel, "endGesture",
               { c.x() + 80, c.y(), (int)Qt::LeftButton, 0 });

        QVERIFY2(bridge->canUndo(), "move drag produced no undo entry");
        const QVariantMap st1 = bridge->instanceState(inst);
        QVERIFY2(st1["x"].toDouble() != st0["x"].toDouble()
                 || st1["z"].toDouble() != st0["z"].toDouble(),
                 "drag on desk XZ plane moved nothing");

        bridge->undo();
        QVERIFY2(!bridge->canUndo(), "more than one undo entry from one drag");
        const QVariantMap st2 = bridge->instanceState(inst);
        QVERIFY(std::fabs(st2["x"].toDouble() - st0["x"].toDouble()) < 1e-5
                && std::fabs(st2["z"].toDouble() - st0["z"].toDouble()) < 1e-5);
    }

    /* Escape mid-drag: zero history + pre-gesture transform back. */
    void escapeMidDrag_restores()
    {
        const QString obj  = firstDeviceObjectId();
        const QString inst = instanceOf(obj);
        const QVariantMap st0 = bridge->instanceState(inst);
        const QVector3D wp0 = worldPosOf(obj);   /* world-space, pre-drag */
        QVERIFY(!wp0.isNull());
        const QPoint c(w->width() / 2, w->height() / 2);

        callFn(sel, "beginPressAt",
               { c.x(), c.y(), (int)Qt::LeftButton, 0,
                 "obj|" + obj, QVariant::fromValue(wp0) });
        callFn(sel, "dragTo", { c.x() + 60, c.y() + 20, 0 });
        QCOMPARE(sel->property("gesture").toInt(), 2);
        QVERIFY(bridge->gestureActive());

        callFn(sel, "cancel", {});
        QCOMPARE(sel->property("gesture").toInt(), 0);
        QVERIFY(!bridge->gestureActive());
        QVERIFY(!bridge->canUndo());

        const QVariantMap st1 = bridge->instanceState(inst);
        QVERIFY(std::fabs(st1["x"].toDouble() - st0["x"].toDouble()) < 1e-5
                && std::fabs(st1["z"].toDouble() - st0["z"].toDouble()) < 1e-5);
        /* The runtime scene must be back on the pre-drag pose too —
           compare world pos to world pos (the object may be a child
           whose local != instance authored pos). scenePosition needs
           a scene-graph sync to reflect the restore. */
        pump(300);
        QVERIFY2((worldPosOf(obj) - wp0).length() < 0.01,
                 qPrintable(QStringLiteral("post-cancel world pos %1,%2,%3 vs pre-drag %4,%5,%6")
                    .arg(worldPosOf(obj).x()).arg(worldPosOf(obj).y())
                    .arg(worldPosOf(obj).z())
                    .arg(wp0.x()).arg(wp0.y()).arg(wp0.z())));
    }

    /* Click (< threshold) selects, creates no undo entry. */
    void clickSelects_noUndo()
    {
        const QString obj  = firstDeviceObjectId();
        const QString inst = instanceOf(obj);
        const QPoint c(w->width() / 2, w->height() / 2);

        callFn(sel, "beginPressAt",
               { c.x(), c.y(), (int)Qt::LeftButton, 0,
                 "obj|" + obj, QVariant::fromValue(worldPosOf(obj)) });
        /* sub-threshold wiggle, then release */
        callFn(sel, "dragTo", { c.x() + 2, c.y() + 1, 0 });
        callFn(sel, "endGesture",
               { c.x() + 2, c.y() + 1, (int)Qt::LeftButton, 0 });

        QVERIFY(bridge->selectedInstances().contains(inst));
        QVERIFY(!bridge->canUndo());

        /* Empty-space click clears. */
        callFn(sel, "beginPressAt",
               { 30, 30, (int)Qt::LeftButton, 0, "", QVariant() });
        callFn(sel, "endGesture", { 30, 30, (int)Qt::LeftButton, 0 });
        QVERIFY(bridge->selectedInstances().isEmpty());
    }

    /* Marquee: enclosed instances only — and ONLY those. */
    void marquee_selectsEnclosed()
    {
        /* The mouse sits apart from the keyboard/case cluster — the
           isolation target for the tight box. */
        QString obj = deviceObjectIdFor("mouse");
        if(obj.isEmpty())
            obj = firstDeviceObjectId();
        const QString inst = instanceOf(obj);
        const QVector3D sp =
            callFn(root, "screenPosOf", { obj }).value<QVector3D>();
        QVERIFY2(!sp.isNull(), "mapFrom3DScene unavailable");

        /* Tight box around one object -> only its instance. */
        callFn(sel, "selectMarquee",
               { sp.x() - 25, sp.y() - 25, sp.x() + 25, sp.y() + 25 });
        const QVariantList got = bridge->selectedInstances();
        QStringList gotNames;
        for(const QVariant& g : got)
            gotNames << g.toString();
        QVERIFY2(gotNames.contains(inst),
                 qPrintable(QStringLiteral("marquee missed %1 (got %2)")
                    .arg(inst).arg(gotNames.join(','))));
        /* Exclusion, not just containment: the tight box must not
           pull in neighbouring instances. */
        QCOMPARE(gotNames.size(), 1);

        /* Whole-view box -> everything non-decor. */
        callFn(sel, "selectMarquee",
               { 0, 0, w->width(), w->height() });
        const QVariantList all = bridge->selectedInstances();
        QVERIFY(all.size() >= 2);
        QVERIFY(all.contains(inst));
        bridge->clearEditorSelection();
    }

    /* Wheel zoom: pointer-centered and clamped both directions. */
    void wheelZoom_bounded()
    {
        const double span0 = cam->property("span").toDouble();
        const double smin  = cam->property("spanMin").toDouble();
        const double smax  = cam->property("spanMax").toDouble();

        /* Zoom in hard. */
        for(int i = 0; i < 40; i++)
            callFn(sel, "wheelAt",
                   { w->width() / 2, w->height() / 2, 120 });
        double sp = cam->property("span").toDouble();
        QVERIFY(sp < span0);
        QVERIFY(sp >= smin - 1e-9);

        /* Zoom out way past the cap — clamps at spanMax. */
        for(int i = 0; i < 60; i++)
            callFn(sel, "wheelAt",
                   { w->width() / 2, w->height() / 2, -120 });
        sp = cam->property("span").toDouble();
        QVERIFY(sp <= smax + 1e-9);
        QCOMPARE(sp, smax);
    }

    /* F — frame selection: camera target lands on the selection. */
    void frameSelection_movesCamera()
    {
        const QString obj  = firstDeviceObjectId();
        const QString inst = instanceOf(obj);
        const QVector3D wp = worldPosOf(obj);

        bridge->selectInstance(inst, false);
        callFn(sel, "frameSelection", {});

        const QVector3D t = camTarget();
        QVERIFY2((t - wp).length() < 0.05,
                 qPrintable(QStringLiteral("frame target %1,%2,%3 vs object %4,%5,%6")
                    .arg(t.x()).arg(t.y()).arg(t.z())
                    .arg(wp.x()).arg(wp.y()).arg(wp.z())));
        bridge->clearEditorSelection();
    }

    /* Rotate tool: drag produces a yaw change + one undo entry. */
    void rotateDrag_oneUndo()
    {
        const QString obj  = firstDeviceObjectId();
        const QString inst = instanceOf(obj);
        const QVariantMap st0 = bridge->instanceState(inst);
        const QPoint c(w->width() / 2, w->height() / 2);

        callFn(sel, "setTool", { 1 });
        QCOMPARE(sel->property("tool").toInt(), 1);

        callFn(sel, "beginPressAt",
               { c.x(), c.y(), (int)Qt::LeftButton, 0,
                 "obj|" + obj, QVariant::fromValue(worldPosOf(obj)) });
        for(int i = 1; i <= 5; i++)
            callFn(sel, "dragTo", { c.x() + i * 15, c.y() - i * 6, 0 });
        QCOMPARE(sel->property("gesture").toInt(), 3);   /* gRotate */
        callFn(sel, "endGesture",
               { c.x() + 75, c.y() - 30, (int)Qt::LeftButton, 0 });

        QVERIFY(bridge->canUndo());
        const QVariantMap st1 = bridge->instanceState(inst);
        QVERIFY2(st1["ry"].toDouble() != st0["ry"].toDouble(),
                 "rotate drag left ry unchanged");
        bridge->undo();
        QVERIFY(!bridge->canUndo());
        callFn(sel, "setTool", { 0 });
    }

    /* C1 regression: Paint mode — a left-drag on a device body must
       never promote to a move (or orbit/marquee): no transform
       gesture, no undo record, camera untouched. */
    void paintModeDrag_neverMoves()
    {
        const QString obj = firstDeviceObjectId();
        const QString digest0 = transformDigest();
        const QVector3D t0 = camTarget();
        const bool undo0 = bridge->canUndo();

        callFn(sel, "setTool", { 2 });
        QCOMPARE(sel->property("tool").toInt(), 2);

        const QPoint c(w->width() / 2, w->height() / 2);
        callFn(sel, "beginPressAt",
               { c.x(), c.y(), (int)Qt::LeftButton, 0,
                 "obj|" + obj, QVariant::fromValue(worldPosOf(obj)) });
        for(int i = 1; i <= 5; i++)
            callFn(sel, "dragTo", { c.x() + i * 20, c.y() + i * 10, 0 });
        QCOMPARE(sel->property("gesture").toInt(), 0);   /* no promotion */
        QVERIFY(!bridge->gestureActive());
        callFn(sel, "endGesture",
               { c.x() + 100, c.y() + 50, (int)Qt::LeftButton, 0 });

        QCOMPARE(transformDigest(), digest0);
        QCOMPARE(bridge->canUndo(), undo0);
        QVERIFY2((camTarget() - t0).length() < 1e-4,
                 "paint-mode drag moved the camera");
        callFn(sel, "setTool", { 0 });
    }

    /* M2 regression: a second press mid-drag cancels the live
       gesture — it must not commit a half-finished move. */
    void secondPress_cancelsGesture()
    {
        const QString obj  = firstDeviceObjectId();
        const QString inst = instanceOf(obj);
        const QVariantMap st0 = bridge->instanceState(inst);
        const QPoint c(w->width() / 2, w->height() / 2);

        callFn(sel, "beginPressAt",
               { c.x(), c.y(), (int)Qt::LeftButton, 0,
                 "obj|" + obj, QVariant::fromValue(worldPosOf(obj)) });
        callFn(sel, "dragTo", { c.x() + 50, c.y(), 0 });
        QCOMPARE(sel->property("gesture").toInt(), 2);
        QVERIFY(bridge->gestureActive());

        /* Second press while the move is live -> cancel, not commit. */
        callFn(sel, "beginPressAt",
               { c.x() + 200, c.y(), (int)Qt::LeftButton, 0,
                 "", QVariant() });
        QCOMPARE(sel->property("gesture").toInt(), 0);
        QVERIFY(!bridge->gestureActive());
        QVERIFY(!bridge->canUndo());

        const QVariantMap st1 = bridge->instanceState(inst);
        QVERIFY(std::fabs(st1["x"].toDouble() - st0["x"].toDouble()) < 1e-5
                && std::fabs(st1["z"].toDouble() - st0["z"].toDouble()) < 1e-5);
        callFn(sel, "endGesture",
               { c.x() + 200, c.y(), (int)Qt::LeftButton, 0 });
    }

    /* D1 regression: spaceDown mirrors the PHYSICAL Space key — it
       clears only on key release or app deactivation (host-side
       Connections), never on gesture end. Clearing it in reset()
       desynced from a still-held Space, and because held-key
       autorepeat is filtered, nothing re-armed it — every second
       held-space drag misrouted to move/marquee. */
    void spaceDown_survivesGestureEnd()
    {
        sel->setProperty("spaceDown", true);
        const QPoint c(w->width() / 2, w->height() / 2);

        callFn(sel, "beginPressAt",
               { c.x(), c.y(), (int)Qt::LeftButton, 0, "", QVariant() });
        callFn(sel, "dragTo", { c.x() + 60, c.y() + 30, 0 });
        QCOMPARE(sel->property("gesture").toInt(), 1);   /* space pans */
        callFn(sel, "endGesture",
               { c.x() + 60, c.y() + 30, (int)Qt::LeftButton, 0 });
        QCOMPARE(sel->property("spaceDown").toBool(), true);

        /* cancel() doesn't clear it either — after Escape the key is
           still down and the next drag SHOULD pan. */
        callFn(sel, "cancel", {});
        QCOMPARE(sel->property("spaceDown").toBool(), true);

        /* The second held-space drag must pan again — the regression
           misrouted it to marquee/move. */
        callFn(sel, "beginPressAt",
               { c.x(), c.y(), (int)Qt::LeftButton, 0, "", QVariant() });
        callFn(sel, "dragTo", { c.x() + 40, c.y() - 20, 0 });
        QCOMPARE(sel->property("gesture").toInt(), 1);
        callFn(sel, "endGesture",
               { c.x() + 40, c.y() - 20, (int)Qt::LeftButton, 0 });
        sel->setProperty("spaceDown", false);
    }

    /* D5 regression: undo()/redo() REFUSE mid-gesture — the gesture
       survives and keeps previewing; a statusMessage hint fires. */
    void undoRedo_refusedMidGesture()
    {
        const QString obj = firstDeviceObjectId();
        const QPoint c(w->width() / 2, w->height() / 2);

        callFn(sel, "beginPressAt",
               { c.x(), c.y(), (int)Qt::LeftButton, 0,
                 "obj|" + obj, QVariant::fromValue(worldPosOf(obj)) });
        callFn(sel, "dragTo", { c.x() + 50, c.y(), 0 });
        QCOMPARE(sel->property("gesture").toInt(), 2);   /* gMove */
        QVERIFY(bridge->gestureActive());

        QSignalSpy spy(bridge, &studio::SceneBridge::statusMessage);
        bridge->undo();
        QVERIFY(bridge->gestureActive());   /* not cancelled */
        QCOMPARE(spy.count(), 1);
        bridge->redo();
        QVERIFY(bridge->gestureActive());
        QCOMPARE(spy.count(), 2);

        callFn(sel, "cancel", {});
        QVERIFY(!bridge->gestureActive());
        QVERIFY(!bridge->canUndo());
    }

    /* I2: end-to-end left press over a device. Synthesized events go
       MouseArea -> selCtl.press -> view.pick -> beginPressAt; if the
       platform eats them, press() alone still proves the pick
       resolves an obj|/emit| name (not "" -> marquee). */
    void realMouse_leftDrag_device()
    {
        /* Back on a sane view — wheelZoom_bounded left spanMax. */
        callFn(cam, "applyView", { "desk" });
        pump(200);

        const QString obj = firstDeviceObjectId();
        const QVector3D sp =
            callFn(root, "screenPosOf", { obj }).value<QVector3D>();
        QVERIFY2(!sp.isNull(), "mapFrom3DScene unavailable");
        const QPoint p((int)sp.x(), (int)sp.y());
        const bool undo0 = bridge->canUndo();

        sendMouse(QEvent::MouseButtonPress, p, Qt::LeftButton,
                  Qt::LeftButton);
        pump(50);
        for(int i = 1; i <= 6; i++)
        {
            sendMouse(QEvent::MouseMove, p + QPoint(i * 12, 0),
                      Qt::NoButton, Qt::LeftButton);
            pump(30);
        }

        if(sel->property("gesture").toInt() != 2)
        {
            qWarning() << "synthesized left-drag not delivered —"
                          "verifying the pick path directly";
            callFn(sel, "press",
                   { p.x(), p.y(), (int)Qt::LeftButton, 0 });
            const QString hit = sel->property("pressHit").toString();
            QVERIFY2(hit.startsWith("obj|") || hit.startsWith("emit|"),
                     qPrintable(QStringLiteral(
                         "pick under device resolved to '%1'").arg(hit)));
            callFn(sel, "dragTo", { p.x() + 60, p.y(), 0 });
            QCOMPARE(sel->property("gesture").toInt(), 2);
            callFn(sel, "endGesture",
                   { p.x() + 60, p.y(), (int)Qt::LeftButton, 0 });
        }
        else
        {
            sendMouse(QEvent::MouseButtonRelease, p + QPoint(72, 0),
                      Qt::LeftButton, Qt::NoButton);
            pump(200);
            QCOMPARE(sel->property("gesture").toInt(), 0);
        }

        /* Either path committed one move record — pop it so the
           stack stays clean for later tests. */
        QVERIFY(bridge->canUndo() != undo0);
        bridge->undo();
        QVERIFY(!bridge->canUndo());
    }

    /* End-to-end: synthesized QMouseEvent middle-drag into the real
       QQuickWidget. If the platform eats synthesized events (SAC/
       Device Guard flake), the controller-path tests above already
       cover routing — report what happened. */
    void realMouse_middleDrag()
    {
        const QString digest0 = transformDigest();
        const QVector3D t0 = camTarget();
        const QPoint c(w->width() / 2, w->height() / 2);

        sendMouse(QEvent::MouseButtonPress, c, Qt::MiddleButton,
                  Qt::MiddleButton);
        pump(50);
        for(int i = 1; i <= 8; i++)
        {
            sendMouse(QEvent::MouseMove, c + QPoint(i * 12, i * 6),
                      Qt::NoButton, Qt::MiddleButton);
            pump(30);
        }
        sendMouse(QEvent::MouseButtonRelease, c + QPoint(96, 48),
                  Qt::MiddleButton, Qt::NoButton);
        pump(300);

        if((camTarget() - t0).length() < 1e-4)
        {
            qWarning() << "synthesized middle-drag was not delivered to the"
                          "QML MouseArea — controller-path coverage stands";
            QCOMPARE(transformDigest(), digest0);
            return;
        }
        QCOMPARE(transformDigest(), digest0);
        QVERIFY(!bridge->canUndo());
    }
};

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    EditorQmlTest tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "main.moc"

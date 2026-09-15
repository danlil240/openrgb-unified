// Loads the Studio probe QML in a QQuickWidget, renders, then saves
// grabFramebuffer() to probe.png. Used to reproduce/diagnose the
// black-View3D issue seen inside OpenRGB without a user screenshot.
#include <QApplication>
#include <QElapsedTimer>
#include <QMouseEvent>
#include <QThread>
#include <QQuickItem>
#include <QQuickWidget>
#include <QQuickWindow>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlError>
#include <QSGRendererInterface>
#include <QTimer>
#include <QImage>
#include <QVariantList>
#include <QVariantMap>
#include <cmath>
#include <cstdio>

#include "../../plugins/DesktopLightingStudio/plugin/SceneBridge.h"

/* Minimal stand-in for the plugin's SceneBridge context property so the
   probe exercises the scene delegates without OpenRGB. */
class FakeBridge : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList objectList READ objectList NOTIFY sceneChanged)
    Q_PROPERTY(QString selectedId READ selectedId NOTIFY selectionChanged)
    Q_PROPERTY(bool live READ live NOTIFY liveChanged)
    Q_PROPERTY(bool caseGhost READ caseGhost NOTIFY caseGhostChanged)
    Q_PROPERTY(int brightnessPct READ brightnessPct NOTIFY brightnessChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY undoChanged)
    Q_PROPERTY(QColor paintColor READ paintColor NOTIFY paintColorChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
public:
    QVariantList objectList() const
    {
        QVariantMap desk;   desk["id"]="desk";   desk["label"]="Desk"; desk["kind"]="decor";
        desk["geometry"]="desk"; desk["x"]=0.0; desk["y"]=-0.02; desk["z"]=0.1;
        desk["rx"]=0.0; desk["ry"]=0.0; desk["rz"]=0.0; desk["sx"]=1.4; desk["sy"]=0.04; desk["sz"]=0.75;
        desk["visible"]=true; desk["verified"]=false; desk["emitters"]=0; desk["bound"]="none";

        QVariantMap fan;    fan["id"]="case_fans"; fan["label"]="Case fans"; fan["kind"]="device";
        fan["geometry"]="fan_body"; fan["x"]=0.2; fan["y"]=0.1; fan["z"]=0.05;
        fan["rx"]=90.0; fan["ry"]=0.0; fan["rz"]=0.0; fan["sx"]=1.0; fan["sy"]=1.0; fan["sz"]=1.0;
        fan["visible"]=true; fan["verified"]=true; fan["emitters"]=8; fan["bound"]="ok";

        QVariantMap kbd;    kbd["id"]="keyboard"; kbd["label"]="G512"; kbd["kind"]="device";
        kbd["geometry"]="keyboard_body"; kbd["x"]=-0.1; kbd["y"]=0.02; kbd["z"]=0.25;
        kbd["rx"]=0.0; kbd["ry"]=0.0; kbd["rz"]=0.0; kbd["sx"]=1.0; kbd["sy"]=1.0; kbd["sz"]=1.0;
        kbd["visible"]=true; kbd["verified"]=true; kbd["emitters"]=21; kbd["bound"]="ok";

        return { desk, fan, kbd };
    }
    QString selectedId() const { return m_sel; }
    bool live() const { return false; }
    bool caseGhost() const { return false; }
    int brightnessPct() const { return 100; }
    bool canUndo() const { return false; }
    bool canRedo() const { return false; }
    QColor paintColor() const { return Qt::white; }
    QString statusText() const { return "probe"; }

    Q_INVOKABLE QVariantList emittersOf(const QString& objectId) const
    {
        QVariantList out;
        const int n = (objectId == "case_fans") ? 8 : 21;
        for(int i = 0; i < n; i++)
        {
            QVariantMap m;
            if(objectId == "case_fans")
            {
                const float a = (float)i * 6.2831853f / 8.0f;
                m["x"] = 0.052f * std::cos(a); m["y"] = 0.0; m["z"] = -0.052f * std::sin(a);
            }
            else
            {
                m["x"] = -0.19f + (float)i * 0.019f; m["y"] = 0.016; m["z"] = 0.0;
            }
            m["c"] = "#00c8ff"; m["i"] = i;
            out.push_back(m);
        }
        return out;
    }
    Q_INVOKABLE QVariantMap objectInfo(const QString&) const { return {}; }
    Q_INVOKABLE QString bindingReport() const { return "fake"; }
public slots:
    void select(const QString& id) { m_sel = id; emit selectionChanged(); }
    void paintEmitter(const QString&, int, const QColor&) {}
    void setBrightnessPct(int) {}
    void setLive(bool) {}
    void setCaseGhost(bool) {}
    void setPaintColor(const QColor&) {}
    void undo() {}
    void redo() {}
    void refreshDevices() {}
    bool saveScene() { return true; }
    bool loadScene() { return true; }
    void resetScene() {}
signals:
    void sceneChanged();
    void emittersChanged(const QString& objectId);
    void selectionChanged();
    void liveChanged();
    void caseGhostChanged();
    void brightnessChanged();
    void undoChanged();
    void paintColorChanged();
    void statusChanged();
    void statusMessage(const QString& text);
private:
    QString m_sel;
};

int main(int argc, char** argv)
{
    // Optional: force a backend via arg1 = "d3d11"|"opengl"|"vulkan"|"software"
    if(argc > 1)
    {
        const QString api = QString::fromLocal8Bit(argv[1]);
        if(api == "d3d11")   QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);
        if(api == "opengl")  QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
        if(api == "vulkan")  QQuickWindow::setGraphicsApi(QSGRendererInterface::Vulkan);
        if(api == "d3d12")   QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D12);
        if(api == "software") QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    }

    QApplication app(argc, argv);

    const QString qmlDir = (argc > 2) ? QString::fromLocal8Bit(argv[2])
        : QStringLiteral("C:/Users/daniel/tools/openrgb-unified/OpenRGB/OpenRGB Windows 64-bit/plugins/DesktopLightingStudio/qml");
    const QString qmlFile = (argc > 3) ? QString::fromLocal8Bit(argv[3])
        : QStringLiteral("C:/Users/daniel/tools/openrgb-unified/plugins/DesktopLightingStudio/ui/StudioScene.qml");
    const QString outPng = (argc > 4) ? QString::fromLocal8Bit(argv[4])
        : QStringLiteral("C:/Users/daniel/tools/openrgb-unified/tests/quickwidget_probe/probe.png");

    QQuickWidget w;
    w.engine()->addImportPath(qmlDir);
    w.setResizeMode(QQuickWidget::SizeRootObjectToView);
    w.resize(900, 500);

    studio::SceneBridge bridge(nullptr);
    w.rootContext()->setContextProperty("bridge", &bridge);

    QObject::connect(&w, &QQuickWidget::statusChanged, &w, [&w](QQuickWidget::Status s)
    {
        fprintf(stderr, "status=%d\n", (int)s);
        if(s == QQuickWidget::Error)
            for(const QQmlError& e : w.errors())
                fprintf(stderr, "  err: %s\n", qPrintable(e.toString()));
    });
    QObject::connect(&w, &QQuickWidget::sceneGraphError, &w, [](QQuickWindow::SceneGraphError e, const QString& msg)
    {
        fprintf(stderr, "sceneGraphError %d: %s\n", (int)e, qPrintable(msg));
    });

    w.setSource(QUrl::fromLocalFile(qmlFile));
    w.show();

    // Send a mouse event with an explicit `buttons` state (needed to
    // simulate drags; QTest::mouseMove sends buttons=0).
    auto sendMouse = [&w](QEvent::Type t, const QPoint& pos,
                          Qt::MouseButton btn, Qt::MouseButtons btns,
                          Qt::KeyboardModifiers mods = Qt::NoModifier)
    {
        QMouseEvent ev(t, QPointF(pos), QPointF(w.mapToGlobal(pos)),
                       btn, btns, mods);
        QApplication::sendEvent(&w, &ev);
    };
    auto pump = [&app](int ms)
    {
        QElapsedTimer t; t.start();
        while(t.elapsed() < ms) { QCoreApplication::processEvents(); QThread::msleep(5); }
    };

    QTimer::singleShot(2500, &app, [&]()
    {
        QSGRendererInterface* rhi = w.quickWindow()->rendererInterface();
        fprintf(stderr, "graphicsApi=%d (1=SW 3=GL 4=D3D11 5=VK 8=D3D12)\n",
                rhi ? (int)rhi->graphicsApi() : -1);

        QObject* root = w.rootObject();
        fprintf(stderr, "rootObject=%p objects=%lld\n", root,
                root ? (long long)bridge.objectList().size() : -1);
        if(root)
            fprintf(stderr, "camYaw(before)=%.3f\n",
                    root->property("camYaw").toReal());

        // --- Drag test: press, move in steps, release -> orbit ---
        const QPoint c(w.width() / 2, w.height() / 2);
        sendMouse(QEvent::MouseButtonPress, c, Qt::LeftButton, Qt::LeftButton);
        pump(30);
        for(int i = 1; i <= 8; i++)
        {
            sendMouse(QEvent::MouseMove, c + QPoint(i * 10, i * 3),
                      Qt::NoButton, Qt::LeftButton);
            pump(30);
        }
        sendMouse(QEvent::MouseButtonRelease, c + QPoint(80, 24),
                  Qt::LeftButton, Qt::NoButton);
        pump(400);
        if(root)
            fprintf(stderr, "camYaw(after-drag)=%.3f\n",
                    root->property("camYaw").toReal());

        // --- Click test: plain click -> select ---
        sendMouse(QEvent::MouseButtonPress, c, Qt::LeftButton, Qt::LeftButton);
        pump(30);
        sendMouse(QEvent::MouseButtonRelease, c, Qt::LeftButton, Qt::NoButton);
        pump(200);
        fprintf(stderr, "after-click dbg=%s selectedId=%s\n",
                root ? qPrintable(root->property("dbg").toString()) : "?",
                qPrintable(bridge.selectedId()));

        // --- Shift-click test -> paint attempt ---
        sendMouse(QEvent::MouseButtonPress, c, Qt::LeftButton, Qt::LeftButton,
                  Qt::ShiftModifier);
        pump(30);
        sendMouse(QEvent::MouseButtonRelease, c, Qt::LeftButton, Qt::NoButton,
                  Qt::ShiftModifier);
        pump(200);
        fprintf(stderr, "after-shift-click dbg=%s\n",
                root ? qPrintable(root->property("dbg").toString()) : "?");

        QImage img = w.grabFramebuffer();
        // Count non-near-black pixels to distinguish "renders" from "black".
        long lit = 0;
        for(int y = 0; y < img.height(); y += 8)
            for(int x = 0; x < img.width(); x += 8)
            {
                QRgb p = img.pixel(x, y);
                if(qRed(p) > 40 || qGreen(p) > 40 || qBlue(p) > 40)
                    lit++;
            }
        fprintf(stderr, "lit-sample-pixels=%ld of %ld\n", lit,
                (long)(img.width() / 8) * (img.height() / 8));
        img.save(outPng);
        fprintf(stderr, "saved %s\n", qPrintable(outPng));
        app.quit();
    });

    return app.exec();
}

#include "main.moc"

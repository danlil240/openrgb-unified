// Loads the Studio probe QML in a QQuickWidget, renders, then saves
// grabFramebuffer() to probe.png. Used to reproduce/diagnose the
// black-View3D issue seen inside OpenRGB without a user screenshot.
#include <QApplication>
#include <QQuickWidget>
#include <QQuickWindow>
#include <QQmlEngine>
#include <QQmlError>
#include <QSGRendererInterface>
#include <QTimer>
#include <QImage>
#include <cstdio>

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

    QTimer::singleShot(2500, &app, [&]()
    {
        QSGRendererInterface* rhi = w.quickWindow()->rendererInterface();
        fprintf(stderr, "graphicsApi=%d (1=SW 3=GL 4=D3D11 5=VK 8=D3D12)\n",
                rhi ? (int)rhi->graphicsApi() : -1);

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

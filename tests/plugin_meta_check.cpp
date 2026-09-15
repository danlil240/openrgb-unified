/* One-off probe: replicate PluginManager's metadata check on a plugin DLL. */
#include <QCoreApplication>
#include <QPluginLoader>
#include <QJsonObject>
#include <QDebug>
#include <QJsonDocument>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if(argc < 2)
    {
        qWarning() << "usage: plugin_meta_check <dll>";
        return 1;
    }
    QPluginLoader loader(QString::fromLocal8Bit(argv[1]));
    QJsonObject metadata = loader.metaData();
    qDebug().noquote() << QJsonDocument(metadata).toJson(QJsonDocument::Indented);

    if(metadata.contains("MetaData"))
    {
        metadata = metadata.value("MetaData").toObject();
        qDebug() << "MetaData found, OpenRGBPluginAPIVersion present:"
                 << metadata.contains("OpenRGBPluginAPIVersion")
                 << "value:" << metadata.value("OpenRGBPluginAPIVersion").toInt();
    }
    else
    {
        qDebug() << "no MetaData field";
    }
    return 0;
}

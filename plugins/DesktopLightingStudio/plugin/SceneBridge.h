/*---------------------------------------------------------*\
|| SceneBridge.h                                             |
||                                                           |
||   QObject bridge between the scene document and the      |
||   QML desk view. Owns the SceneDocument, the controller  |
||   adapter, the undo stack, and the live-output switch.   |
||                                                           |
||   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include <QObject>
#include <QColor>
#include <QMutex>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include "../scene/SceneTypes.h"
#include "../output/ControllerAdapter.h"

class OpenRGBPluginAPIInterface;
class QUndoStack;

namespace studio
{

class SceneBridge : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList objectList READ objectList NOTIFY sceneChanged)
    Q_PROPERTY(QString selectedId READ selectedId NOTIFY selectionChanged)
    Q_PROPERTY(bool live READ live NOTIFY liveChanged)
    Q_PROPERTY(bool caseGhost READ caseGhost NOTIFY caseGhostChanged)
    Q_PROPERTY(int brightnessPct READ brightnessPct NOTIFY brightnessChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY undoChanged)
    Q_PROPERTY(QColor paintColor READ paintColor WRITE setPaintColor NOTIFY paintColorChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)

public:
    explicit SceneBridge(OpenRGBPluginAPIInterface* api, QObject* parent = nullptr);
    ~SceneBridge() override;

    QVariantList    objectList() const;
    QString         selectedId() const { return selected; }
    bool            live() const { return live_output; }
    bool            caseGhost() const { return case_ghost; }
    int             brightnessPct() const;
    bool            canUndo() const;
    bool            canRedo() const;
    QColor          paintColor() const { return paint_color; }
    QString         statusText() const { return status; }

    /* Emitter dots for one object: [{x,y,z,c}] — linked objects
       return the owner's emitter layout and colors. */
    Q_INVOKABLE QVariantList emittersOf(const QString& objectId) const;

    Q_INVOKABLE QVariantMap objectInfo(const QString& objectId) const;
    Q_INVOKABLE QString     bindingReport() const;

public slots:
    void select(const QString& objectId);
    void setSelectedColor(const QColor& color);
    void paintEmitter(const QString& objectId, int index, const QColor& color);
    void setBrightnessPct(int pct);
    void setLive(bool on);
    void setCaseGhost(bool on);
    void setPaintColor(const QColor& color);
    void undo();
    void redo();
    void refreshDevices();
    bool saveScene();
    bool loadScene();
    void resetScene();

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
    void statusMessage(const QString& text);      /* -> results box        */

private:
    friend class SceneColorCommand;
    friend class SceneEmitterCommand;
    friend class SceneBrightnessCommand;

    /* Non-undoable core ops used by undo commands and public slots. */
    void applyObjectColor(const std::string& owner_id, SceneColor color);
    void applyEmitterColor(const std::string& owner_id, int index, SceneColor color);
    void applyBrightness(float brightness);

    /* Push a zone to hardware on a serialized worker when live. */
    void pushLive(const std::string& object_id);
    void pushLiveAll();

    void rebuildMatrixLayouts();
    void setStatus(const QString& text);

    OpenRGBPluginAPIInterface*  api;
    ControllerAdapter           adapter;
    SceneDocument               doc;
    QUndoStack*                 undo_stack;

    QString                     selected;
    bool                        live_output = false;
    bool                        case_ghost  = false;
    QColor                      paint_color = Qt::white;
    QString                     status;
    QMutex                      io_mutex;
};

} /* namespace studio */

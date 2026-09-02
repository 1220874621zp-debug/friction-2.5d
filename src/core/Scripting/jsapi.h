/*
#
# Friction - https://friction.graphics
#
# Copyright (c) Ole-André Rodlie and contributors
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
# See 'README.md' for more information.
#
*/

#ifndef JSAPI_H
#define JSAPI_H

#include "core_global.h"

#include <QObject>
#include <QPointer>
#include <QJSValue>
#include <QJSEngine>
#include <QVector>
#include <memory>

class Canvas;
class BoundingBox;
class Property;
class QPointFAnimator;
class QrealAnimator;
class BasicTransformAnimator;
class SmartVectorPath;
class SmartPathAnimator;

namespace Friction
{
    namespace Core
    {
        // AE-flavored JS plugin API (phase 1):
        //   app.activeScene / app.project.activeItem
        //   scene.layer(i) / scene.numLayers / scene.addRect(...)
        //   layer.property("position") / layer.name / layer.visible
        //   property.setValueAtTime(seconds, value) / property.value
        //
        // Every script file runs in its own QJSEngine (isolation).
        // Proxies are QObject wrappers created per engine; layer and
        // property proxies use QPointer guards so stale references
        // from deleted objects fail safely.

        class JsHost;

        class CORE_EXPORT JsPropertyProxy : public QObject
        {
            Q_OBJECT
            Q_PROPERTY(QJSValue value READ value WRITE setValue)
            Q_PROPERTY(int numKeys READ numKeys)
        public:
            // kind of the wrapped animator
            enum class Kind { Scalar, Point };
            Q_ENUM(Kind)

            JsPropertyProxy(const QPointer<Property> &prop,
                            const Kind kind,
                            QObject * const parent);
            // out-of-line dtor: QPointer<T> members require the
            // complete type, so it must not be instantiated in
            // translation units that only see this header
            ~JsPropertyProxy();

            Q_INVOKABLE QJSValue value();
            Q_INVOKABLE void setValue(const QJSValue &v);

            // time in seconds (AE convention), converted with scene fps
            Q_INVOKABLE QJSValue valueAtTime(const qreal seconds);
            Q_INVOKABLE void setValueAtTime(const qreal seconds,
                                            const QJSValue &v);
            // native frame-based variants
            Q_INVOKABLE QJSValue valueAtFrame(const int frame);
            Q_INVOKABLE void setValueAtFrame(const int frame,
                                             const QJSValue &v);

            Q_INVOKABLE int numKeys();
            // seconds of key at 1-based index (AE convention)
            Q_INVOKABLE qreal keyTime(const int index);
            // native frame of key at 1-based index
            Q_INVOKABLE int keyFrame(const int index);
            Q_INVOKABLE void removeKeyAtFrame(const int frame);

            bool valid() const { return !mProp.isNull(); }
        private:
            qreal fps() const;
            QPointer<Property> mProp;
            Kind mKind;
        };

        class CORE_EXPORT JsLayerProxy : public QObject
        {
            Q_OBJECT
            Q_PROPERTY(QString name READ name WRITE setName)
            Q_PROPERTY(int index READ index)
            Q_PROPERTY(bool visible READ visible WRITE setVisible)
            Q_PROPERTY(bool selected READ selected WRITE setSelected)
            Q_PROPERTY(qreal opacity READ opacity WRITE setOpacity)
        public:
            JsLayerProxy(const QPointer<BoundingBox> &box,
                         QJSEngine * const engine,
                         QObject * const parent);
            ~JsLayerProxy();

            Q_INVOKABLE QJSValue property(const QString &name);
            // AE-style shorthand proxies
            Q_INVOKABLE QJSValue position();
            Q_INVOKABLE QJSValue scale();
            Q_INVOKABLE QJSValue rotation();
            // 2.5D billboard shorthand proxies
            Q_INVOKABLE QJSValue rotationX();
            Q_INVOKABLE QJSValue rotationY();
            Q_INVOKABLE QJSValue zPosition();
            Q_INVOKABLE QJSValue perspective();
            Q_INVOKABLE QJSValue opacityProp();
            // Styling & typography helpers
            Q_INVOKABLE bool setFillColor(const QString &color);
            Q_INVOKABLE bool setStrokeColor(const QString &color);
            Q_INVOKABLE bool setStrokeWidth(const qreal width);
            Q_INVOKABLE bool setFontSize(const qreal size);
            Q_INVOKABLE bool setFontFamily(const QString &family);
            Q_INVOKABLE bool setLetterSpacing(const qreal spacing);
            Q_INVOKABLE bool setLineSpacing(const qreal spacing);
            Q_INVOKABLE bool setText(const QString &text);
            Q_INVOKABLE bool setTextAlignment(const QString &align);
            Q_INVOKABLE bool setBlendMode(const QString &mode);
            Q_INVOKABLE bool setCornerRadius(const qreal radius);
            Q_INVOKABLE bool setRadius(const qreal radius);
            // Effects & Filter management
            Q_INVOKABLE bool addEffect(const QString &effectType);
            Q_INVOKABLE bool removeEffect(const int index);
            Q_INVOKABLE QJSValue effects();
            // Text animation presets (staggered per-character animations)
            Q_INVOKABLE bool applyTextPreset(const QString &presetId,
                                             const qreal startFrame = 0,
                                             const qreal durationScale = 1.0,
                                             const bool out = false);
            Q_INVOKABLE QJSValue textPresets();
            // Lock & Visibility
            Q_INVOKABLE bool isLocked() const;
            Q_INVOKABLE void setLocked(const bool locked);
            // 2.5D layer toggle (timeline cube button state)
            Q_INVOKABLE bool is3DEnabled();
            Q_INVOKABLE void set3DEnabled(const bool enabled);
            // pivot position in scene coordinates (AE layer position)
            Q_INVOKABLE QJSValue worldPosition();
            Q_INVOKABLE bool setWorldPosition(const QJSValue &v);
            // content bounds in layer coordinates {left, top, width,
            // height, right, bottom}, like AE sourceRectAtTime
            // (NOTE: not a Q_PROPERTY - Q_PROPERTY would shadow the
            // method and break bounds() calls from JS)
            Q_INVOKABLE QJSValue bounds();
            Q_INVOKABLE QJSValue worldBounds();
            // pivot ("center") in layer content coordinates; moving
            // it keeps the layer visually in place (AE anchor move
            // with position compensation)
            Q_INVOKABLE QJSValue anchorPoint();
            Q_INVOKABLE void setAnchorPoint(const QJSValue &v);
            Q_INVOKABLE void remove();
            // duplicate this layer (AE layer.duplicate()): full copy
            // (content + transform) inserted into the same parent
            // group; returns the new layer proxy, or null on failure
            Q_INVOKABLE QJSValue duplicate();
            // re-parent this layer under the given layer (group/null)
            // without changing its world position
            Q_INVOKABLE bool setParentLayer(const QJSValue &parent);

            QString name() const;
            void setName(const QString &name);
            int index() const;
            bool visible() const;
            void setVisible(const bool visible);
            bool selected() const;
            void setSelected(const bool selected);
            qreal opacity() const;
            void setOpacity(const qreal opacity);

            bool valid() const { return !mBox.isNull(); }
            BoundingBox *box() const;

            // vector paths of this layer (SmartVectorPath layers);
            // returns an array of JsPathProxy, empty for other types
            Q_INVOKABLE QJSValue paths();

            // motion-path keys of the position animator (AE spatial
            // keyframes): {keys:[{frame, point:[x,y],
            // inTan:[dx,dy], outTan:[dx,dy]}]} in canvas coordinates;
            // x animates left->right in time, tangents are offsets in
            // (time-frames, value) pairs converted to canvas space
            Q_INVOKABLE QJSValue motionPath();
            // set both spatial tangents of the position key at frame
            // (offsets in canvas units; time spread kept)
            Q_INVOKABLE bool setMotionKeyTangents(const int frame,
                                                  const QJSValue &inTan,
                                                  const QJSValue &outTan);
        private:
            QJSValue makeProperty(const QString &name);
            QPointer<BoundingBox> mBox;
            QPointer<QJSEngine> mEngine;
        };

        // AE Shape {vertices, inTangents, outTangents, closed} analog
        // for a Friction SmartPath. Node layout matches AE:
        //   point  = node vertex (p1)
        //   inTan  = c0 handle offset relative to the vertex
        //   outTan = c2 handle offset relative to the vertex
        class CORE_EXPORT JsPathProxy : public QObject
        {
            Q_OBJECT
        public:
            JsPathProxy(const QPointer<SmartPathAnimator> &anim,
                        QJSEngine * const engine,
                        QObject * const parent);
            ~JsPathProxy();

            // {closed, nodes:[{id, point:[x,y], inTan:[x,y],
            //  outTan:[x,y], selected}] } - AE-shape-like snapshot
            Q_INVOKABLE QJSValue pathInfo();

            // set both handles of a node; tangents are offsets
            // relative to the vertex (AE convention); pass null to
            // keep the current value
            Q_INVOKABLE void setNodeHandles(const int nodeId,
                                            const QJSValue &inTan,
                                            const QJSValue &outTan);
            // CtrlsMode: "corner" / "smooth" / "symmetric"
            Q_INVOKABLE void setNodeCtrlsMode(const int nodeId,
                                              const QString &mode);

            bool valid() const;
        private:
            QPointer<SmartPathAnimator> mAnim;
            QPointer<QJSEngine> mEngine;
        };

        class CORE_EXPORT JsSceneProxy : public QObject
        {
            Q_OBJECT
            Q_PROPERTY(QString name READ name WRITE setName)
            Q_PROPERTY(int width READ width WRITE setWidth)
            Q_PROPERTY(int height READ height WRITE setHeight)
            Q_PROPERTY(qreal fps READ fps WRITE setFps)
            Q_PROPERTY(qreal duration READ duration WRITE setDuration)
            Q_PROPERTY(int currentFrame READ currentFrame WRITE setCurrentFrame)
            Q_PROPERTY(qreal currentTime READ currentTime WRITE setCurrentTime)
            Q_PROPERTY(int numLayers READ numLayers)
        public:
            JsSceneProxy(const QPointer<Canvas> &scene,
                         QJSEngine * const engine,
                         QObject * const parent);
            ~JsSceneProxy();

            Q_INVOKABLE QJSValue layer(const QJSValue &indexOrName);
            Q_INVOKABLE QJSValue layers();
            Q_INVOKABLE QJSValue selectedLayers();

            Q_INVOKABLE QJSValue addRect(const QString &name,
                                         const qreal x, const qreal y,
                                         const qreal w, const qreal h);
            Q_INVOKABLE QJSValue addEllipse(const QString &name,
                                            const qreal cx, const qreal cy,
                                            const qreal radius);
            Q_INVOKABLE QJSValue addText(const QString &name,
                                         const QString &text);
            Q_INVOKABLE QJSValue addNull(const QString &name);
            Q_INVOKABLE QJSValue addGroup(const QString &name);
            // layer-type container: can hold child layers (the enve
            // equivalent of an AE null used as a parent controller;
            // NullObject itself is a leaf and cannot have children)
            Q_INVOKABLE QJSValue addLayer(const QString &name);

            QString name() const;
            void setName(const QString &name);
            int width() const;
            void setWidth(const int w);
            int height() const;
            void setHeight(const int h);
            qreal fps() const;
            void setFps(const qreal fps);
            qreal duration() const;
            void setDuration(const qreal seconds);
            int currentFrame() const;
            void setCurrentFrame(const int frame);
            qreal currentTime() const;
            void setCurrentTime(const qreal seconds);
            int numLayers() const;

            bool valid() const { return !mScene.isNull(); }
            Canvas *scene() const;
        private:
            QJSValue wrapBox(BoundingBox * const box);
            QJSValue addBox(const int boxType, const QString &name);

            QPointer<Canvas> mScene;
            QPointer<QJSEngine> mEngine;
        };

        // minimal AE-compat project facade: app.project.activeItem
        class CORE_EXPORT JsProjectProxy : public QObject
        {
            Q_OBJECT
            Q_PROPERTY(QJSValue activeItem READ activeItem)
            Q_PROPERTY(QString file READ file)
            Q_PROPERTY(int numScenes READ numScenes)
        public:
            JsProjectProxy(JsHost * const host, QObject * const parent);
            QJSValue activeItem();
            QString file() const;
            int numScenes() const;
        private:
            JsHost *mHost;
        };

        class CORE_EXPORT JsAppProxy : public QObject
        {
            Q_OBJECT
            Q_PROPERTY(QString version READ version)
            Q_PROPERTY(QJSValue activeScene READ activeScene)
            Q_PROPERTY(QJSValue project READ project)
        public:
            JsAppProxy(JsHost * const host, QObject * const parent);
            QString version() const;
            QJSValue activeScene();
            QJSValue project();

            // AE app.beginUndoGroup()/endUndoGroup(): while a group is
            // open, per-operation actionFinished() calls are
            // suppressed so every undo record of the batch
            // accumulates in a single undo set (one undo step)
            Q_INVOKABLE void beginUndoGroup(const QString &name);
            Q_INVOKABLE void endUndoGroup();
        private:
            JsHost *mHost;
        };

        // Owns one QJSEngine and hosts the plugin API. A script file is
        // executed on load; registerCommand(label, fn) callbacks live in
        // this engine and are invoked via callCommand().
        class CORE_EXPORT JsHost : public QObject
        {
            Q_OBJECT
        public:
            explicit JsHost(QObject * const parent = nullptr);

            // UI-layer handlers for console output and dialogs
            using PrintHandler = std::function<void(const QString&)>;
            using AlertHandler = std::function<void(const QString&)>;
            using ConfirmHandler = std::function<bool(const QString&)>;
            void setHandlers(const PrintHandler &print,
                             const AlertHandler &alert,
                             const ConfirmHandler &confirm);

            // loads and evaluates the file; returns the error message
            // (uncaught exception) or an empty string on success
            QString loadScript(const QString &path);

            // evaluates an expression (console REPL); returns the
            // result string, prefixed with "Uncaught" on error
            QString evaluate(const QString &source);

            // run a registered command by label; returns error or empty
            QString callCommand(const QString &label);

            const QStringList commandLabels() const;

            Canvas *activeScene() const;
            // cached JS wrapper of the active scene (recreated when the
            // active scene changes); null QJSValue when there is none
            QJSValue appScene();
            // cached JS wrapper of app.project
            QJSValue appProject();

            // called from JS (Q_INVOKABLE so newQObject exposes them)
            Q_INVOKABLE void print(const QString &message);
            Q_INVOKABLE void alert(const QString &message);
            Q_INVOKABLE bool confirm(const QString &message);
            Q_INVOKABLE void registerCommand(const QString &label,
                                             const QJSValue &callable);
            // register a panel UI: config { title, buttons: [{label,
            // tooltip, onClick}, ...] } laid out in a square grid of
            // the given columns, plus optional extra full-width
            // buttons; the panel appears in the Scripts menu and as a
            // dockable panel (replaces any earlier panel of the
            // script)
            Q_INVOKABLE void registerPanel(const QJSValue &config);
            Q_INVOKABLE QJSValue appObject();
            Q_INVOKABLE QJSValue projectObject();

            // used by the UI layer to invoke stored panel callbacks
            void invokePanelButton(const int index);

        private:
            void installApi();

            std::unique_ptr<QJSEngine> mEngine;
            // label -> callable
            QMap<QString, QJSValue> mCommands;
            QJSValue mAppObj;
            QJSValue mProjectObj;
            QPointer<class JsSceneProxy> mSceneProxy;
            QJSValue mSceneObj;
            PrintHandler mPrintHandler;
            AlertHandler mAlertHandler;
            ConfirmHandler mConfirmHandler;

        public:
            // panel description filled by registerPanel(), consumed by
            // the UI layer (ScriptManager)
            struct PanelButton
            {
                QString label;
                QString tooltip;
                QJSValue onClick;
            };
            // slider control (row): label + slider + value box
            struct PanelSlider
            {
                QString label;
                QString id;
                qreal min = 0.;
                qreal max = 100.;
                qreal value = 0.;
                int decimals = 0;
                QJSValue onChanging; // called while dragging (value)
                QJSValue onChange;   // called on release (value)
            };
            // dropdown (combo) control: label + list of options
            struct PanelCombo
            {
                QString label;
                QString id;
                QStringList options;
                int index = 0;
                QJSValue onChange; // called with (index, optionText)
            };
            struct PanelDesc
            {
                QString title;
                int columns = 3;
                int buttonMinSize = 24;
                QList<PanelButton> buttons;      // grid buttons
                QList<PanelButton> extraButtons; // full-width below
                QList<PanelSlider> sliders;      // above the grid
                QList<PanelCombo> combos;        // above the grid
                bool valid = false;
            };
            const PanelDesc& panelDesc() const { return mPanelDesc; }
            void invokeButton(const QJSValue &callable);
            // invoke a slider/combo callback stored in the panel desc
            // (kind: 0=onChanging, 1=onChange; value: slider number or
            // combo index; text: selected combo option text)
            void invokePanelValue(const int kind, const QString &id,
                                  const qreal value, const QString &text);

        private:
            PanelDesc mPanelDesc;
        };
    }
}

#endif // JSAPI_H

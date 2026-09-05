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

#include "jsapi.h"

#include "Private/document.h"
#include "canvas.h"
#include "clipboardcontainer.h"
#include "Boxes/boundingbox.h"
#include "Boxes/containerbox.h"
#include "Boxes/rectangle.h"
#include "Boxes/circle.h"
#include "Boxes/textbox.h"
#include "Boxes/nullobject.h"
#include "Boxes/smartvectorpath.h"
#include "Animators/SmartPath/smartpathcollection.h"
#include "Animators/SmartPath/smartpathanimator.h"
#include "Animators/SmartPath/node.h"
#include "Animators/transformanimator.h"
#include "Boxes/pathbox.h"
#include "Animators/paintsettingsanimator.h"
#include "Animators/outlinesettingsanimator.h"
#include "Animators/qpointfanimator.h"
#include "Animators/qrealanimator.h"
#include "Animators/key.h"
#include "RasterEffects/rastereffectcollection.h"
#include "RasterEffects/rastereffect.h"
#include "textanimpresets.h"
#include "Private/esettings.h"
#include "Expressions/expression.h"
#include "Expressions/propertybindingparser.h"
#include "Timeline/durationrectangle.h"

#include <QFile>
#include <QTextStream>
#include <QTextCodec>
#include <QQmlEngine>

namespace Friction
{
    namespace Core
    {

        namespace
        {
            // collect BoundingBox layers of a container (top = index 0)
            QList<BoundingBox*> boxLayers(Canvas * const scene)
            {
                QList<BoundingBox*> result;
                if (!scene) { return result; }
                const auto &contained = scene->getContained();
                for (const auto &child : contained) {
                    const auto box = enve_cast<BoundingBox*>(child.get());
                    if (box) { result.append(box); }
                }
                return result;
            }

            Canvas *activeSceneOrNull()
            {
                if (!Document::sInstance) { return nullptr; }
                if (Document::sInstance->fActiveScene) {
                    return Document::sInstance->fActiveScene;
                }
                if (!Document::sInstance->fScenes.isEmpty()) {
                    return Document::sInstance->fScenes.first().get();
                }
                return Document::sInstance->createNewScene(true);
            }

            // collect selected boxes depth-first (document order), so
            // layers nested inside groups are found too
            void collectSelectedBoxes(ContainerBox * const container,
                                      QList<BoundingBox*> &out)
            {
                const auto &contained = container->getContained();
                for (const auto &child : contained) {
                    const auto box = enve_cast<BoundingBox*>(child.get());
                    if (!box) { continue; }
                    if (box->isSelected()) { out.append(box); }
                    const auto group = enve_cast<ContainerBox*>(child.get());
                    if (group) { collectSelectedBoxes(group, out); }
                }
            }

            qreal sceneFps()
            {
                const auto scene = activeSceneOrNull();
                return scene ? scene->getFps() : 1.;
            }

            QJSValue makeArray(QJSEngine * const e, const qreal a, const qreal b)
            {
                if (!e) { return QJSValue(); }
                auto arr = e->newArray(2);
                arr.setProperty(0, a);
                arr.setProperty(1, b);
                return arr;
            }

            bool readPoint(const QJSValue &v, qreal &x, qreal &y)
            {
                if (v.isArray()) {
                    x = v.property(0).toNumber();
                    y = v.property(1).toNumber();
                    return true;
                }
                if (v.isObject() && v.hasProperty("x") && v.hasProperty("y")) {
                    x = v.property("x").toNumber();
                    y = v.property("y").toNumber();
                    return true;
                }
                return false;
            }

            // standard enve undo-able value change on a scalar animator
            void setScalarValue(QrealAnimator * const anim, const qreal v)
            {
                if (!anim) { return; }
                anim->prp_startTransform();
                anim->setCurrentBaseValue(v);
                anim->prp_finishTransform();
            }

            // ---- script undo groups (AE app.beginUndoGroup) --------
            // Undo records accumulate in the scene's "current set"
            // until Document::actionFinished() closes it. Normally
            // every scripted operation calls actionFinished itself,
            // which would turn one script batch into many undo
            // steps. While a script undo group is open (depth > 0)
            // those per-op calls are suppressed so the whole batch
            // becomes a single undo step, closed by endUndoGroup().
            int sUndoGroupDepth = 0;

            void finishAction()
            {
                if (sUndoGroupDepth > 0) { return; }
                if (Document::sInstance) { Document::sInstance->actionFinished(); }
            }

            void beginUndoGroupBatch()
            {
                if (sUndoGroupDepth == 0 && Document::sInstance) {
                    // flush any records pending from earlier actions
                    // so they do not merge into the script's batch
                    Document::sInstance->actionFinished();
                }
                sUndoGroupDepth++;
            }

            void endUndoGroupBatch()
            {
                if (sUndoGroupDepth > 0) { sUndoGroupDepth--; }
                if (sUndoGroupDepth == 0 && Document::sInstance) {
                    Document::sInstance->actionFinished();
                }
            }

            // safety net for scripts that begin a group but never end
            // it (e.g. an uncaught exception): called by the host
            // after every scripted callback returns
            void forceCloseUndoGroup()
            {
                if (sUndoGroupDepth > 0) {
                    sUndoGroupDepth = 0;
                    if (Document::sInstance) {
                        Document::sInstance->actionFinished();
                    }
                }
            }

            bool applyEasingOnQreal(QrealAnimator * const anim, const FrameRange &range, const QString &easingName)
            {
                if (!anim || easingName.isEmpty() || !range.isValid() || range.fMin >= range.fMax) { return false; }

                QString normalized = easingName.trimmed();
                // Match common shorthand names to Friction core expression presets
                if (normalized.compare(QStringLiteral("easeOut"), Qt::CaseInsensitive) == 0) {
                    normalized = QStringLiteral("easeOutCubic");
                } else if (normalized.compare(QStringLiteral("easeIn"), Qt::CaseInsensitive) == 0) {
                    normalized = QStringLiteral("easeInCubic");
                } else if (normalized.compare(QStringLiteral("easeInOut"), Qt::CaseInsensitive) == 0) {
                    normalized = QStringLiteral("easeInOutCubic");
                } else if (normalized.compare(QStringLiteral("bounce"), Qt::CaseInsensitive) == 0) {
                    normalized = QStringLiteral("easeOutBounce");
                } else if (normalized.compare(QStringLiteral("elastic"), Qt::CaseInsensitive) == 0) {
                    normalized = QStringLiteral("easeOutElastic");
                } else if (normalized.compare(QStringLiteral("back"), Qt::CaseInsensitive) == 0 ||
                           normalized.compare(QStringLiteral("spring"), Qt::CaseInsensitive) == 0) {
                    normalized = QStringLiteral("easeOutBack");
                }

                if (!eSettings::sInstance) { return false; }

                auto preset = eSettings::sInstance->fExpressions.getExpr(normalized);
                if (!preset.valid) {
                    preset = eSettings::sInstance->fExpressions.getExpr(QStringLiteral("graphics.friction.") + normalized);
                }
                if (!preset.valid) {
                    const auto allCore = eSettings::sInstance->fExpressions.getAll();
                    for (const auto &p : allCore) {
                        if (p.id.endsWith(normalized, Qt::CaseInsensitive) ||
                            p.title.compare(normalized, Qt::CaseInsensitive) == 0) {
                            preset = p;
                            break;
                        }
                    }
                }
                if (!preset.valid || !preset.enabled) {
                    qWarning() << "[JsAPI] Easing preset not found or disabled:" << easingName;
                    return false;
                }

                QString script = preset.script;
                script.replace(QStringLiteral("__START_VALUE__"), QString::number(anim->getBaseValue(range.fMin)));
                script.replace(QStringLiteral("__END_VALUE__"), QString::number(anim->getBaseValue(range.fMax)));
                script.replace(QStringLiteral("__START_FRAME__"), QString::number(range.fMin));
                script.replace(QStringLiteral("__END_FRAME__"), QString::number(range.fMax));

                PropertyBindingMap bindings;
                try {
                    bindings = PropertyBindingParser::parseBindings(preset.bindings, nullptr, anim);
                } catch (const std::exception &) {
                    return false;
                }

                auto engine = std::make_unique<QJSEngine>();
                try {
                    Expression::sAddDefinitionsTo(preset.definitions, *engine);
                } catch (const std::exception &) {
                    return false;
                }

                QJSValue eEvaluate;
                try {
                    Expression::sAddScriptTo(script, bindings, *engine, eEvaluate, Expression::sQrealAnimatorTester);
                } catch (const std::exception &) {
                    return false;
                }

                const auto scene = anim->getParentScene();
                const bool cacheWasFresh = scene && scene->sceneFramesCacheIsFresh();
                FrameRange dirtyRange = range;
                if (const auto prevK = anim->anim_getPrevKey<Key>(range.fMin)) {
                    dirtyRange.fMin = prevK->getRelFrame();
                }
                if (const auto nextK = anim->anim_getNextKey<Key>(range.fMax)) {
                    dirtyRange.fMax = nextK->getRelFrame();
                }

                try {
                    auto expr = Expression::sCreate(preset.definitions, script, std::move(bindings),
                                                    std::move(engine), std::move(eEvaluate));
                    if (expr && !expr->isValid()) { expr = nullptr; }
                    anim->setExpression(expr, dirtyRange);
                    anim->applyExpression(range, 10, true, true);
                    if (cacheWasFresh && scene) {
                        const auto absDirty = anim->prp_relRangeToAbsRange(dirtyRange);
                        scene->getSceneFramesHandler().remove(absDirty);
                        scene->setCacheGen(scene->effectiveContentGen());
                    }
                    finishAction();
                } catch (const std::exception &) {
                    return false;
                }
                return true;
            }
        }

        //---------------------------- JsPropertyProxy ----------------------------

        JsPropertyProxy::JsPropertyProxy(const QPointer<Property> &prop,
                                         const Kind kind,
                                         QObject * const parent)
            : QObject(parent)
            , mProp(prop)
            , mKind(kind)
        {}

        JsPropertyProxy::~JsPropertyProxy() = default;

        qreal JsPropertyProxy::fps() const
        {
            return sceneFps();
        }

        QJSValue JsPropertyProxy::value()
        {
            if (!mProp) { return QJSValue(QJSValue::NullValue); }
            const auto engine = qjsEngine(this);
            if (!engine) { return QJSValue(); }
            if (mKind == Kind::Point) {
                const auto point = static_cast<QPointFAnimator*>(mProp.data());
                const auto x = point->getXAnimator();
                const auto y = point->getYAnimator();
                return makeArray(engine,
                                 x ? x->getCurrentBaseValue() : 0.,
                                 y ? y->getCurrentBaseValue() : 0.);
            }
            const auto scalar = static_cast<QrealAnimator*>(mProp.data());
            return QJSValue(scalar->getCurrentBaseValue());
        }

        void JsPropertyProxy::setValue(const QJSValue &v)
        {
            if (!mProp) { return; }
            if (mKind == Kind::Point) {
                qreal x = 0.;
                qreal y = 0.;
                if (!readPoint(v, x, y)) { return; }
                const auto point = static_cast<QPointFAnimator*>(mProp.data());
                setScalarValue(point->getXAnimator(), x);
                setScalarValue(point->getYAnimator(), y);
            } else {
                const auto scalar = static_cast<QrealAnimator*>(mProp.data());
                setScalarValue(scalar, v.toNumber());
            }
        }

        QJSValue JsPropertyProxy::valueAtFrame(const int frame)
        {
            if (!mProp) { return QJSValue(QJSValue::NullValue); }
            const auto engine = qjsEngine(this);
            if (!engine) { return QJSValue(); }
            if (mKind == Kind::Point) {
                const auto point = static_cast<QPointFAnimator*>(mProp.data());
                const auto x = point->getXAnimator();
                const auto y = point->getYAnimator();
                return makeArray(engine,
                                 x ? x->getBaseValue(frame) : 0.,
                                 y ? y->getBaseValue(frame) : 0.);
            }
            const auto scalar = static_cast<QrealAnimator*>(mProp.data());
            return QJSValue(scalar->getBaseValue(frame));
        }

        QJSValue JsPropertyProxy::valueAtTime(const qreal seconds)
        {
            return valueAtFrame(qRound(seconds * fps()));
        }

        void JsPropertyProxy::setValueAtFrame(const int frame,
                                              const QJSValue &v)
        {
            if (!mProp) { return; }
            if (mKind == Kind::Point) {
                qreal x = 0.;
                qreal y = 0.;
                if (!readPoint(v, x, y)) { return; }
                const auto point = static_cast<QPointFAnimator*>(mProp.data());
                if (point->getXAnimator()) {
                    point->getXAnimator()->saveValueToKey(frame, x);
                }
                if (point->getYAnimator()) {
                    point->getYAnimator()->saveValueToKey(frame, y);
                }
            } else {
                const auto scalar = static_cast<QrealAnimator*>(mProp.data());
                scalar->saveValueToKey(frame, v.toNumber());
            }
        }

        void JsPropertyProxy::setValueAtTime(const qreal seconds,
                                             const QJSValue &v)
        {
            setValueAtFrame(qRound(seconds * fps()), v);
        }

        int JsPropertyProxy::numKeys()
        {
            if (!mProp) { return 0; }
            if (mKind == Kind::Point) {
                const auto point = static_cast<QPointFAnimator*>(mProp.data());
                const auto x = point->getXAnimator();
                return x ? x->anim_getKeys().count() : 0;
            }
            const auto scalar = static_cast<QrealAnimator*>(mProp.data());
            return scalar->anim_getKeys().count();
        }

        qreal JsPropertyProxy::keyTime(const int index)
        {
            return keyFrame(index) / fps();
        }

        int JsPropertyProxy::keyFrame(const int index)
        {
            if (!mProp || index < 1) { return -1; }
            const Animator *anim = nullptr;
            if (mKind == Kind::Point) {
                const auto point = static_cast<QPointFAnimator*>(mProp.data());
                anim = point->getXAnimator();
            } else {
                anim = static_cast<QrealAnimator*>(mProp.data());
            }
            if (!anim || index > anim->anim_getKeys().count()) { return -1; }
            // OverlappingKeyList supports iteration only
            int n = 0;
            for (const auto &key : anim->anim_getKeys()) {
                if (++n == index) { return key->getAbsFrame(); }
            }
            return -1;
        }

        void JsPropertyProxy::removeKeyAtFrame(const int frame)
        {
            if (!mProp) { return; }
            QList<Animator*> animators;
            if (mKind == Kind::Point) {
                const auto point = static_cast<QPointFAnimator*>(mProp.data());
                if (point->getXAnimator()) { animators << point->getXAnimator(); }
                if (point->getYAnimator()) { animators << point->getYAnimator(); }
            } else {
                animators << static_cast<QrealAnimator*>(mProp.data());
            }
            for (const auto anim : animators) {
                const auto key = anim->
                        template anim_getKeyAtAbsFrame<Key>(frame);
                if (key) { anim->anim_removeKeyAction(key->ref<Key>()); }
            }
        }

        bool JsPropertyProxy::setEasing(const QString &easing,
                                        const int startFrame,
                                        const int endFrame)
        {
            if (!mProp) { return false; }
            if (mKind == Kind::Point) {
                const auto point = static_cast<QPointFAnimator*>(mProp.data());
                if (!point) { return false; }
                const auto x = point->getXAnimator();
                const auto y = point->getYAnimator();
                bool okX = false;
                bool okY = false;
                if (x) {
                    FrameRange r{startFrame, endFrame};
                    if (startFrame < 0 || endFrame < 0) {
                        const auto keys = x->anim_getKeys();
                        if (keys.count() >= 2) {
                            r = {keys.first()->getRelFrame(), keys.last()->getRelFrame()};
                        }
                    }
                    if (r.isValid()) {
                        okX = applyEasingOnQreal(x, r, easing);
                    }
                }
                if (y) {
                    FrameRange r{startFrame, endFrame};
                    if (startFrame < 0 || endFrame < 0) {
                        const auto keys = y->anim_getKeys();
                        if (keys.count() >= 2) {
                            r = {keys.first()->getRelFrame(), keys.last()->getRelFrame()};
                        }
                    }
                    if (r.isValid()) {
                        okY = applyEasingOnQreal(y, r, easing);
                    }
                }
                return okX || okY;
            } else {
                const auto scalar = static_cast<QrealAnimator*>(mProp.data());
                if (!scalar) { return false; }
                FrameRange r{startFrame, endFrame};
                if (startFrame < 0 || endFrame < 0) {
                    const auto keys = scalar->anim_getKeys();
                    if (keys.count() >= 2) {
                        r = {keys.first()->getRelFrame(), keys.last()->getRelFrame()};
                    }
                }
                if (r.isValid()) {
                    return applyEasingOnQreal(scalar, r, easing);
                }
            }
            return false;
        }

        void JsPropertyProxy::setValueAtFrameWithEasing(const int frame,
                                                        const QJSValue &v,
                                                        const QString &easing)
        {
            if (!mProp) { return; }
            int prevFrame = -1;
            bool hasPrev = false;
            if (mKind == Kind::Point) {
                const auto point = static_cast<QPointFAnimator*>(mProp.data());
                if (point && point->getXAnimator()) {
                    if (const auto prevK = point->getXAnimator()->anim_getPrevKey<Key>(frame)) {
                        prevFrame = prevK->getRelFrame();
                        hasPrev = true;
                    }
                }
            } else {
                const auto scalar = static_cast<QrealAnimator*>(mProp.data());
                if (scalar) {
                    if (const auto prevK = scalar->anim_getPrevKey<Key>(frame)) {
                        prevFrame = prevK->getRelFrame();
                        hasPrev = true;
                    }
                }
            }
            setValueAtFrame(frame, v);
            if (hasPrev && prevFrame < frame && !easing.isEmpty()) {
                setEasing(easing, prevFrame, frame);
            }
        }

        void JsPropertyProxy::setValueAtTimeWithEasing(const qreal seconds,
                                                       const QJSValue &v,
                                                       const QString &easing)
        {
            setValueAtFrameWithEasing(qRound(seconds * fps()), v, easing);
        }

        //---------------------------- JsLayerProxy ----------------------------

        JsLayerProxy::JsLayerProxy(const QPointer<BoundingBox> &box,
                                   QJSEngine * const engine,
                                   QObject * const parent)
            : QObject(parent)
            , mBox(box)
            , mEngine(engine)
        {}

        JsLayerProxy::~JsLayerProxy() = default;

        BoundingBox *JsLayerProxy::box() const { return mBox.data(); }

        QString JsLayerProxy::name() const
        {
            return mBox ? mBox->prp_getName() : QString();
        }

        QString JsLayerProxy::type() const
        {
            if (!mBox) { return QString(); }
            switch (mBox->getBoxType()) {
            case eBoxType::vectorPath: return QStringLiteral("path");
            case eBoxType::circle: return QStringLiteral("circle");
            case eBoxType::image: return QStringLiteral("image");
            case eBoxType::rectangle: return QStringLiteral("rectangle");
            case eBoxType::text: return QStringLiteral("text");
            case eBoxType::layer: return QStringLiteral("layer");
            case eBoxType::canvas: return QStringLiteral("canvas");
            case eBoxType::group: return QStringLiteral("group");
            case eBoxType::nullObject: return QStringLiteral("null");
            case eBoxType::adjustmentLayer: return QStringLiteral("adjustment");
            case eBoxType::solid: return QStringLiteral("solid");
            case eBoxType::cameraLayer: return QStringLiteral("camera");
            default: return QStringLiteral("layer");
            }
        }

        QString JsLayerProxy::text() const
        {
            if (!mBox) { return QString(); }
            const auto textBox = enve_cast<TextBox*>(mBox.data());
            return textBox ? textBox->getCurrentValue() : QString();
        }

        void JsLayerProxy::setName(const QString &name)
        {
            if (mBox) { mBox->prp_setName(name); }
        }

        int JsLayerProxy::index() const
        {
            if (!mBox) { return -1; }
            const auto scene = activeSceneOrNull();
            const auto layers = boxLayers(scene);
            const int idx = layers.indexOf(mBox.data());
            // AE convention: 1-based from top
            return idx < 0 ? -1 : idx + 1;
        }

        bool JsLayerProxy::visible() const
        {
            return mBox ? mBox->isVisible() : false;
        }

        void JsLayerProxy::setVisible(const bool visible)
        {
            if (mBox) { mBox->setVisible(visible); }
        }

        bool JsLayerProxy::selected() const
        {
            return mBox ? mBox->isSelected() : false;
        }

        void JsLayerProxy::setSelected(const bool selected)
        {
            if (mBox) { mBox->setSelected(selected); }
        }

        qreal JsLayerProxy::opacity() const
        {
            return mBox ? mBox->getOpacity(mBox->anim_getCurrentAbsFrame()) : 0.;
        }

        void JsLayerProxy::setOpacity(const qreal opacity)
        {
            if (mBox) { mBox->setOpacity(opacity); }
        }

        QJSValue JsLayerProxy::makeProperty(const QString &name)
        {
            if (!mBox || !mEngine) { return QJSValue(QJSValue::NullValue); }
            const auto transform = mBox->getTransformAnimator();
            if (!transform) { return QJSValue(QJSValue::NullValue); }

            const QString n = name.toLower();
            Property *prop = nullptr;
            auto kind = JsPropertyProxy::Kind::Scalar;
            if (n == "position" || n == "pos") {
                prop = transform->getPosAnimator();
                kind = JsPropertyProxy::Kind::Point;
            } else if (n == "scale") {
                prop = transform->getScaleAnimator();
                kind = JsPropertyProxy::Kind::Point;
            } else if (n == "rotation" || n == "rot") {
                prop = transform->getRotAnimator();
            } else if (n == "rotationx" || n == "rotx"
                       || n == "xrotation" || n == "3drotationx") {
                const auto boxTrans = mBox->getBoxTransformAnimator();
                if (!boxTrans) { return QJSValue(QJSValue::NullValue); }
                prop = boxTrans->getRotXAnimator();
            } else if (n == "rotationy" || n == "roty"
                       || n == "yrotation" || n == "3drotationy") {
                const auto boxTrans = mBox->getBoxTransformAnimator();
                if (!boxTrans) { return QJSValue(QJSValue::NullValue); }
                prop = boxTrans->getRotYAnimator();
            } else if (n == "zposition" || n == "zpos" || n == "z") {
                const auto boxTrans = mBox->getBoxTransformAnimator();
                if (!boxTrans) { return QJSValue(QJSValue::NullValue); }
                prop = boxTrans->getZPosAnimator();
            } else if (n == "perspective" || n == "persp"
                       || n == "3dperspective") {
                const auto boxTrans = mBox->getBoxTransformAnimator();
                if (!boxTrans) { return QJSValue(QJSValue::NullValue); }
                prop = boxTrans->getPerspectiveAnimator();
            } else if (n == "opacity" || n == "op") {
                const auto boxTrans = mBox->getBoxTransformAnimator();
                if (!boxTrans) { return QJSValue(QJSValue::NullValue); }
                prop = boxTrans->getOpacityAnimator();
            } else {
                return QJSValue(QJSValue::NullValue);
            }
            if (!prop) { return QJSValue(QJSValue::NullValue); }
            const auto proxy = new JsPropertyProxy(QPointer<Property>(prop),
                                                   kind, nullptr);
            return mEngine->newQObject(proxy);
        }

        QJSValue JsLayerProxy::property(const QString &name)
        {
            return makeProperty(name);
        }

        QJSValue JsLayerProxy::position()
        {
            return makeProperty(QStringLiteral("position"));
        }

        QJSValue JsLayerProxy::scale()
        {
            return makeProperty(QStringLiteral("scale"));
        }

        QJSValue JsLayerProxy::rotation()
        {
            return makeProperty(QStringLiteral("rotation"));
        }

        QJSValue JsLayerProxy::rotationX()
        {
            return makeProperty(QStringLiteral("rotationx"));
        }

        QJSValue JsLayerProxy::rotationY()
        {
            return makeProperty(QStringLiteral("rotationy"));
        }

        QJSValue JsLayerProxy::zPosition()
        {
            return makeProperty(QStringLiteral("zposition"));
        }

        QJSValue JsLayerProxy::perspective()
        {
            return makeProperty(QStringLiteral("perspective"));
        }

        QJSValue JsLayerProxy::opacityProp()
        {
            return makeProperty(QStringLiteral("opacity"));
        }

        bool JsLayerProxy::setFillColor(const QString &color)
        {
            if (!mBox) { return false; }
            const auto pathBox = enve_cast<PathBox*>(mBox.data());
            if (!pathBox || !pathBox->getFillSettings()) { return false; }
            const QString c = color.trimmed().toLower();
            if (c == QStringLiteral("transparent") || c == QStringLiteral("none") || c.isEmpty()) {
                pathBox->getFillSettings()->setPaintType(PaintType::NOPAINT);
            } else {
                pathBox->getFillSettings()->setPaintType(PaintType::FLATPAINT);
                pathBox->getFillSettings()->setCurrentColor(QColor(color));
            }
            finishAction();
            return true;
        }

        bool JsLayerProxy::setStrokeColor(const QString &color)
        {
            if (!mBox) { return false; }
            const auto pathBox = enve_cast<PathBox*>(mBox.data());
            if (!pathBox || !pathBox->getStrokeSettings()) { return false; }
            const QString c = color.trimmed().toLower();
            if (c == QStringLiteral("transparent") || c == QStringLiteral("none") || c.isEmpty()) {
                pathBox->getStrokeSettings()->setPaintType(PaintType::NOPAINT);
            } else {
                pathBox->getStrokeSettings()->setPaintType(PaintType::FLATPAINT);
                pathBox->getStrokeSettings()->setCurrentColor(QColor(color));
            }
            finishAction();
            return true;
        }

        bool JsLayerProxy::setStrokeWidth(const qreal width)
        {
            if (!mBox) { return false; }
            const auto pathBox = enve_cast<PathBox*>(mBox.data());
            if (!pathBox || !pathBox->getStrokeSettings()) { return false; }
            if (width <= 0.) {
                pathBox->getStrokeSettings()->setPaintType(PaintType::NOPAINT);
                pathBox->getStrokeSettings()->setCurrentStrokeWidth(0.);
            } else {
                pathBox->getStrokeSettings()->setPaintType(PaintType::FLATPAINT);
                pathBox->getStrokeSettings()->setCurrentStrokeWidth(width);
            }
            finishAction();
            return true;
        }

        bool JsLayerProxy::setFontSize(const qreal size)
        {
            if (!mBox) { return false; }
            const auto textBox = enve_cast<TextBox*>(mBox.data());
            if (!textBox) { return false; }
            textBox->setFontSize(size);
            finishAction();
            return true;
        }

        bool JsLayerProxy::setText(const QString &text)
        {
            if (!mBox) { return false; }
            const auto textBox = enve_cast<TextBox*>(mBox.data());
            if (!textBox) { return false; }
            textBox->setCurrentValue(text);
            finishAction();
            return true;
        }

        bool JsLayerProxy::setTextAlignment(const QString &align)
        {
            if (!mBox) { return false; }
            const auto textBox = enve_cast<TextBox*>(mBox.data());
            if (!textBox) { return false; }
            const QString a = align.toLower();
            if (a == QStringLiteral("center")) {
                textBox->setTextHAlignment(Qt::AlignHCenter);
                textBox->setTextVAlignment(Qt::AlignVCenter);
            } else if (a == QStringLiteral("right")) {
                textBox->setTextHAlignment(Qt::AlignRight);
                textBox->setTextVAlignment(Qt::AlignVCenter);
            } else {
                textBox->setTextHAlignment(Qt::AlignLeft);
                textBox->setTextVAlignment(Qt::AlignVCenter);
            }
            finishAction();
            return true;
        }

        bool JsLayerProxy::setFontFamily(const QString &family)
        {
            if (!mBox) { return false; }
            const auto textBox = enve_cast<TextBox*>(mBox.data());
            if (!textBox) { return false; }
            textBox->setFontFamilyAndStyle(family, textBox->getFontStyle());
            finishAction();
            return true;
        }

        bool JsLayerProxy::setLetterSpacing(const qreal spacing)
        {
            if (!mBox) { return false; }
            const auto textBox = enve_cast<TextBox*>(mBox.data());
            if (!textBox) { return false; }
            textBox->setLetterSpacing(spacing);
            finishAction();
            return true;
        }

        bool JsLayerProxy::setLineSpacing(const qreal spacing)
        {
            if (!mBox) { return false; }
            const auto textBox = enve_cast<TextBox*>(mBox.data());
            if (!textBox) { return false; }
            textBox->setLineSpacing(spacing);
            finishAction();
            return true;
        }

        bool JsLayerProxy::setBlendMode(const QString &mode)
        {
            if (!mBox) { return false; }
            const QString m = mode.toLower().remove(QLatin1Char('_')).remove(QLatin1Char('-'));
            SkBlendMode bm = SkBlendMode::kSrcOver;
            if (m == QStringLiteral("multiply")) bm = SkBlendMode::kMultiply;
            else if (m == QStringLiteral("screen")) bm = SkBlendMode::kScreen;
            else if (m == QStringLiteral("overlay")) bm = SkBlendMode::kOverlay;
            else if (m == QStringLiteral("darken")) bm = SkBlendMode::kDarken;
            else if (m == QStringLiteral("lighten")) bm = SkBlendMode::kLighten;
            else if (m == QStringLiteral("colordodge") || m == QStringLiteral("dodge")) bm = SkBlendMode::kColorDodge;
            else if (m == QStringLiteral("colorburn") || m == QStringLiteral("burn")) bm = SkBlendMode::kColorBurn;
            else if (m == QStringLiteral("hardlight")) bm = SkBlendMode::kHardLight;
            else if (m == QStringLiteral("softlight")) bm = SkBlendMode::kSoftLight;
            else if (m == QStringLiteral("difference")) bm = SkBlendMode::kDifference;
            else if (m == QStringLiteral("exclusion")) bm = SkBlendMode::kExclusion;
            else if (m == QStringLiteral("plus") || m == QStringLiteral("add")) bm = SkBlendMode::kPlus;
            else if (m == QStringLiteral("clear")) bm = SkBlendMode::kClear;
            else if (m == QStringLiteral("src")) bm = SkBlendMode::kSrc;
            else if (m == QStringLiteral("dst")) bm = SkBlendMode::kDst;
            else if (m == QStringLiteral("srcatop")) bm = SkBlendMode::kSrcATop;
            else if (m == QStringLiteral("dstatop")) bm = SkBlendMode::kDstATop;
            else if (m == QStringLiteral("xor")) bm = SkBlendMode::kXor;

            mBox->setBlendMode(bm);
            finishAction();
            return true;
        }

        bool JsLayerProxy::setCornerRadius(const qreal radius)
        {
            if (!mBox) { return false; }
            const auto rectBox = enve_cast<RectangleBox*>(mBox.data());
            if (rectBox) {
                rectBox->setXRadius(radius);
                rectBox->setYRadius(radius);
                finishAction();
                return true;
            }
            return false;
        }

        bool JsLayerProxy::setRadius(const qreal radius)
        {
            if (!mBox) { return false; }
            const auto circle = enve_cast<Circle*>(mBox.data());
            if (circle) {
                circle->setRadius(radius);
                finishAction();
                return true;
            }
            return setCornerRadius(radius);
        }

        bool JsLayerProxy::setSize(const qreal width, const qreal height)
        {
            if (!mBox) { return false; }
            const auto rectBox = enve_cast<RectangleBox*>(mBox.data());
            if (rectBox) {
                const auto tlAnim = rectBox->getTopLeftAnimator();
                const QPointF tl = tlAnim ? tlAnim->getBaseValue() : QPointF(-width / 2.0, -height / 2.0);
                rectBox->setBottomRightPos(QPointF(tl.x() + width, tl.y() + height));
                finishAction();
                return true;
            }
            const auto circle = enve_cast<Circle*>(mBox.data());
            if (circle) {
                circle->setRadius(qMin(width, height) / 2.0);
                finishAction();
                return true;
            }
            return false;
        }

        bool JsLayerProxy::addEffect(const QString &effectType)
        {
            if (!mBox) { return false; }
            const auto coll = mBox->rasterEffectsCollection();
            if (!coll) { return false; }
            const QString n = effectType.toLower().remove(QLatin1Char('_')).remove(QLatin1Char('-'));
            RasterEffectType type = RasterEffectType::BLUR;
            if (n == QStringLiteral("glow")) type = RasterEffectType::GLOW;
            else if (n == QStringLiteral("liquidglass")) type = RasterEffectType::LIQUID_GLASS;
            else if (n == QStringLiteral("vignette")) type = RasterEffectType::VIGNETTE;
            else if (n == QStringLiteral("chromaticaberration")) type = RasterEffectType::CHROMATIC_ABERRATION;
            else if (n == QStringLiteral("scanlines")) type = RasterEffectType::SCANLINES;
            else if (n == QStringLiteral("glitch")) type = RasterEffectType::GLITCH;
            else if (n == QStringLiteral("dropshadow") || n == QStringLiteral("shadow")) type = RasterEffectType::DROP_SHADOW;
            else if (n == QStringLiteral("blur") || n == QStringLiteral("gaussianblur")) type = RasterEffectType::BLUR;
            else if (n == QStringLiteral("motionblur")) type = RasterEffectType::MOTION_BLUR;
            else if (n == QStringLiteral("directionalblur")) type = RasterEffectType::DIRECTIONAL_BLUR;
            else if (n == QStringLiteral("radialblur")) type = RasterEffectType::RADIAL_BLUR;
            else if (n == QStringLiteral("zoomblur")) type = RasterEffectType::ZOOM_BLUR;
            else if (n == QStringLiteral("wavewarp") || n == QStringLiteral("wave")) type = RasterEffectType::WAVE_WARP;
            else if (n == QStringLiteral("tint")) type = RasterEffectType::TINT;
            else if (n == QStringLiteral("invert")) type = RasterEffectType::INVERT;
            else if (n == QStringLiteral("pixelate")) type = RasterEffectType::PIXELATE;
            else if (n == QStringLiteral("pixelart")) type = RasterEffectType::PIXEL_ART;
            else if (n == QStringLiteral("noise")) type = RasterEffectType::NOISE;
            else if (n == QStringLiteral("filmgrain")) type = RasterEffectType::FILM_GRAIN;
            else if (n == QStringLiteral("halftone")) type = RasterEffectType::HALFTONE;
            else if (n == QStringLiteral("posterize")) type = RasterEffectType::POSTERIZE;
            else if (n == QStringLiteral("twirl")) type = RasterEffectType::TWIRL;
            else if (n == QStringLiteral("shake")) type = RasterEffectType::SHAKE;
            else if (n == QStringLiteral("stripe")) type = RasterEffectType::STRIPE;
            else if (n == QStringLiteral("colorgrading")) type = RasterEffectType::COLOR_GRADING;
            else if (n == QStringLiteral("brightnesscontrast")) type = RasterEffectType::BRIGHTNESS_CONTRAST;
            else if (n == QStringLiteral("colorize")) type = RasterEffectType::COLORIZE;
            else if (n == QStringLiteral("lightsweep")) type = RasterEffectType::LIGHT_SWEEP;
            else if (n == QStringLiteral("fractalnoise")) type = RasterEffectType::FRACTAL_NOISE;
            else if (n == QStringLiteral("motiontile")) type = RasterEffectType::MOTION_TILE;
            else if (n == QStringLiteral("edgedetect")) type = RasterEffectType::EDGE_DETECT;
            else if (n == QStringLiteral("rain")) type = RasterEffectType::RAIN;
            else if (n == QStringLiteral("mirror")) type = RasterEffectType::MIRROR;
            else if (n == QStringLiteral("chromakey")) type = RasterEffectType::CHROMA_KEY;
            else if (n == QStringLiteral("displacementwarp") || n == QStringLiteral("displacement")) type = RasterEffectType::DISPLACEMENT_WARP;
            else if (n == QStringLiteral("blackwhiteflash") || n == QStringLiteral("bwflash") || n == QStringLiteral("flash")) type = RasterEffectType::BLACK_WHITE_FLASH;
            else if (n == QStringLiteral("channelblur")) type = RasterEffectType::CHANNEL_BLUR;
            else if (n == QStringLiteral("letterbox")) type = RasterEffectType::LETTERBOX;
            else if (n == QStringLiteral("noisefade")) type = RasterEffectType::NOISE_FADE;
            else if (n == QStringLiteral("wipe")) type = RasterEffectType::WIPE;

            auto eff = createRasterEffectForNonCustomType(type);
            if (eff) {
                coll->addChild(eff);
                finishAction();
                return true;
            }
            return false;
        }

        bool JsLayerProxy::removeEffect(const int index)
        {
            if (!mBox) { return false; }
            const auto coll = mBox->rasterEffectsCollection();
            if (!coll) { return false; }
            if (index >= 0 && index < coll->ca_getNumberOfChildren()) {
                const auto child = coll->getChild(index);
                if (child) {
                    coll->removeChild(child->ref<RasterEffect>());
                    finishAction();
                    return true;
                }
            }
            return false;
        }

        QJSValue JsLayerProxy::effects()
        {
            if (!mBox || !mEngine) { return QJSValue(QJSValue::NullValue); }
            const auto coll = mBox->rasterEffectsCollection();
            if (!coll) { return mEngine->newArray(); }
            const int count = coll->ca_getNumberOfChildren();
            auto arr = mEngine->newArray(count);
            for (int i = 0; i < count; ++i) {
                const auto child = coll->getChild(i);
                arr.setProperty(i, child ? child->prp_getName() : QString());
            }
            return arr;
        }

        bool JsLayerProxy::applyTextPreset(const QString &presetId,
                                           const qreal startFrame,
                                           const qreal durationScale,
                                           const bool out)
        {
            if (!mBox) { return false; }
            const auto textBox = enve_cast<TextBox*>(mBox.data());
            if (!textBox) { return false; }
            const auto preset = TextAnimPresets::byId(presetId);
            if (!preset) { return false; }
            const qreal fps = sceneFps();
            const bool ok = TextAnimPresets::apply(textBox, *preset, qRound(startFrame), fps, durationScale, out);
            if (ok) {
                finishAction();
            }
            return ok;
        }

        QJSValue JsLayerProxy::textPresets()
        {
            if (!mEngine) { return QJSValue(QJSValue::NullValue); }
            const auto &presets = TextAnimPresets::all();
            auto arr = mEngine->newArray(presets.count());
            for (int i = 0; i < presets.count(); ++i) {
                const auto &p = presets.at(i);
                auto obj = mEngine->newObject();
                obj.setProperty(QStringLiteral("id"), p.id);
                obj.setProperty(QStringLiteral("name"), p.name);
                obj.setProperty(QStringLiteral("desc"), p.desc);
                obj.setProperty(QStringLiteral("category"), p.category);
                obj.setProperty(QStringLiteral("duration"), p.duration);
                arr.setProperty(i, obj);
            }
            return arr;
        }

        bool JsLayerProxy::isLocked() const
        {
            return mBox ? mBox->isLocked() : false;
        }

        void JsLayerProxy::setLocked(const bool locked)
        {
            if (mBox) {
                mBox->setLocked(locked);
                finishAction();
            }
        }

        bool JsLayerProxy::is3DEnabled()
        {
            if (!mBox) { return false; }
            const auto trans = mBox->getBoxTransformAnimator();
            return trans ? trans->is3DEnabled() : false;
        }

        void JsLayerProxy::set3DEnabled(const bool enabled)
        {
            if (!mBox) { return; }
            const auto trans = mBox->getBoxTransformAnimator();
            if (trans) { trans->set3DEnabled(enabled); }
        }

        QJSValue JsLayerProxy::worldPosition()
        {
            if (!mBox || !mEngine) { return QJSValue(QJSValue::NullValue); }
            const auto transform = mBox->getBoxTransformAnimator();
            if (!transform) { return QJSValue(QJSValue::NullValue); }
            const auto pos = transform->getPivotAbs();
            return makeArray(mEngine.data(), pos.x(), pos.y());
        }

        bool JsLayerProxy::setWorldPosition(const QJSValue &v)
        {
            if (!mBox) { return false; }
            qreal x = 0.;
            qreal y = 0.;
            if (!readPoint(v, x, y)) { return false; }
            const auto transform = mBox->getBoxTransformAnimator();
            if (!transform) { return false; }
            // move so the pivot lands at the scene position (AE position)
            mBox->startPivotTransform();
            const auto absPos = transform->getPivotAbs();
            const QPointF relDelta = transform->mapAbsPosToRel(
                        QPointF(x, y)) - transform->mapAbsPosToRel(absPos);
            transform->translate(relDelta.x(), relDelta.y());
            mBox->finishPivotTransform();
            finishAction();
            return true;
        }

        QJSValue JsLayerProxy::bounds()
        {
            // content bounds in layer coordinates (like AE
            // sourceRectAtTime: unaffected by the layer transform)
            if (!mBox || !mEngine) { return QJSValue(QJSValue::NullValue); }
            const auto r = mBox->getRelBoundingRect();
            auto obj = mEngine->newObject();
            obj.setProperty(QStringLiteral("left"), r.left());
            obj.setProperty(QStringLiteral("top"), r.top());
            obj.setProperty(QStringLiteral("width"), r.width());
            obj.setProperty(QStringLiteral("height"), r.height());
            obj.setProperty(QStringLiteral("right"), r.right());
            obj.setProperty(QStringLiteral("bottom"), r.bottom());
            return obj;
        }

        QJSValue JsLayerProxy::worldBounds()
        {
            // content bounds in scene coordinates
            if (!mBox || !mEngine) { return QJSValue(QJSValue::NullValue); }
            const auto r = mBox->getAbsBoundingRect();
            auto obj = mEngine->newObject();
            obj.setProperty(QStringLiteral("left"), r.left());
            obj.setProperty(QStringLiteral("top"), r.top());
            obj.setProperty(QStringLiteral("width"), r.width());
            obj.setProperty(QStringLiteral("height"), r.height());
            obj.setProperty(QStringLiteral("right"), r.right());
            obj.setProperty(QStringLiteral("bottom"), r.bottom());
            return obj;
        }

        QJSValue JsLayerProxy::anchorPoint()
        {
            if (!mBox || !mEngine) { return QJSValue(QJSValue::NullValue); }
            const auto transform = mBox->getBoxTransformAnimator();
            if (!transform) { return QJSValue(QJSValue::NullValue); }
            const auto pivot = transform->getPivot();
            return makeArray(mEngine.data(), pivot.x(), pivot.y());
        }

        void JsLayerProxy::setAnchorPoint(const QJSValue &v)
        {
            if (!mBox) { return; }
            qreal x = 0.;
            qreal y = 0.;
            if (!readPoint(v, x, y)) { return; }
            // setPivotRelPos -> setPivotFixedTransform keeps the layer
            // visually in place while the pivot moves (AE anchor move)
            mBox->setPivotRelPos(QPointF(x, y));
            finishAction();
        }

        bool JsLayerProxy::setParentLayer(const QJSValue &parent)
        {
            if (!mBox) { return false; }
            const auto proxy = qobject_cast<JsLayerProxy*>(parent.toQObject());
            if (!proxy || !proxy->valid()) { return false; }
            const auto parentBox = proxy->box();
            const auto parentGroup = enve_cast<ContainerBox*>(parentBox);
            if (!parentGroup) { return false; }
            if (parentGroup == mBox->getParentGroup()) { return true; }
            const auto boxSP = mBox->ref<BoundingBox>();
            // world position before the re-parent
            const auto transform = mBox->getBoxTransformAnimator();
            const QPointF worldBefore = transform
                    ? transform->getPivotAbs() : QPointF();
            // same reparent flow as Canvas::groupSelectedBoxes
            boxSP->removeFromParent_k();
            parentGroup->addContained(boxSP);
            // a child's position is expressed in the PARENT coordinate
            // system, so re-parenting shifts the layer visually;
            // restore the exact world position
            if (transform) {
                const QPointF worldAfter = transform->getPivotAbs();
                if (!qFuzzyCompare(worldBefore.x(), worldAfter.x()) ||
                    !qFuzzyCompare(worldBefore.y(), worldAfter.y())) {
                    const QPointF relDelta = transform->mapAbsPosToRel(
                                worldBefore) - transform->mapAbsPosToRel(
                                worldAfter);
                    transform->translate(relDelta.x(), relDelta.y());
                }
            }
            finishAction();
            return true;
        }

        bool JsLayerProxy::setInPoint(const int frame)
        {
            if (!mBox) { return false; }
            if (!mBox->hasDurationRectangle()) {
                mBox->createDurationRectangle();
            }
            const auto dur = mBox->getDurationRectangle();
            if (!dur) { return false; }
            mBox->startMinFramePosTransform();
            dur->setMinAbsFrame(qMin(frame, dur->getMaxAbsFrame() - 1));
            mBox->finishMinFramePosTransform();
            finishAction();
            return true;
        }

        bool JsLayerProxy::setOutPoint(const int frame)
        {
            if (!mBox) { return false; }
            if (!mBox->hasDurationRectangle()) {
                mBox->createDurationRectangle();
            }
            const auto dur = mBox->getDurationRectangle();
            if (!dur) { return false; }
            mBox->startMaxFramePosTransform();
            dur->setMaxAbsFrame(qMax(frame, dur->getMinAbsFrame() + 1));
            mBox->finishMaxFramePosTransform();
            finishAction();
            return true;
        }

        int JsLayerProxy::inPoint() const
        {
            if (!mBox || !mBox->hasDurationRectangle()) { return 0; }
            const auto dur = mBox->getDurationRectangle();
            return dur ? dur->getMinAbsFrame() : 0;
        }

        int JsLayerProxy::outPoint() const
        {
            if (!mBox) { return 0; }
            if (mBox->hasDurationRectangle()) {
                const auto dur = mBox->getDurationRectangle();
                if (dur) { return dur->getMaxAbsFrame(); }
            }
            const auto s = activeSceneOrNull();
            return s ? s->getFrameRange().fMax : 0;
        }

        void JsLayerProxy::bringToFront()
        {
            if (mBox) {
                mBox->bringToFront();
                finishAction();
            }
        }

        void JsLayerProxy::bringToEnd()
        {
            if (mBox) {
                mBox->bringToEnd();
                finishAction();
            }
        }

        void JsLayerProxy::moveUp()
        {
            if (mBox) {
                mBox->moveUp();
                finishAction();
            }
        }

        void JsLayerProxy::moveDown()
        {
            if (mBox) {
                mBox->moveDown();
                finishAction();
            }
        }

        QJSValue JsLayerProxy::paths()
        {
            if (!mBox || !mEngine) {
                return mEngine ? mEngine->newArray() : QJSValue();
            }
            const auto svp = enve_cast<SmartVectorPath*>(mBox.data());
            auto arr = mEngine->newArray();
            if (!svp) { return arr; }
            const auto collection = svp->getPathAnimator();
            if (!collection) { return arr; }
            const int n = collection->ca_getNumberOfChildren();
            int count = 0;
            for (int i = 0; i < n; i++) {
                const auto anim = collection->ca_getChildAt<SmartPathAnimator>(i);
                if (!anim) { continue; }
                const auto proxy = new JsPathProxy(
                            QPointer<SmartPathAnimator>(anim),
                            mEngine.data(), nullptr);
                arr.setProperty(count++, mEngine->newQObject(proxy));
            }
            return arr;
        }

        //---------------------------- JsPathProxy ----------------------------

        JsPathProxy::JsPathProxy(const QPointer<SmartPathAnimator> &anim,
                                 QJSEngine * const engine,
                                 QObject * const parent)
            : QObject(parent)
            , mAnim(anim)
            , mEngine(engine)
        {}

        JsPathProxy::~JsPathProxy() = default;

        bool JsPathProxy::valid() const { return !mAnim.isNull(); }

        QJSValue JsPathProxy::pathInfo()
        {
            if (!mAnim || !mEngine) { return QJSValue(QJSValue::NullValue); }
            // operate on the currently edited path (current frame)
            const auto path = mAnim->getCurrentlyEdited();
            if (!path) { return QJSValue(QJSValue::NullValue); }

            auto obj = mEngine->newObject();
            obj.setProperty(QStringLiteral("closed"), path->isClosed());
            const int count = path->getNodeCount();
            auto nodes = mEngine->newArray(count);
            for (int i = 0; i < count; i++) {
                const auto node = path->getNodePtr(i);
                if (!node || !node->isNormal()) { continue; }
                auto nd = mEngine->newObject();
                nd.setProperty(QStringLiteral("id"), node->getNodeId());
                const QPointF p1 = node->p1();
                nd.setProperty(QStringLiteral("point"),
                               makeArray(mEngine.data(), p1.x(), p1.y()));
                const QPointF c0 = node->c0();
                nd.setProperty(QStringLiteral("inTan"),
                               makeArray(mEngine.data(),
                                         c0.x() - p1.x(), c0.y() - p1.y()));
                const QPointF c2 = node->c2();
                nd.setProperty(QStringLiteral("outTan"),
                               makeArray(mEngine.data(),
                                         c2.x() - p1.x(), c2.y() - p1.y()));
                nodes.setProperty(node->getNodeId(), nd);
            }
            obj.setProperty(QStringLiteral("nodes"), nodes);
            return obj;
        }

        void JsPathProxy::setNodeHandles(const int nodeId,
                                         const QJSValue &inTan,
                                         const QJSValue &outTan)
        {
            if (!mAnim) { return; }
            const auto path = mAnim->getCurrentlyEdited();
            if (!path) { return; }
            const auto node = path->getNodePtr(nodeId);
            if (!node || !node->isNormal()) { return; }

            qreal x = 0.;
            qreal y = 0.;
            QPointF c0 = node->c0();
            if (!inTan.isNull() && readPoint(inTan, x, y)) {
                c0 = node->p1() + QPointF(x, y);
            }
            QPointF c2 = node->c2();
            if (!outTan.isNull() && readPoint(outTan, x, y)) {
                c2 = node->p1() + QPointF(x, y);
            }
            // mirrors SmartPathAnimator action flow: start transform,
            // apply to base value, finish (undo-able)
            mAnim->prp_startTransform();
            path->actionSetNormalNodeValues(nodeId, c0, node->p1(), c2);
            mAnim->prp_finishTransform();
            mAnim->prp_afterWholeInfluenceRangeChanged();
        }

        void JsPathProxy::setNodeCtrlsMode(const int nodeId,
                                           const QString &mode)
        {
            if (!mAnim) { return; }
            CtrlsMode m;
            if (mode == QLatin1String("symmetric")) {
                m = CtrlsMode::symmetric;
            } else if (mode == QLatin1String("smooth")) {
                m = CtrlsMode::smooth;
            } else if (mode == QLatin1String("corner")) {
                m = CtrlsMode::corner;
            } else { return; }
            mAnim->actionSetNormalNodeCtrlsMode(nodeId, m);
        }

        QJSValue JsLayerProxy::motionPath()
        {
            if (!mBox || !mEngine) { return QJSValue(QJSValue::NullValue); }
            const auto transform = mBox->getBoxTransformAnimator();
            if (!transform) { return QJSValue(QJSValue::NullValue); }
            const auto pos = transform->getPosAnimator();
            if (!pos) { return QJSValue(QJSValue::NullValue); }
            const auto xAnim = pos->getXAnimator();
            const auto yAnim = pos->getYAnimator();
            if (!xAnim || !yAnim) { return QJSValue(QJSValue::NullValue); }

            const auto &keys = xAnim->anim_getKeys();
            if (keys.count() < 2) { return QJSValue(QJSValue::NullValue); }

            auto obj = mEngine->newObject();
            auto arr = mEngine->newArray(keys.count());
            int count = 0;
            for (const auto &key : keys) {
                auto gk = const_cast<GraphKey*>(
                            static_cast<GraphKey*>(key));
                const int frame = gk->getAbsFrame();
                // position value at this key (parent-space coords)
                const qreal px = xAnim->getBaseValue(frame);
                const qreal py = yAnim->getBaseValue(frame);
                auto kd = mEngine->newObject();
                kd.setProperty(QStringLiteral("frame"), frame);
                kd.setProperty(QStringLiteral("point"),
                               makeArray(mEngine.data(), px, py));
                // graph c0/c1 are (frame, value) bezier offsets in the
                // x and y animators separately; map to a canvas-space
                // tangent: [frame offset -> kept as-is, value offsets
                // combined from x and y]
                const qreal c0fx = gk->getC0Frame();
                const qreal c1fx = gk->getC1Frame();
                const qreal c0vx = xAnim->getBaseValue(frame + c0fx) - px;
                const qreal c0vy = yAnim->getBaseValue(frame + c0fx) - py;
                const qreal c1vx = xAnim->getBaseValue(frame + c1fx) - px;
                const qreal c1vy = yAnim->getBaseValue(frame + c1fx) - py;
                kd.setProperty(QStringLiteral("inTan"),
                               makeArray(mEngine.data(), c0vx, c0vy));
                kd.setProperty(QStringLiteral("outTan"),
                               makeArray(mEngine.data(), c1vx, c1vy));
                arr.setProperty(count++, kd);
            }
            obj.setProperty(QStringLiteral("keys"), arr);
            return obj;
        }

        bool JsLayerProxy::setMotionKeyTangents(const int frame,
                                                const QJSValue &inTan,
                                                const QJSValue &outTan)
        {
            if (!mBox) { return false; }
            const auto transform = mBox->getBoxTransformAnimator();
            if (!transform) { return false; }
            const auto pos = transform->getPosAnimator();
            if (!pos) { return false; }
            const auto xAnim = pos->getXAnimator();
            const auto yAnim = pos->getYAnimator();
            if (!xAnim || !yAnim) { return false; }

            const auto xKey = xAnim->template anim_getKeyAtAbsFrame<GraphKey>(frame);
            const auto yKey = yAnim->template anim_getKeyAtAbsFrame<GraphKey>(frame);
            if (!xKey || !yKey) { return false; }

            qreal x = 0.;
            qreal y = 0.;
            const bool hasIn = !inTan.isNull() && readPoint(inTan, x, y);
            const bool hasOut = !outTan.isNull() && readPoint(outTan, x, y);
            if (!hasIn && !hasOut) { return false; }

            const qreal px = xAnim->getBaseValue(frame);
            const qreal py = yAnim->getBaseValue(frame);

            xKey->startCtrlPointsValueTransform();
            yKey->startCtrlPointsValueTransform();
            // enable handles so they take effect (only when needed:
            // repeated enabling triggers redundant updates)
            if (!xKey->getC0Enabled()) { xKey->setC0Enabled(true); }
            if (!xKey->getC1Enabled()) { xKey->setC1Enabled(true); }
            if (!yKey->getC0Enabled()) { yKey->setC0Enabled(true); }
            if (!yKey->getC1Enabled()) { yKey->setC1Enabled(true); }
            if (hasIn) {
                // keep the time spread (frames), set the value part so
                // the canvas-space tangent matches (x, y)
                const qreal c0f = xKey->getC0Frame();
                xKey->setC0Value(px + inTan.property(0).toNumber());
                yKey->setC0Value(py + inTan.property(1).toNumber());
                Q_UNUSED(c0f)
            }
            if (hasOut) {
                xKey->setC1Value(px + outTan.property(0).toNumber());
                yKey->setC1Value(py + outTan.property(1).toNumber());
            }
            xKey->finishCtrlPointsValueTransform();
            yKey->finishCtrlPointsValueTransform();
            xAnim->prp_afterWholeInfluenceRangeChanged();
            yAnim->prp_afterWholeInfluenceRangeChanged();
            finishAction();
            return true;
        }

        void JsLayerProxy::remove()
        {
            if (!mBox) { return; }
            mBox->removeFromParent_k();
            finishAction();
            mBox.clear();
        }

        QJSValue JsLayerProxy::duplicate()
        {
            if (!mBox || !mEngine) { return QJSValue(QJSValue::NullValue); }
            // paste into the same parent group as the original
            // (top-level boxes paste into the scene's current group)
            auto parentGroup = mBox->getParentGroup();
            if (!parentGroup) {
                const auto scene = activeSceneOrNull();
                if (!scene) { return QJSValue(QJSValue::NullValue); }
                parentGroup = scene->getCurrentGroup();
            }
            if (!parentGroup) { return QJSValue(QJSValue::NullValue); }

            const int oldCount = parentGroup->getContainedBoxesCount();
            QList<BoundingBox*> singleList;
            singleList.append(mBox.data());
            // same flow as Canvas::duplicateAction(): serialize the
            // box into a temporary boxes clipboard and paste it back
            // into the parent group (copies content + transform, and
            // creates the undo record for the insertion)
            const auto clipboard = enve::make_shared<BoxesClipboard>(singleList);
            clipboard->pasteTo(parentGroup);
            const int newCount = parentGroup->getContainedBoxesCount();
            if (newCount <= oldCount) {
                return QJSValue(QJSValue::NullValue);
            }
            // newly pasted boxes sit at the FRONT of the contained
            // list (see BoxesClipboard::pasteTo)
            const auto &list = parentGroup->getContainedBoxes();
            BoundingBox *copy = nullptr;
            for (int i = 0; i < newCount - oldCount; i++) {
                const auto box = list.at(i);
                if (!box) { continue; }
                // pasteTo selects the pasted boxes; scripts manage
                // selection themselves (AE duplicate does not select)
                box->setSelected(false);
                if (!copy) { copy = box; }
            }
            if (!copy) { return QJSValue(QJSValue::NullValue); }
            finishAction();
            const auto proxy = new JsLayerProxy(QPointer<BoundingBox>(copy),
                                                mEngine.data(), nullptr);
            return mEngine->newQObject(proxy);
        }

        //---------------------------- JsSceneProxy ----------------------------

        JsSceneProxy::JsSceneProxy(const QPointer<Canvas> &scene,
                                   QJSEngine * const engine,
                                   QObject * const parent)
            : QObject(parent)
            , mScene(scene)
            , mEngine(engine)
        {}

        JsSceneProxy::~JsSceneProxy() = default;

        Canvas *JsSceneProxy::scene() const { return mScene.data(); }

        QString JsSceneProxy::name() const
        {
            return mScene ? mScene->prp_getName() : QString();
        }

        void JsSceneProxy::setName(const QString &name)
        {
            if (mScene) { mScene->prp_setName(name); }
        }

        int JsSceneProxy::width() const
        {
            return mScene ? mScene->getCanvasWidth() : 0;
        }

        void JsSceneProxy::setWidth(const int w)
        {
            if (mScene) { mScene->setCanvasSize(w, mScene->getCanvasHeight()); }
        }

        int JsSceneProxy::height() const
        {
            return mScene ? mScene->getCanvasHeight() : 0;
        }

        void JsSceneProxy::setHeight(const int h)
        {
            if (mScene) { mScene->setCanvasSize(mScene->getCanvasWidth(), h); }
        }

        qreal JsSceneProxy::fps() const
        {
            return mScene ? mScene->getFps() : 1.;
        }

        void JsSceneProxy::setFps(const qreal f)
        {
            if (mScene) { mScene->setFps(f); }
        }

        qreal JsSceneProxy::duration() const
        {
            if (!mScene) { return 0.; }
            return mScene->getFrameRange().fMax / fps();
        }

        void JsSceneProxy::setDuration(const qreal seconds)
        {
            if (!mScene) { return; }
            const auto range = mScene->getFrameRange();
            mScene->setFrameRange({range.fMin,
                                   qRound(seconds * fps())});
        }

        int JsSceneProxy::currentFrame() const
        {
            return mScene && Document::sInstance
                    ? Document::sInstance->getActiveSceneFrame() : 0;
        }

        void JsSceneProxy::setCurrentFrame(const int frame)
        {
            if (mScene && Document::sInstance) {
                Document::sInstance->setActiveSceneFrame(frame);
            }
        }

        qreal JsSceneProxy::currentTime() const
        {
            return currentFrame() / fps();
        }

        void JsSceneProxy::setCurrentTime(const qreal seconds)
        {
            setCurrentFrame(qRound(seconds * fps()));
        }

        int JsSceneProxy::numLayers() const
        {
            return boxLayers(mScene.data()).count();
        }

        QJSValue JsSceneProxy::wrapBox(BoundingBox * const box)
        {
            if (!box || !mEngine) { return QJSValue(QJSValue::NullValue); }
            const auto proxy = new JsLayerProxy(QPointer<BoundingBox>(box),
                                                mEngine.data(), nullptr);
            return mEngine->newQObject(proxy);
        }

        static BoundingBox *findBoxRecursive(ContainerBox * const parent, const QString &name)
        {
            if (!parent) { return nullptr; }
            for (const auto &child : parent->getContained()) {
                const auto box = enve_cast<BoundingBox*>(child.get());
                if (!box) { continue; }
                if (box->prp_getName() == name) { return box; }
                if (const auto cont = enve_cast<ContainerBox*>(box)) {
                    if (auto found = findBoxRecursive(cont, name)) {
                        return found;
                    }
                }
            }
            return nullptr;
        }

        QJSValue JsSceneProxy::layer(const QJSValue &indexOrName)
        {
            if (!mScene || !mEngine) { return QJSValue(QJSValue::NullValue); }
            const auto layers = boxLayers(mScene.data());
            if (indexOrName.isNumber()) {
                const int idx = indexOrName.toInt(); // 1-based from top
                if (idx < 1 || idx > layers.count()) {
                    return QJSValue(QJSValue::NullValue);
                }
                return wrapBox(layers.at(idx - 1));
            }
            if (indexOrName.isString()) {
                const QString name = indexOrName.toString();
                // 1. Direct top-level match
                for (const auto box : layers) {
                    if (box->prp_getName() == name) { return wrapBox(box); }
                }
                // 2. Search recursively in child containers
                if (auto recursiveBox = findBoxRecursive(mScene.data(), name)) {
                    return wrapBox(recursiveBox);
                }
                // 3. Fallback: integer string (e.g. "1")
                bool isNumeric = false;
                const int numIdx = name.toInt(&isNumeric);
                if (isNumeric && numIdx >= 1 && numIdx <= layers.count()) {
                    return wrapBox(layers.at(numIdx - 1));
                }
            }
            return QJSValue(QJSValue::NullValue);
        }

        QJSValue JsSceneProxy::layers()
        {
            if (!mScene || !mEngine) { return QJSValue(QJSValue::NullValue); }
            const auto layers = boxLayers(mScene.data());
            auto arr = mEngine->newArray(layers.count());
            for (int i = 0; i < layers.count(); i++) {
                arr.setProperty(i, wrapBox(layers.at(i)));
            }
            return arr;
        }

        QJSValue JsSceneProxy::selectedLayers()
        {
            if (!mScene || !mEngine) { return QJSValue(QJSValue::NullValue); }
            // use the canvas' own selection list (authoritative)
            QList<BoundingBox*> selected = mScene->getSelectedBoxesList();
            if (selected.isEmpty()) {
                // AE-like fallback: a selected property row (e.g.
                // Position) implies its owning layer is "selected"
                for (const auto prop : mScene->getSelectedPropsList()) {
                    const auto box = prop->getFirstAncestor<BoundingBox>();
                    if (box && !selected.contains(box)) {
                        selected.append(box);
                    }
                }
            }
            if (selected.isEmpty()) {
                selected.clear();
                collectSelectedBoxes(mScene.data(), selected);
                // diagnose: layer rows clicked but nothing selected
                if (selected.isEmpty()) {
                    for (const auto &child : mScene->getContained()) {
                        const auto box = enve_cast<BoundingBox*>(child.get());
                        if (box) {
                            qWarning() << "[script] layer"
                                       << box->prp_getName()
                                       << "isSelected"
                                       << box->isSelected();
                        }
                    }
                }
            }
            auto arr = mEngine->newArray();
            int n = 0;
            for (const auto box : selected) {
                arr.setProperty(n++, wrapBox(box));
            }
            return arr;
        }

        QJSValue JsSceneProxy::addBox(const int boxType, const QString &name)
        {
            if (!mScene || !mEngine) { return QJSValue(QJSValue::NullValue); }
            // local copy of createBoxOfNonCustomType() for the types
            // supported by the scripting API (the free function is not
            // exported in a header)
            qsptr<BoundingBox> box;
            switch (static_cast<eBoxType>(boxType)) {
                case eBoxType::rectangle:
                    box = enve::make_shared<RectangleBox>();
                    break;
                case eBoxType::circle:
                    box = enve::make_shared<Circle>();
                    break;
                case eBoxType::text:
                    box = enve::make_shared<TextBox>();
                    break;
                case eBoxType::nullObject:
                    box = enve::make_shared<NullObject>();
                    break;
                case eBoxType::group:
                    box = enve::make_shared<ContainerBox>(eBoxType::group);
                    break;
                case eBoxType::layer:
                    box = enve::make_shared<ContainerBox>(eBoxType::layer);
                    break;
                default:
                    return QJSValue(QJSValue::NullValue);
            }
            if (!box) { return QJSValue(QJSValue::NullValue); }

            mScene->getCurrentGroup()->addContained(box);
            // set the name AFTER insertion: insertContained() runs the
            // name through makeNameUniqueForDescendants() whose
            // prp_sFixName() strips non-ASCII characters (same
            // workaround as the PSD importer)
            if (!name.isEmpty()) { box->prp_setName(name); }
            finishAction();
            return wrapBox(box.get());
        }

        QJSValue JsSceneProxy::addRect(const QString &name,
                                       const qreal x, const qreal y,
                                       const qreal w, const qreal h)
        {
            const auto result = addBox(int(eBoxType::rectangle), name);
            if (result.isNull()) { return result; }
            const auto box = static_cast<RectangleBox*>(
                        qobject_cast<JsLayerProxy*>(
                            result.toQObject())->box());
            if (box) {
                box->setTopLeftPos(QPointF(x, y));
                box->setBottomRightPos(QPointF(x + w, y + h));
                if (box->getStrokeSettings()) {
                    box->getStrokeSettings()->setPaintType(PaintType::NOPAINT);
                }
            }
            return result;
        }

        QJSValue JsSceneProxy::addEllipse(const QString &name,
                                          const qreal cx, const qreal cy,
                                          const qreal radius)
        {
            const auto result = addBox(int(eBoxType::circle), name);
            if (result.isNull()) { return result; }
            const auto box = static_cast<Circle*>(
                        qobject_cast<JsLayerProxy*>(
                            result.toQObject())->box());
            if (box) {
                box->setRadius(radius);
                const auto transform = box->getTransformAnimator();
                if (transform) { transform->setRelativePos(QPointF(cx, cy)); }
                if (box->getStrokeSettings()) {
                    box->getStrokeSettings()->setPaintType(PaintType::NOPAINT);
                }
            }
            return result;
        }

        QJSValue JsSceneProxy::addText(const QString &name,
                                       const QString &text)
        {
            const auto result = addBox(int(eBoxType::text), name);
            if (result.isNull()) { return result; }
            const auto box = static_cast<TextBox*>(
                        qobject_cast<JsLayerProxy*>(
                            result.toQObject())->box());
            if (box) {
                if (Document::sInstance && !Document::sInstance->fFontFamily.isEmpty()) {
                    box->setFontFamilyAndStyle(Document::sInstance->fFontFamily,
                                               Document::sInstance->fFontStyle);
                } else {
#ifdef Q_OS_LINUX
                    box->setFontFamilyAndStyle(QStringLiteral("Noto Sans CJK JP"),
                                               SkFontStyle());
#else
                    box->setFontFamilyAndStyle(QStringLiteral("Microsoft YaHei"),
                                               SkFontStyle());
#endif
                }
                if (!text.isEmpty()) { box->setCurrentValue(text); }
            }
            return result;
        }

        QJSValue JsSceneProxy::addNull(const QString &name)
        {
            return addBox(int(eBoxType::nullObject), name);
        }

        QJSValue JsSceneProxy::addGroup(const QString &name)
        {
            return addBox(int(eBoxType::group), name);
        }

        QJSValue JsSceneProxy::addLayer(const QString &name)
        {
            return addBox(int(eBoxType::layer), name);
        }

        //---------------------------- JsProjectProxy ----------------------------

        JsProjectProxy::JsProjectProxy(JsHost * const host,
                                       QObject * const parent)
            : QObject(parent)
            , mHost(host)
        {}

        QJSValue JsProjectProxy::activeItem()
        {
            if (!mHost) { return QJSValue(QJSValue::NullValue); }
            return mHost->activeScene() ? mHost->appScene() :
                                          QJSValue(QJSValue::NullValue);
        }

        QString JsProjectProxy::file() const
        {
            return Document::sInstance ?
                        Document::sInstance->fEvFile : QString();
        }

        int JsProjectProxy::numScenes() const
        {
            return Document::sInstance ?
                        Document::sInstance->fScenes.count() : 0;
        }

        //---------------------------- JsAppProxy ----------------------------

        JsAppProxy::JsAppProxy(JsHost * const host, QObject * const parent)
            : QObject(parent)
            , mHost(host)
        {}

        QString JsAppProxy::version() const
        {
            return QStringLiteral("1.0");
        }

        QJSValue JsAppProxy::activeScene()
        {
            if (!mHost || !mHost->activeScene()) {
                return QJSValue(QJSValue::NullValue);
            }
            return mHost->appScene();
        }

        QJSValue JsAppProxy::project()
        {
            return mHost ? mHost->appProject() : QJSValue(QJSValue::NullValue);
        }

        void JsAppProxy::beginUndoGroup(const QString &name)
        {
            // the group name is not used yet (the undo set inherits
            // the name of its first record); kept for AE compatibility
            Q_UNUSED(name)
            beginUndoGroupBatch();
        }

        void JsAppProxy::endUndoGroup()
        {
            endUndoGroupBatch();
        }

        //---------------------------- JsHost ----------------------------

        JsHost::JsHost(QObject * const parent)
            : QObject(parent)
            , mEngine(std::make_unique<QJSEngine>())
        {
            installApi();
        }

        void JsHost::installApi()
        {
            const auto hostObj = mEngine->newQObject(this);
            // CppOwnership: the host must survive GC
            QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership);
            mEngine->globalObject().setProperty(QStringLiteral("__host"),
                                                hostObj);

            const auto appProxy = new JsAppProxy(this, this);
            const auto projProxy = new JsProjectProxy(this, this);
            mAppObj = mEngine->newQObject(appProxy);
            mProjectObj = mEngine->newQObject(projProxy);

            // AE-flavored globals (app / $ / print / alert / confirm)
            mEngine->evaluate(QStringLiteral(
                "this.app = __host.appObject();"
                "this.$ = { writeln: function(m) { __host.print(m); } };"
                "this.print = function(m) { __host.print(m); };"
                "this.alert = function(m) { __host.alert(m); };"
                "this.confirm = function(m) { return __host.confirm(m); };"
                "this.registerCommand = function(label, fn) {"
                "    __host.registerCommand(label, fn);"
                "};"
                "this.registerPanel = function(config) {"
                "    __host.registerPanel(config);"
                "};"));
        }

        void JsHost::setHandlers(const PrintHandler &print,
                                 const AlertHandler &alert,
                                 const ConfirmHandler &confirm)
        {
            mPrintHandler = print;
            mAlertHandler = alert;
            mConfirmHandler = confirm;
        }

        void JsHost::print(const QString &message)
        {
            if (mPrintHandler) { mPrintHandler(message); }
        }

        void JsHost::alert(const QString &message)
        {
            if (mAlertHandler) { mAlertHandler(message); }
        }

        bool JsHost::confirm(const QString &message)
        {
            return mConfirmHandler ? mConfirmHandler(message) : false;
        }

        void JsHost::registerCommand(const QString &label,
                                     const QJSValue &callable)
        {
            if (label.isEmpty() || !callable.isCallable()) { return; }
            mCommands.insert(label, callable);
        }

        void JsHost::registerPanel(const QJSValue &config)
        {
            if (!config.isObject()) { return; }
            PanelDesc desc;
            desc.title = config.property(QStringLiteral("title")).toString();
            if (desc.title.isEmpty()) { desc.title = QStringLiteral("Panel"); }
            desc.columns = config.property(QStringLiteral("columns")).toInt();
            if (desc.columns < 1) { desc.columns = 3; }
            desc.buttonMinSize = config.property(
                        QStringLiteral("buttonMinSize")).toInt();
            if (desc.buttonMinSize < 12) { desc.buttonMinSize = 24; }

            const auto readButtons = [&desc](const QJSValue &arr,
                                             const bool extra) {
                if (!arr.isArray()) { return; }
                const int n = arr.property(QStringLiteral("length")).toInt();
                for (int i = 0; i < n; i++) {
                    const auto b = arr.property(i);
                    if (!b.isObject()) { continue; }
                    PanelButton pb;
                    pb.label = b.property(QStringLiteral("label")).toString();
                    pb.tooltip = b.property(
                                QStringLiteral("tooltip")).toString();
                    pb.onClick = b.property(QStringLiteral("onClick"));
                    if (!pb.label.isEmpty() && pb.onClick.isCallable()) {
                        if (extra) { desc.extraButtons.append(pb); }
                        else { desc.buttons.append(pb); }
                    }
                }
            };
            readButtons(config.property(QStringLiteral("buttons")), false);
            readButtons(config.property(QStringLiteral("extraButtons")), true);

            // sliders: {label, id, min, max, value, decimals,
            //           onChanging(value), onChange(value)}
            const auto sliders = config.property(QStringLiteral("sliders"));
            if (sliders.isArray()) {
                const int n = sliders.property(
                            QStringLiteral("length")).toInt();
                for (int i = 0; i < n; i++) {
                    const auto s = sliders.property(i);
                    if (!s.isObject()) { continue; }
                    PanelSlider ps;
                    ps.label = s.property(QStringLiteral("label")).toString();
                    ps.id = s.property(QStringLiteral("id")).toString();
                    ps.min = s.property(QStringLiteral("min")).toNumber();
                    ps.max = s.property(QStringLiteral("max")).toNumber();
                    if (ps.max <= ps.min) { ps.min = 0.; ps.max = 100.; }
                    ps.value = s.property(QStringLiteral("value")).toNumber();
                    ps.decimals = s.property(
                                QStringLiteral("decimals")).toInt();
                    ps.onChanging = s.property(QStringLiteral("onChanging"));
                    ps.onChange = s.property(QStringLiteral("onChange"));
                    if (!ps.id.isEmpty()) { desc.sliders.append(ps); }
                }
            }

            // combos: {label, id, options:[...], index,
            //           onChange(index, text)}
            const auto combos = config.property(QStringLiteral("combos"));
            if (combos.isArray()) {
                const int n = combos.property(
                            QStringLiteral("length")).toInt();
                for (int i = 0; i < n; i++) {
                    const auto c = combos.property(i);
                    if (!c.isObject()) { continue; }
                    PanelCombo pc;
                    pc.label = c.property(QStringLiteral("label")).toString();
                    pc.id = c.property(QStringLiteral("id")).toString();
                    pc.index = c.property(QStringLiteral("index")).toInt();
                    pc.onChange = c.property(QStringLiteral("onChange"));
                    const auto opts = c.property(QStringLiteral("options"));
                    if (opts.isArray()) {
                        const int m = opts.property(
                                    QStringLiteral("length")).toInt();
                        for (int j = 0; j < m; j++) {
                            pc.options.append(opts.property(j).toString());
                        }
                    }
                    if (!pc.id.isEmpty() && !pc.options.isEmpty()) {
                        desc.combos.append(pc);
                    }
                }
            }

            desc.valid = !desc.buttons.isEmpty() ||
                         !desc.extraButtons.isEmpty() ||
                         !desc.sliders.isEmpty() || !desc.combos.isEmpty();
            mPanelDesc = desc;
        }

        void JsHost::invokePanelValue(const int kind, const QString &id,
                                      const qreal value, const QString &text)
        {
            const auto call = [this](const QJSValue &fn,
                                     const qreal v, const QString &text) {
                if (!fn.isCallable()) { return; }
                auto f = fn;
                QJSValueList args;
                args << QJSValue(v);
                if (!text.isEmpty()) { args << QJSValue(text); }
                const auto result = f.call(args);
                if (result.isError()) {
                    print(QStringLiteral("Uncaught exception at line %1: %2")
                          .arg(result.property("lineNumber").toString(),
                               result.toString()));
                }
                // close undo groups left open by a failed callback
                forceCloseUndoGroup();
            };
            for (const auto &s : mPanelDesc.sliders) {
                if (s.id != id) { continue; }
                if (kind == 0) { call(s.onChanging, value, QString()); }
                else { call(s.onChange, value, QString()); }
                return;
            }
            for (const auto &c : mPanelDesc.combos) {
                if (c.id != id) { continue; }
                call(c.onChange, value, text);
                return;
            }
        }

        void JsHost::invokePanelButton(const int index)
        {
            // grid buttons and extra buttons share one index space:
            // grid first, then extra
            const int nGrid = mPanelDesc.buttons.count();
            if (index >= 0 && index < nGrid) {
                invokeButton(mPanelDesc.buttons.at(index).onClick);
            } else if (index >= nGrid &&
                       index < nGrid + mPanelDesc.extraButtons.count()) {
                invokeButton(mPanelDesc.extraButtons.at(index - nGrid).onClick);
            }
        }

        void JsHost::invokeButton(const QJSValue &callable)
        {
            if (!callable.isCallable()) { return; }
            auto fn = callable;
            const auto result = fn.call();
            if (result.isError()) {
                print(QStringLiteral("Uncaught exception at line %1: %2")
                      .arg(result.property("lineNumber").toString(),
                           result.toString()));
            }
            // close undo groups left open by a failed callback
            forceCloseUndoGroup();
        }

        QJSValue JsHost::appObject()
        {
            return mAppObj;
        }

        QJSValue JsHost::projectObject()
        {
            return mProjectObj;
        }

        QJSValue JsHost::appProject()
        {
            return mProjectObj;
        }

        QJSValue JsHost::appScene()
        {
            const auto scene = activeSceneOrNull();
            if (!scene) {
                mSceneProxy.clear();
                mSceneObj = QJSValue(QJSValue::NullValue);
                return mSceneObj;
            }
            if (!mSceneProxy || mSceneProxy->scene() != scene) {
                const auto proxy = new JsSceneProxy(QPointer<Canvas>(scene),
                                                    mEngine.get(), this);
                mSceneProxy = proxy;
                mSceneObj = mEngine->newQObject(proxy);
            }
            return mSceneObj;
        }

        QString JsHost::loadScript(const QString &path)
        {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                return QStringLiteral("Cannot open script file");
            }
            QTextStream stream(&file);
            // scripts are UTF-8; QTextStream defaults to the locale
            // codec (GBK on Chinese Windows), which mangles non-ASCII
            // and causes JS parse errors
            stream.setCodec(QTextCodec::codecForName("UTF-8"));
            const QString source = stream.readAll();
            file.close();
            const auto result = mEngine->evaluate(source, path);
            if (result.isError()) {
                return QStringLiteral("Uncaught exception at line %1: %2")
                        .arg(result.property("lineNumber").toString(),
                             result.toString());
            }
            return QString();
        }

        QString JsHost::evaluate(const QString &source)
        {
            const auto result = mEngine->evaluate(source);
            if (result.isError()) {
                return QStringLiteral("Uncaught exception: %1")
                        .arg(result.toString());
            }
            if (result.isUndefined()) { return QStringLiteral("undefined"); }
            if (result.isString()) { return result.toString(); }
            if (result.isNumber()) { return QString::number(result.toNumber()); }
            if (result.isNull()) { return QStringLiteral("null"); }
            return result.toString();
        }

        QString JsHost::callCommand(const QString &label)
        {
            auto callable = mCommands.value(label);
            if (!callable.isCallable()) {
                return QStringLiteral("Command '%1' not found").arg(label);
            }
            const auto result = callable.call();
            if (result.isError()) {
                forceCloseUndoGroup();
                return QStringLiteral("Uncaught exception at line %1: %2")
                        .arg(result.property("lineNumber").toString(),
                             result.toString());
            }
            // close undo groups left open by a failed callback
            forceCloseUndoGroup();
            return QString();
        }

        const QStringList JsHost::commandLabels() const
        {
            return mCommands.keys();
        }

        Canvas *JsHost::activeScene() const
        {
            return activeSceneOrNull();
        }

    }
}

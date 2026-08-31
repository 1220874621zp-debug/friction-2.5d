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

// Fork of enve - Copyright (C) 2016-2020 Maurycy Liebner

#include "MovablePoints/boxpathpoint.h"
#include "qrealanimator.h"
#include "transformanimator.h"
#include "qpointfanimator.h"
#include "MovablePoints/animatedpoint.h"
#include "skia/skqtconversions.h"
#include "matrixdecomposition.h"
#include "svgexporter.h"
#include "Boxes/boundingbox.h"

#include <QtMath>

BasicTransformAnimator::BasicTransformAnimator() :
    StaticComplexAnimator("transform") {
    mPosAnimator = enve::make_shared<QPointFAnimator>("translation");
    mPosAnimator->setBaseValue(QPointF(0, 0));

    mScaleAnimator = enve::make_shared<QPointFAnimator>("scale");
    mScaleAnimator->setBaseValue(QPointF(1, 1));
    mScaleAnimator->setPrefferedValueStep(0.05);

    mRotAnimator = enve::make_shared<QrealAnimator>("rotation");
    mRotAnimator->setCurrentBaseValue(0);

    const auto events = QStringList() << "none" << "click" << "dblclick"
                                      << "mousedown" << "mouseenter" << "mouseleave"
                                      << "mousemove" << "mouseout" << "mouseover"
                                      << "mouseup";

    mSVGBeginProperty = enve::make_shared<ComboBoxProperty>("begin event", events);
    mSVGEndProperty = enve::make_shared<ComboBoxProperty>("end event", events);

    ca_addChild(mSVGBeginProperty);
    ca_addChild(mSVGEndProperty);
    ca_addChild(mPosAnimator);
    ca_addChild(mRotAnimator);
    ca_addChild(mScaleAnimator);

    connect(this, &Property::prp_currentFrameChanged,
            this, &BasicTransformAnimator::updateRelativeTransform);
    // value changes (dragged motion path keys, timeline edits) do not
    // propagate prp_currentFrameChanged up from children (only frame
    // switches do), so the cached rel/total transforms - and the
    // pivot refresh they trigger - went stale until the next frame
    // switch; listen to the animators directly
    const auto valueChanged = [this](const UpdateReason reason) {
        updateRelativeTransform(reason);
    };
    connect(mPosAnimator.get(), &Property::prp_currentFrameChanged,
            this, valueChanged);
    connect(mRotAnimator.get(), &Property::prp_currentFrameChanged,
            this, valueChanged);
    connect(mScaleAnimator.get(), &Property::prp_currentFrameChanged,
            this, valueChanged);
}

void BasicTransformAnimator::resetScale() {
    mScaleAnimator->prp_startTransform();
    mScaleAnimator->setBaseValue(QPointF(1, 1));
    mScaleAnimator->prp_finishTransform();
}

void BasicTransformAnimator::resetTranslation() {
    mPosAnimator->prp_startTransform();
    mPosAnimator->setBaseValue(QPointF(0, 0));
    mPosAnimator->prp_finishTransform();
}

void BasicTransformAnimator::resetRotation() {
    mRotAnimator->prp_startTransform();
    mRotAnimator->setCurrentBaseValue(0);
    mRotAnimator->prp_finishTransform();
}

void BasicTransformAnimator::reset() {
    resetScale();
    resetTranslation();
    resetRotation();
}

void BasicTransformAnimator::setScale(const qreal sx, const qreal sy) {
    mScaleAnimator->setBaseValue(QPointF(sx, sy));
}

void BasicTransformAnimator::setPosition(const qreal x, const qreal y) {
    mPosAnimator->setBaseValue(QPointF(x, y));
}

void BasicTransformAnimator::setRotation(const qreal rot) {
    mRotAnimator->setCurrentBaseValue(rot);
}

void BasicTransformAnimator::startRotTransform() {
    mRotAnimator->prp_startTransform();
}

void BasicTransformAnimator::startPosTransform() {
    mPosAnimator->prp_startTransform();
}

void BasicTransformAnimator::startScaleTransform() {
    mScaleAnimator->prp_startTransform();
}

void BasicTransformAnimator::rotateRelativeToSavedValue(const qreal rotRel) {
    const bool flip = rotationFlipped();
    mRotAnimator->incSavedValueToCurrentValue(flip ? -rotRel : rotRel);
}

void BasicTransformAnimator::moveRelativeToSavedValue(const qreal dX, const qreal dY) {
    mPosAnimator->incSavedValueToCurrentValue(dX, dY);
}

void BasicTransformAnimator::translate(const qreal dX, const qreal dY) {
    mPosAnimator->incBaseValues(dX, dY);
}

void BasicTransformAnimator::scale(const qreal sx, const qreal sy) {
    mScaleAnimator->multSavedValueToCurrentValue(sx, sy);
}

qreal BasicTransformAnimator::dx() {
    return mPosAnimator->getEffectiveXValue();
}

qreal BasicTransformAnimator::dy() {
    return mPosAnimator->getEffectiveYValue();
}

qreal BasicTransformAnimator::rot() {
    return mRotAnimator->getEffectiveValue();
}

qreal BasicTransformAnimator::xScale() {
    return mScaleAnimator->getEffectiveXValue();
}

qreal BasicTransformAnimator::yScale() {
    return mScaleAnimator->getEffectiveYValue();
}

QPointF BasicTransformAnimator::pos() {
    return mPosAnimator->getEffectiveValue();
}

QPointF BasicTransformAnimator::mapAbsPosToRel(const QPointF &absPos) const {
    return getTotalTransform().inverted().map(absPos);
}

QPointF BasicTransformAnimator::mapRelPosToAbs(const QPointF &relPos) const {
    return getTotalTransform().map(relPos);
}

QPointF BasicTransformAnimator::mapFromParent(const QPointF &parentRelPos) const {
    if(!mParentTransform) return parentRelPos;
    const auto absPos = mParentTransform->mapRelPosToAbs(parentRelPos);
    return mapAbsPosToRel(absPos);
}

SkPoint BasicTransformAnimator::mapAbsPosToRel(const SkPoint &absPos) const {
    return toSkPoint(mapAbsPosToRel(toQPointF(absPos)));
}

SkPoint BasicTransformAnimator::mapRelPosToAbs(const SkPoint &relPos) const {
    return toSkPoint(mapRelPosToAbs(toQPointF(relPos)));
}

SkPoint BasicTransformAnimator::mapFromParent(const SkPoint &parentRelPos) const {
    return toSkPoint(mapFromParent(toQPointF(parentRelPos)));
}

QMatrix BasicTransformAnimator::getRelativeTransformAtFrame(
        const qreal relFrame, QMatrix* postTransform) const {
    Q_UNUSED(postTransform)
    QMatrix matrix;
    matrix.translate(mPosAnimator->getEffectiveXValue(relFrame),
                     mPosAnimator->getEffectiveYValue(relFrame));

    matrix.rotate(mRotAnimator->getEffectiveValue(relFrame));
    matrix.scale(mScaleAnimator->getEffectiveXValue(relFrame),
                 mScaleAnimator->getEffectiveYValue(relFrame));
    return matrix;
}

void BasicTransformAnimator::moveByAbs(const QPointF &absTrans) {
    const auto savedRelPos = mPosAnimator->getSavedValue();
    const auto transform = mPostTransform*mInheritedTransform;
    const auto savedAbsPos = transform.map(savedRelPos);
    const auto relPos = transform.inverted().map(savedAbsPos + absTrans);
    setRelativePos(relPos);
}

void BasicTransformAnimator::setRelativePos(const QPointF &relPos) {
    mPosAnimator->setBaseValue(relPos);
}

void BasicTransformAnimator::rotateRelativeToSavedValue(const qreal rotRel,
                                                        const QPointF &pivot) {
    QMatrix matrix;
    matrix.translate(pivot.x(), pivot.y());
    matrix.rotate(rotRel);
    matrix.translate(-pivot.x() + mPosAnimator->getSavedXValue(),
                     -pivot.y() + mPosAnimator->getSavedYValue());
    const bool flip = rotationFlipped();
    rotateRelativeToSavedValue(flip ? -rotRel : rotRel);
    mPosAnimator->setBaseValue(QPointF(matrix.dx(), matrix.dy()));
}

void BasicTransformAnimator::updateRelativeTransform(const UpdateReason reason) {
    mRelTransform = getRelativeTransformAtFrame(anim_getCurrentRelFrame(),
                                                &mPostTransform);
    updateTotalTransform(reason);
}

void BasicTransformAnimator::updateInheritedTransform(const UpdateReason reason) {
    if(mParentTransform) {
        mInheritedTransform = mParentTransform->getTotalTransform();
    } else {
        mInheritedTransform.reset();
    }
    updateTotalTransform(reason);
    emit inheritedTransformChanged(reason);
}

void BasicTransformAnimator::updateTotalTransform(const UpdateReason reason) {
    if(mParentTransform) {
        mTotalTransform = mRelTransform*mInheritedTransform;
    } else {
        mTotalTransform = mRelTransform;
    }
    emit totalTransformChanged(reason);
}

void BasicTransformAnimator::setSVGEventsVisibility(const bool visible)
{
    mSVGBeginProperty->SWT_setVisible(visible);
    mSVGEndProperty->SWT_setVisible(visible);
}

bool BasicTransformAnimator::rotationFlipped() const
{
    return mInheritedTransform.determinant() < 0.0;
}

const QMatrix &BasicTransformAnimator::getInheritedTransform() const {
    return mInheritedTransform;
}

const QMatrix &BasicTransformAnimator::getTotalTransform() const {
    return mTotalTransform;
}

const QMatrix &BasicTransformAnimator::getRelativeTransform() const {
    return mRelTransform;
}

BasicTransformAnimator *BasicTransformAnimator::getParentTransformAnimator() const {
    return mParentTransform;
}

void BasicTransformAnimator::setParentTransformAnimator(
        BasicTransformAnimator* parent) {
    auto& conn = mParentTransform.assign(parent);
    if(parent) {
        conn << connect(parent, &BasicTransformAnimator::totalTransformChanged,
                        this, &BasicTransformAnimator::updateInheritedTransform);
    }
    updateInheritedTransform(UpdateReason::userChange);
}

void BasicTransformAnimator::scaleRelativeToSavedValue(const qreal sx,
                                                       const qreal sy,
                                                      const QPointF &pivot) {
    QMatrix matrix;
    matrix.translate(pivot.x(), pivot.y());
    matrix.rotate(mRotAnimator->getEffectiveValue());
    matrix.scale(sx, sy);
    matrix.rotate(-mRotAnimator->getEffectiveValue());
    matrix.translate(-pivot.x() + mPosAnimator->getSavedXValue(),
                     -pivot.y() + mPosAnimator->getSavedYValue());

    scale(sx, sy);
    mPosAnimator->setBaseValue(QPointF(matrix.dx(), matrix.dy()));
}

QPointFAnimator *BasicTransformAnimator::getPosAnimator() const {
    return mPosAnimator.get();
}

QPointFAnimator *BasicTransformAnimator::getScaleAnimator() const {
    return mScaleAnimator.get();
}

QrealAnimator *BasicTransformAnimator::getRotAnimator() const {
    return mRotAnimator.get();
}

QMatrix BasicTransformAnimator::getInheritedTransformAtFrame(
        const qreal relFrame) const {
    if(mParentTransform) {
        const qreal absFrame = prp_relFrameToAbsFrameF(relFrame);
        const qreal parentRelFrame =
                mParentTransform->prp_absFrameToRelFrameF(absFrame);
        return mParentTransform->getTotalTransformAtFrame(parentRelFrame);
    } else {
        return QMatrix();
    }
}

QMatrix BasicTransformAnimator::getTotalTransformAtFrame(
        const qreal relFrame) const {
    if(mParentTransform) {
        const qreal absFrame = prp_relFrameToAbsFrameF(relFrame);
        const qreal parentRelFrame =
                mParentTransform->prp_absFrameToRelFrameF(absFrame);
        return getRelativeTransformAtFrame(relFrame)*
                mParentTransform->getTotalTransformAtFrame(parentRelFrame);
    } else {
        return getRelativeTransformAtFrame(relFrame);
    }
}

FrameRange BasicTransformAnimator::prp_getIdenticalRelRange(const int relFrame) const {
    if(mParentTransform) {
        const auto thisIdent = ComplexAnimator::prp_getIdenticalRelRange(relFrame);
        const int absFrame = prp_relFrameToAbsFrame(relFrame);
        const int pRelFrame = mParentTransform->prp_absFrameToRelFrame(absFrame);
        const auto parentIdent = mParentTransform->prp_getIdenticalRelRange(pRelFrame);
        const auto absParentIdent = mParentTransform->prp_relRangeToAbsRange(parentIdent);
        return thisIdent*prp_absRangeToRelRange(absParentIdent);
    } else return ComplexAnimator::prp_getIdenticalRelRange(relFrame);
}

AdvancedTransformAnimator::AdvancedTransformAnimator() {
    mShearAnimator = enve::make_shared<QPointFAnimator>("shear");
    mShearAnimator->setBaseValue(QPointF(0, 0));
    mShearAnimator->setValuesRange(-100, 100);
    mShearAnimator->setPrefferedValueStep(0.1);

    mPivotAnimator = enve::make_shared<QPointFAnimator>("pivot");
    mPivotAnimator->setBaseValue(QPointF(0, 0));

    mOpacityAnimator = enve::make_shared<QrealAnimator>("opacity");
    mOpacityAnimator->setValueRange(0, 100);
    mOpacityAnimator->setPrefferedValueStep(5);
    mOpacityAnimator->setCurrentBaseValue(100);
    mOpacityAnimator->graphFixMinMaxValues();

    // 2.5D billboard plane
    mRotXAnimator = enve::make_shared<QrealAnimator>("3D rotation X");
    mRotXAnimator->setCurrentBaseValue(0);
    mRotXAnimator->setPrefferedValueStep(1);

    mRotYAnimator = enve::make_shared<QrealAnimator>("3D rotation Y");
    mRotYAnimator->setCurrentBaseValue(0);
    mRotYAnimator->setPrefferedValueStep(1);

    mZPosAnimator = enve::make_shared<QrealAnimator>("3D position Z");
    mZPosAnimator->setCurrentBaseValue(0);
    mZPosAnimator->setPrefferedValueStep(1);

    mPerspectiveAnimator = enve::make_shared<QrealAnimator>("3D perspective");
    mPerspectiveAnimator->setValueRange(1, 100000);
    mPerspectiveAnimator->setCurrentBaseValue(800);
    mPerspectiveAnimator->setPrefferedValueStep(10);

    ca_addChild(mShearAnimator);
    ca_addChild(mPivotAnimator);
    ca_addChild(mOpacityAnimator);
    ca_addChild(mRotXAnimator);
    ca_addChild(mRotYAnimator);
    ca_addChild(mZPosAnimator);
    ca_addChild(mPerspectiveAnimator);
}

void AdvancedTransformAnimator::resetShear() {
    mShearAnimator->setBaseValue(QPointF(0, 0));
}

void AdvancedTransformAnimator::resetPivot() {
    mPivotAnimator->setBaseValue(QPointF(0, 0));
}

void AdvancedTransformAnimator::resetRotScaleShear() {
    resetRotation();
    resetScale();
    resetShear();
}

void AdvancedTransformAnimator::reset() {
    BasicTransformAnimator::reset();
    resetShear();
    resetPivot();
}

void AdvancedTransformAnimator::startOpacityTransform() {
    mOpacityAnimator->prp_startTransform();
}

void AdvancedTransformAnimator::setOpacity(const qreal newOpacity) {
    mOpacityAnimator->setCurrentBaseValue(newOpacity);
}

void AdvancedTransformAnimator::setPivot(const qreal x, const qreal y) {
    mPivotAnimator->setBaseValue(QPointF(x, y));
}

void AdvancedTransformAnimator::startPivotTransform() {
    if(!mPosAnimator->anim_isDescendantRecording())
        mPosAnimator->prp_startTransform();
    mPivotAnimator->prp_startTransform();
}

void AdvancedTransformAnimator::finishPivotTransform() {
    if(!mPosAnimator->anim_isDescendantRecording())
        mPosAnimator->prp_finishTransform();
    mPivotAnimator->prp_finishTransform();
}

void AdvancedTransformAnimator::setPivotFixedTransform(
        const QPointF &newPivot) {
    TransformValues oldTransfom;
    oldTransfom.fPivotX = mPivotAnimator->getEffectiveXValue();
    oldTransfom.fPivotY = mPivotAnimator->getEffectiveYValue();
    oldTransfom.fMoveX = mPosAnimator->getEffectiveXValue();
    oldTransfom.fMoveY = mPosAnimator->getEffectiveYValue();
    oldTransfom.fRotation = mRotAnimator->getEffectiveValue();
    oldTransfom.fScaleX = mScaleAnimator->getEffectiveXValue();
    oldTransfom.fScaleY = mScaleAnimator->getEffectiveYValue();
    oldTransfom.fShearX = mShearAnimator->getEffectiveXValue();
    oldTransfom.fShearY = mShearAnimator->getEffectiveYValue();
    const auto newTransform = MatrixDecomposition::
            setPivotKeepTransform(oldTransfom, newPivot);

    const qreal posXInc = newTransform.fMoveX - oldTransfom.fMoveX;
    const qreal posYInc = newTransform.fMoveY - oldTransfom.fMoveY;
    const bool posAnimated = mPosAnimator->anim_isDescendantRecording();
    const bool pivotAnimated = mPivotAnimator->anim_isDescendantRecording();
    if(pivotAnimated) {
        mPivotAnimator->setBaseValue(newPivot);
    } else if(posAnimated && !pivotAnimated) {
        mPosAnimator->incAllBaseValues(posXInc, posYInc);
        mPivotAnimator->setBaseValueWithoutCallingUpdater(newPivot);
    } else { // if(!posAnimated && !pivotAnimated) {
        mPosAnimator->incBaseValuesWithoutCallingUpdater(posXInc, posYInc);
        mPivotAnimator->setBaseValueWithoutCallingUpdater(newPivot);
    }
}

QPointF AdvancedTransformAnimator::getPivot(const qreal relFrame) {
    return mPivotAnimator->getEffectiveValue(relFrame);
}

QPointF AdvancedTransformAnimator::getPivot() {
    return mPivotAnimator->getEffectiveValue();
}

QPointF AdvancedTransformAnimator::getPivotAbs(const qreal relFrame) {
    return mapRelPosToAbs(mPivotAnimator->getEffectiveValue(relFrame));
}

QPointF AdvancedTransformAnimator::getPivotAbs() {
    return mapRelPosToAbs(mPivotAnimator->getEffectiveValue());
}

qreal AdvancedTransformAnimator::getOpacity(const qreal relFrame) {
    return mOpacityAnimator->getEffectiveValue(relFrame);
}

bool AdvancedTransformAnimator::posOrPivotRecording() const {
    return mPosAnimator->anim_isDescendantRecording() ||
           mPivotAnimator->anim_isDescendantRecording();
}

bool AdvancedTransformAnimator::rotOrScaleOrPivotRecording() const {
    return mRotAnimator->anim_isDescendantRecording() ||
           mScaleAnimator->anim_isDescendantRecording() ||
           mPivotAnimator->anim_isDescendantRecording();
}

qreal AdvancedTransformAnimator::getPivotX() {
    return mPivotAnimator->getEffectiveXValue();
}

qreal AdvancedTransformAnimator::getPivotY() {
    return mPivotAnimator->getEffectiveYValue();
}

void AdvancedTransformAnimator::startRotScaleShearTransform() {
    startRotTransform();
    startScaleTransform();
    startShearTransform();
}

void AdvancedTransformAnimator::startShearTransform() {
    mShearAnimator->prp_startTransform();
}

void AdvancedTransformAnimator::setShear(const qreal shearX,
                                         const qreal shearY)
{
    mShearAnimator->setBaseValue(shearX, shearY);
}

void AdvancedTransformAnimator::shear(const qreal shearXBy,
                                      const qreal shearYBy)
{
    mShearAnimator->incSavedValueToCurrentValue(shearXBy, shearYBy);
}

void AdvancedTransformAnimator::shearRelativeToSavedValue(const qreal shearXBy,
                                                          const qreal shearYBy,
                                                          const QPointF &pivot)
{
    QMatrix matrix;
    matrix.translate(pivot.x(), pivot.y());
    matrix.rotate(mRotAnimator->getEffectiveValue());
    matrix.scale(mScaleAnimator->getEffectiveXValue(), mScaleAnimator->getEffectiveYValue());
    matrix.shear(shearXBy, shearYBy);
    matrix.rotate(-mRotAnimator->getEffectiveValue());
    matrix.translate(-pivot.x() + mPosAnimator->getSavedXValue(),
                     -pivot.y() + mPosAnimator->getSavedYValue());

    shear(shearXBy, shearYBy);
    mPosAnimator->setBaseValue(QPointF(matrix.dx(), matrix.dy()));
}

qreal AdvancedTransformAnimator::getOpacity() {
    return mOpacityAnimator->getEffectiveValue();
}

bool AdvancedTransformAnimator::has3DTransformAtFrame(
        const qreal relFrame) const {
    if(!m3DEnabled) return false;
    if(qAbs(mRotXAnimator->getEffectiveValue(relFrame)) > 0.001) return true;
    if(qAbs(mRotYAnimator->getEffectiveValue(relFrame)) > 0.001) return true;
    if(qAbs(mZPosAnimator->getEffectiveValue(relFrame)) > 0.001) return true;
    return false;
}

qreal AdvancedTransformAnimator::get3DZPosAtFrame(
        const qreal relFrame) const {
    return mZPosAnimator->getEffectiveValue(relFrame);
}

void AdvancedTransformAnimator::prp_readProperty_impl(eReadStream& src) {
    BasicTransformAnimator::prp_readProperty_impl(src);
    // layers default to 2D now; if the saved transform carries 3D values,
    // re-enable 2.5D so the layer renders as it was saved
    if(!m3DEnabled && has3DTransformAtFrame(anim_getCurrentRelFrame())) {
        m3DEnabled = true;
        set3DPropertiesVisible(true);
        emit box3DChanged();
    }
}

void AdvancedTransformAnimator::start3DZTransform() {
    mZPosAnimator->prp_startTransform();
}

void AdvancedTransformAnimator::move3DZRelativeToSavedValue(
        const qreal dZ) {
    mZPosAnimator->incSavedValueToCurrentValue(dZ);
}

void AdvancedTransformAnimator::startRotXTransform() {
    mRotXAnimator->prp_startTransform();
}

void AdvancedTransformAnimator::startRotYTransform() {
    mRotYAnimator->prp_startTransform();
}

void AdvancedTransformAnimator::rotXRelativeToSavedValue(
        const qreal deg) {
    mRotXAnimator->incSavedValueToCurrentValue(deg);
}

void AdvancedTransformAnimator::rotYRelativeToSavedValue(
        const qreal deg) {
    mRotYAnimator->incSavedValueToCurrentValue(deg);
}

SkMatrix AdvancedTransformAnimator::get3DTransformAtFrame(
        const qreal relFrame) const {
    SkMatrix result;
    if(!has3DTransformAtFrame(relFrame)) return result;

    const qreal persp = mPerspectiveAnimator->getEffectiveValue(relFrame);
    const qreal f = persp > 1. ? persp : 800.;
    const qreal rx = qDegreesToRadians(
                mRotXAnimator->getEffectiveValue(relFrame));
    const qreal ry = qDegreesToRadians(
                mRotYAnimator->getEffectiveValue(relFrame));
    const qreal zPos = mZPosAnimator->getEffectiveValue(relFrame);
    const qreal pivotX = mPivotAnimator->getEffectiveXValue(relFrame);
    const qreal pivotY = mPivotAnimator->getEffectiveYValue(relFrame);

    const qreal cx = std::cos(rx); const qreal sx = std::sin(rx);
    const qreal cy = std::cos(ry); const qreal sy = std::sin(ry);

    // plane point (x, y, 0) rotated by Rx*Ry, then shifted by zPos:
    //   vx = cy*x
    //   vy = sx*sy*x + cx*y
    //   vz = -cx*sy*x + sx*y + zPos
    // perspective projection with focal length f:
    //   x' = f*vx / (f + vz),  y' = f*vy / (f + vz)   (zPos > 0 = farther)
    // expressed as a 3x3 homography (applied on pivot-centered coords):
    SkMatrix h;
    h.setAll(toSkScalar(f*cy),      toSkScalar(0.),        toSkScalar(0.),
             toSkScalar(f*sx*sy),   toSkScalar(f*cx),      toSkScalar(0.),
             toSkScalar(-cx*sy),    toSkScalar(sx),        toSkScalar(f + zPos));

    SkMatrix pre;  pre.setTranslate(toSkScalar(-pivotX), toSkScalar(-pivotY));
    SkMatrix post; post.setTranslate(toSkScalar(pivotX),  toSkScalar(pivotY));

    // point order: translate(-pivot) -> project -> translate(+pivot)
    // SkMatrix preConcat(m): this = this * m (m applied first)
    result = post;
    result.preConcat(h);
    result.preConcat(pre);
    return result;
}

SkMatrix AdvancedTransformAnimator::getTotalTransform3D() const {
    const qreal relFrame = anim_getCurrentRelFrame();
    // point order: 3D (around own pivot, layer space) -> rel -> inherited
    // 3D must run BEFORE the 2D rel transform so the rotation is
    // centered on the layer's own pivot (AE anchor semantics)
    SkMatrix result = toSkMatrix(getInheritedTransform());
    result.preConcat(toSkMatrix(getRelativeTransform()));
    if(has3DTransformAtFrame(relFrame)) {
        result.preConcat(get3DTransformAtFrame(relFrame));
    }
    return result;
}

void AdvancedTransformAnimator::reset3D() {
    mRotXAnimator->prp_startTransform();
    mRotXAnimator->setCurrentBaseValue(0);
    mRotXAnimator->prp_finishTransform();

    mRotYAnimator->prp_startTransform();
    mRotYAnimator->setCurrentBaseValue(0);
    mRotYAnimator->prp_finishTransform();

    mZPosAnimator->prp_startTransform();
    mZPosAnimator->setCurrentBaseValue(0);
    mZPosAnimator->prp_finishTransform();
}

void AdvancedTransformAnimator::set3DPropertiesVisible(const bool visible) {
    mRotXAnimator->SWT_setVisible(visible);
    mRotYAnimator->SWT_setVisible(visible);
    mZPosAnimator->SWT_setVisible(visible);
    mPerspectiveAnimator->SWT_setVisible(visible);
}

void AdvancedTransformAnimator::set3DEnabled(const bool enabled) {
    if(m3DEnabled == enabled) return;
    // AE-style toggle: disabling keeps the 3D values (they are simply not
    // applied while off) so re-enabling restores the user's adjustments;
    // use reset3D() explicitly to zero the values
    m3DEnabled = enabled;
    set3DPropertiesVisible(enabled);
    emit box3DChanged();
    prp_afterWholeInfluenceRangeChanged();
}

void AdvancedTransformAnimator::startTransformSkipOpacity() {
    startPosTransform();
    startPivotTransform();
    startRotTransform();
    startScaleTransform();
    startShearTransform();
}

QMatrix valuesToMatrix(const qreal pivotX, const qreal pivotY,
                       const qreal posX, const qreal posY,
                       const qreal rot,
                       const qreal scaleX, const qreal scaleY,
                       const qreal shearX, const qreal shearY) {
    QMatrix matrix;
    matrix.translate(pivotX + posX, pivotY + posY);
    matrix.rotate(rot);
    matrix.scale(scaleX, scaleY);
    matrix.shear(shearX, shearY);
    matrix.translate(-pivotX, -pivotY);

    return matrix;
}

void AdvancedTransformAnimator::applyTransformEffects(
        const qreal relFrame,
        qreal& pivotX, qreal& pivotY,
        qreal& posX, qreal& posY,
        qreal& rot,
        qreal& scaleX, qreal& scaleY,
        qreal& shearX, qreal& shearY,
        QMatrix& postTransform) const {
    const auto parent = getFirstAncestor<BoundingBox>();
    if(!parent) return;
    parent->applyTransformEffects(relFrame,
                                  pivotX, pivotY,
                                  posX, posY,
                                  rot,
                                  scaleX, scaleY,
                                  shearX, shearY,
                                  postTransform);
}

void AdvancedTransformAnimator::setValues(const TransformValues &values) {
    setPivot(values.fPivotX, values.fPivotY);
    setPosition(values.fMoveX, values.fMoveY);
    setScale(values.fScaleX, values.fScaleY);
    setRotation(values.fRotation);
    setShear(values.fShearX, values.fShearY);
}

QMatrix AdvancedTransformAnimator::getRotScaleShearTransform() {
    qreal pivotX = mPivotAnimator->getEffectiveXValue();
    qreal pivotY = mPivotAnimator->getEffectiveYValue();

    qreal posX = 0.;
    qreal posY = 0.;

    qreal rot = mRotAnimator->getEffectiveValue();

    qreal scaleX = mScaleAnimator->getEffectiveXValue();
    qreal scaleY = mScaleAnimator->getEffectiveYValue();

    qreal shearX = mShearAnimator->getEffectiveXValue();
    qreal shearY = mShearAnimator->getEffectiveYValue();


    QMatrix matrix = valuesToMatrix(pivotX, pivotY,
                                    posX, posY,
                                    rot,
                                    scaleX, scaleY,
                                    shearX, shearY);

    return matrix;
}

QMatrix AdvancedTransformAnimator::getRelativeTransformAtFrame(
        const qreal relFrame, QMatrix* postTransform) const {
    qreal pivotX = mPivotAnimator->getEffectiveXValue(relFrame);
    qreal pivotY = mPivotAnimator->getEffectiveYValue(relFrame);

    qreal posX = mPosAnimator->getEffectiveXValue(relFrame);
    qreal posY = mPosAnimator->getEffectiveYValue(relFrame);

    qreal rot = mRotAnimator->getEffectiveValue(relFrame);

    qreal scaleX = mScaleAnimator->getEffectiveXValue(relFrame);
    qreal scaleY = mScaleAnimator->getEffectiveYValue(relFrame);

    qreal shearX = mShearAnimator->getEffectiveXValue(relFrame);
    qreal shearY = mShearAnimator->getEffectiveYValue(relFrame);

    QMatrix postTransformT;

    applyTransformEffects(relFrame,
                          pivotX, pivotY,
                          posX, posY,
                          rot,
                          scaleX, scaleY,
                          shearX, shearY,
                          postTransformT);
    if(postTransform) *postTransform = postTransformT;

    const auto matrix = valuesToMatrix(pivotX, pivotY,
                                       posX, posY,
                                       rot,
                                       scaleX, scaleY,
                                       shearX, shearY);

    return matrix*postTransformT;
}

BoxTransformAnimator::BoxTransformAnimator() {
    setPointsHandler(enve::make_shared<PointsHandler>());
    const auto pivotPt = enve::make_shared<BoxPathPoint>(
                getPivotAnimator(), this);
    getPointsHandler()->appendPt(pivotPt);
}

const QString BoxTransformAnimator::getSVGPropertyAction(const int value)
{
    switch(value) {
    case 1: return "click";
    case 2: return "dblclick";
    case 3: return "mousedown";
    case 4: return "mouseenter";
    case 5: return "mouseleave";
    case 6: return "mousemove";
    case 7: return "mouseout";
    case 8: return "mouseover";
    case 9: return "mouseup";
    default:;
    }
    return QString();
}

QDomElement saveSVG_Split(QPointFAnimator* const anim,
                          const FrameRange& visRange,
                          const qreal multiplier,
                          const qreal def,
                          const QString& type,
                          SvgExporter& exp,
                          const QDomElement& child,
                          const QString& beginEvent,
                          const QString& endEvent)
{
    const auto animX = anim->getXAnimator();
    const auto animY = anim->getYAnimator();

    const bool xStatic = !animX->anim_hasKeys() &&
                         !animX->hasExpression();
    const bool yStatic = !animY->anim_hasKeys() &&
                         !animY->hasExpression();

    if(xStatic || yStatic) {
        auto unpivot = exp.createElement("g");
        if(yStatic) {
            const qreal y = multiplier*animY->getEffectiveValue();
            anim->saveQPointFSVGX(exp, unpivot, visRange, "transform", y,
                                  multiplier, true, type,
                                  beginEvent, endEvent);
        } else {
            const qreal x = multiplier*animX->getEffectiveValue();
            anim->saveQPointFSVGY(exp, unpivot, visRange, "transform", x,
                                  multiplier, true, type,
                                  beginEvent, endEvent);
        }
        unpivot.appendChild(child);
        return unpivot;
    } else {
        auto xEle = exp.createElement("g");
        anim->saveQPointFSVGX(exp, xEle, visRange, "transform", def,
                              multiplier, true, type,
                              beginEvent, endEvent);
        auto yEle = exp.createElement("g");
        anim->saveQPointFSVGY(exp, yEle, visRange, "transform", def,
                              multiplier, true, type,
                              beginEvent, endEvent);

        yEle.appendChild(child);
        xEle.appendChild(yEle);
        return xEle;
    }
}

QDomElement BoxTransformAnimator::saveSVG(SvgExporter& exp,
                                          const FrameRange& visRange,
                                          const QDomElement& child) const
{

    const auto beginEvent = getSVGPropertyAction(mSVGBeginProperty->getCurrentValue());
    const auto endEvent = getSVGPropertyAction(mSVGEndProperty->getCurrentValue());

    auto unpivot = saveSVG_Split(getPivotAnimator(), visRange, -1, 0,
                                 "translate", exp, child,
                                 beginEvent, endEvent);
    {
        const auto opaAnim = getOpacityAnimator();
        opaAnim->saveQrealSVG(exp, unpivot, visRange,
                              "opacity", 0.01, false, "", "%1",
                              beginEvent, endEvent);
    }

    auto shear = exp.createElement("g");
    {
        const auto shearAnim = getShearAnimator();
        const auto shearXAnim = shearAnim->getXAnimator();
        const auto shearYAnim = shearAnim->getYAnimator();
        const bool shearXStatic = !shearXAnim->anim_hasKeys() && !shearXAnim->hasExpression();
        const bool shearYStatic = !shearYAnim->anim_hasKeys() && !shearYAnim->hasExpression();
        if (shearXStatic || shearYStatic) {
            shearXAnim->saveQrealSVG(exp, shear, visRange, "transform", 45, true, "skewX",
                                     "%1", beginEvent, endEvent);
            shearYAnim->saveQrealSVG(exp, shear, visRange, "transform", 45, true, "skewY",
                                     "%1", beginEvent, endEvent);
            shear.appendChild(unpivot);
        } else {
            shearXAnim->saveQrealSVG(exp, shear, visRange, "transform", 45, true, "skewX",
                                     "%1", beginEvent, endEvent);
            auto shearY = exp.createElement("g");
            shearYAnim->saveQrealSVG(exp, shearY, visRange, "transform", 45, true, "skewY",
                                     "%1", beginEvent, endEvent);
            shearY.appendChild(unpivot);
            shear.appendChild(shearY);
        }
    }
    const auto scale = saveSVG_Split(getScaleAnimator(), visRange, 1, 1,
                                     "scale", exp, shear,
                                     beginEvent, endEvent);

    auto rotate = exp.createElement("g");
    {
        getRotAnimator()->saveQrealSVG(exp, rotate, visRange,
                                       "transform", 1, true, "rotate",
                                       "%1", beginEvent, endEvent);
        rotate.appendChild(scale);
    }
    const auto translate = saveSVG_Split(getPosAnimator(), visRange, 1, 0,
                                         "translate", exp, rotate,
                                         beginEvent, endEvent);
    auto pivot = saveSVG_Split(getPivotAnimator(), visRange, 1, 0,
                               "translate", exp, translate,
                               beginEvent, endEvent);

    return pivot;
}

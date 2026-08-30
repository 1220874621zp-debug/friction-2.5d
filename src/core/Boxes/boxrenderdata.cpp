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

#include "boxrenderdata.h"
#include "RasterEffects/trackmattecaller.h"
#include "Boxes/adjustmentlayer.h"
#include "boundingbox.h"
#include "skia/skiahelpers.h"
#include "efiltersettings.h"
#include "Private/Tasks/taskscheduler.h"
#include "Private/Tasks/gputaskexecutor.h"

BoxRenderData::BoxRenderData(BoundingBox * const parent) :
    fFilterQuality(eFilterSettings::sRender()) {
    fParentBox = parent;
    fBlendEffectIdentifier = parent;
}

SkMatrix BoxRenderData::getFullRenderTransform() const {
    if(!fHasPerspective && !fHasSceneCamera) {
        return toSkMatrix(fScaledTransform);
    }
    // point order: 3D perspective (layer space, around own pivot)
    //              -> rel -> inherited -> scene camera (world space)
    //              -> resolution
    // SkMatrix preConcat(m): this = this * m (m applied first)
    SkMatrix result = toSkMatrix(fResolutionScale);
    if(fHasSceneCamera) result.preConcat(fSceneCameraT);
    result.preConcat(toSkMatrix(fInheritedTransform));
    result.preConcat(toSkMatrix(fRelTransform));
    result.preConcat(fPerspectiveTransform);
    return result;
}

void BoxRenderData::transformRenderCanvas(SkCanvas &canvas) const {
    canvas.translate(toSkScalar(-fGlobalRect.x()),
                     toSkScalar(-fGlobalRect.y()));
    canvas.concat(getFullRenderTransform());
}

void BoxRenderData::copyFrom(BoxRenderData *src) {
    mCopySource = src;
    fRelTransform = src->fRelTransform;
    fInheritedTransform = src->fInheritedTransform;
    fTotalTransform = src->fTotalTransform;
    fScaledTransform = src->fScaledTransform;
    fPerspectiveTransform = src->fPerspectiveTransform;
    fHasPerspective = src->fHasPerspective;
    fSceneCameraT = src->fSceneCameraT;
    fHasSceneCamera = src->fHasSceneCamera;
    fRelFrame = src->fRelFrame;
    fRelBoundingRect = src->fRelBoundingRect;
    fRenderTransform = src->fRenderTransform;
    fAntiAlias = src->fAntiAlias;
    fUseRenderTransform = src->fUseRenderTransform;
    fBlendMode = src->fBlendMode;
    fGlobalRect = src->fGlobalRect;
    fOpacity = src->fOpacity;
    fResolution = src->fResolution;
    fResolutionScale = src->fResolutionScale;
    fRenderedImage = src->requestImageCopy();
    fBoxStateId = src->fBoxStateId;
    mState = eTaskState::finished;
    fRelBoundingRectSet = true;
}

stdsptr<BoxRenderData> BoxRenderData::makeCopy() {
    if(!fParentBox) return nullptr;
    const auto copy = fParentBox->createRenderData();
    copy->copyFrom(this);
    return copy;
}

sk_sp<SkImage> BoxRenderData::requestImageCopy() {
    if(mImageCopies.isEmpty()) return SkiaHelpers::makeCopy(fRenderedImage);
    else return mImageCopies.takeLast();
}

void BoxRenderData::drawOnParentLayer(SkCanvas * const canvas) {
    SkPaint paint;
    if(fUseRenderTransform) paint.setFilterQuality(fFilterQuality);
    drawOnParentLayer(canvas, paint);
}

void BoxRenderData::drawOnParentLayer(SkCanvas * const canvas,
                                      SkPaint& paint) {
    // backdrop-sampling effects run before anything else so the layer's
    // own (semi-transparent) content composites over the treated
    // backdrop; runs even at zero opacity since the distortion is not
    // part of the layer image
    if(!fBackdropCallers.isEmpty()) {
        adjustmentApplyBackdrop(canvas, fBackdropCallers);
    }
    if(isZero4Dec(fOpacity) || !fRenderedImage) return;
    if(fUseRenderTransform) canvas->concat(toSkMatrix(fRenderTransform));
    if(fBlendMode == SkBlendMode::kDstIn ||
       fBlendMode == SkBlendMode::kSrcIn ||
       fBlendMode == SkBlendMode::kDstATop ||
       fBlendMode == SkBlendMode::kModulate ||
       fBlendMode == SkBlendMode::kSrcOut) {
        canvas->save();
        auto rect = SkRect::MakeXYWH(fGlobalRect.x(), fGlobalRect.y(),
                                     fRenderedImage->width(),
                                     fRenderedImage->height());
        rect.inset(1, 1);
        canvas->clipRect(rect, SkClipOp::kDifference, false);
        canvas->clear(SK_ColorTRANSPARENT);
        canvas->restore();
    }
    paint.setAlpha(static_cast<U8CPU>(qRound(fOpacity*2.55)));
    paint.setBlendMode(fBlendMode);
    paint.setAntiAlias(fAntiAlias);
    canvas->drawImage(fRenderedImage, fGlobalRect.x(), fGlobalRect.y(), &paint);
}

void BoxRenderData::processGpu(QGL33 * const gl,
                               SwitchableContext &context) {
    if(mStep == Step::EFFECTS)
        return mEffectsRenderer.processGpu(gl, context, this);
    updateGlobalRect();
    if(isZero4Dec(fOpacity)) return;
    if(fGlobalRect.width() <= 0 || fGlobalRect.height() <= 0) return;

    context.switchToSkia();
    const auto grContext = context.grContext();

    const auto grTex = grContext->createBackendTexture(
                fGlobalRect.width(), fGlobalRect.height(),
                kRGBA_8888_SkColorType, GrMipMapped::kNo,
                GrRenderable::kYes);
    if(!grTex.isValid()) return;
    const auto surf = SkSurface::MakeFromBackendTexture(
                grContext, grTex, kTopLeft_GrSurfaceOrigin, 0,
                kRGBA_8888_SkColorType, nullptr, nullptr);

    const auto canvas = surf->getCanvas();
    transformRenderCanvas(*canvas);
    canvas->clear(eraseColor());
    drawSk(canvas);
    canvas->flush();
    fRenderedImage = SkImage::MakeFromAdoptedTexture(grContext, grTex,
                                                     kTopLeft_GrSurfaceOrigin,
                                                     kRGBA_8888_SkColorType);
    if(mEffectsRenderer.isEmpty() ||
       mEffectsRenderer.nextHardwareSupport() == HardwareSupport::cpuOnly)
        fRenderedImage = fRenderedImage->makeRasterImage();
    else mEffectsRenderer.processGpu(gl, context, this);
//    if(mEffectsRenderer.isEmpty()) return;
//    const auto nextEffectHw = mEffectsRenderer.nextHardwareSupport();
//    if(nextEffectHw != HardwareSupport::cpuOnly) {
//        mEffectsRenderer.processGpu(gl, context, this);
//    }
}

void BoxRenderData::process()
{
    if (mStep == Step::EFFECTS) { return; }
    updateGlobalRect();

    if (isZero4Dec(fOpacity)) { return; }
    if (fGlobalRect.width() <= 0 || fGlobalRect.height() <= 0) { return; }

    const int tLimit = 32768;
    if (fGlobalRect.width() > tLimit || fGlobalRect.height() > tLimit) {
        return;
    }

    const auto info = SkiaHelpers::getPremulRGBAInfo(fGlobalRect.width(),
                                                     fGlobalRect.height());

    mBitmap.allocPixels(info);

    if (mBitmap.getPixels() == nullptr) { return; }

    mBitmap.eraseColor(eraseColor());
    SkCanvas canvas(mBitmap);
    transformRenderCanvas(canvas);

    drawSk(&canvas);

    fRenderedImage = SkiaHelpers::transferDataToSkImage(mBitmap);
}

void BoxRenderData::beforeProcessing(const Hardware hw) {
    Q_UNUSED(hw)
    Q_ASSERT(mStep != Step::EFFECTS);
    setupRenderData();
    if(!mDataSet) dataSet();
    if(isZero4Dec(fOpacity)) finishedProcessing();
}

void BoxRenderData::afterProcessing() {
    if(fMotionBlurTarget) {
        fMotionBlurTarget->fOtherGlobalRects << fGlobalRect;
    }
    if(fParentBox && fParentIsTarget) {
        fParentBox->renderDataFinished(this);
    } else if(mCopySource) {
        mCopySource->addImageCopy(std::move(fRenderedImage));
    }
}

void BoxRenderData::afterQued() {
    if(mDataSet) return;
    if(!mDelayDataSet) dataSet();
}

#include "Private/esettings.h"

HardwareSupport BoxRenderData::hardwareSupport() const {
    if(mStep == Step::EFFECTS) {
        return mEffectsRenderer.nextHardwareSupport();
    } else {
        if(fParentBox) return fParentBox->hardwareSupport();
        return HardwareSupport::cpuPreffered;
    }
}

void BoxRenderData::queTaskNow() {
    TaskScheduler::instance()->queCpuTask(ref<eTask>());
}

bool BoxRenderData::nextStep() {
    const bool result = !mEffectsRenderer.isEmpty() &&
                        fRenderedImage;
    if(result) {
        mStep = Step::EFFECTS;
        if(hardwareSupport() == HardwareSupport::cpuOnly) {
            mEffectsRenderer.processCpu(this);
        } else {
            GpuTaskExecutor::sAddTask(ref<eTask>());
        }
    }
    return result;
}

void BoxRenderData::dataSet() {
    if(mDataSet) return;
    mDataSet = true;
    if(!fRelBoundingRectSet) {
        fRelBoundingRectSet = true;
        updateRelBoundingRect();
    }
    if(!fParentBox || !fParentIsTarget) return;
    fParentBox->updateCurrentPreviewDataFromRenderData(this);
}

#include "Boxes/textboxrenderdata.h"
void BoxRenderData::updateGlobalRect() {
    fScaledTransform = fTotalTransform*fResolutionScale;
    QRectF baseRectF;
    if(fHasPerspective) {
        // perspective: map bounding rect corners and take the AABB
        const auto full = getFullRenderTransform();
        const auto& r = fRelBoundingRect;
        const SkPoint corners[4] = {
            {toSkScalar(r.left()),  toSkScalar(r.top())},
            {toSkScalar(r.right()), toSkScalar(r.top())},
            {toSkScalar(r.right()), toSkScalar(r.bottom())},
            {toSkScalar(r.left()),  toSkScalar(r.bottom())}
        };
        SkPoint mapped[4];
        full.mapPoints(mapped, corners, 4);
        qreal minX = mapped[0].x(); qreal minY = mapped[0].y();
        qreal maxX = minX; qreal maxY = minY;
        for(int i = 1; i < 4; i++) {
            minX = qMin(minX, qreal(mapped[i].x()));
            minY = qMin(minY, qreal(mapped[i].y()));
            maxX = qMax(maxX, qreal(mapped[i].x()));
            maxY = qMax(maxY, qreal(mapped[i].y()));
        }
        baseRectF = QRectF(minX, minY, maxX - minX, maxY - minY);
    } else {
        baseRectF = fScaledTransform.mapRect(fRelBoundingRect);
    }
    for(const QRectF &rectT : fOtherGlobalRects) {
        baseRectF = baseRectF.united(rectT);
    }
    baseRectF.adjust(-fBaseMargin.left(), -fBaseMargin.top(),
                     fBaseMargin.right(), fBaseMargin.bottom());
    setBaseGlobalRect(baseRectF);
}

void BoxRenderData::setBaseGlobalRect(const QRectF& baseRectF)
{
    const auto clampedBaseRect = baseRectF.intersected(fMaxBoundsRect);
    SkIRect currRect = toSkRect(clampedBaseRect).roundOut();

    const int tLimit = 32768;
    currRect.fLeft = qBound(-tLimit, currRect.fLeft, tLimit);
    currRect.fTop = qBound(-tLimit, currRect.fTop, tLimit);
    currRect.fRight = qBound(-tLimit, currRect.fRight, tLimit);
    currRect.fBottom = qBound(-tLimit, currRect.fBottom, tLimit);

    if (!mEffectsRenderer.isEmpty()) {
        const SkIRect skMaxBounds = toSkIRect(fMaxBoundsRect);
        mEffectsRenderer.setBaseGlobalRect(currRect, skMaxBounds);
    }
    fGlobalRect = toQRect(currRect);
}


void BoxRenderData::setTrackMatte(stdsptr<BoxRenderData> sample,
                                  const int mode) {
    if(!sample || mode <= 0) return;
    fTrackMatteSample = sample;
    fTrackMatteMode = mode;
    // appended after the layer's own effects: the matte masks the
    // final image (blur etc. applied first)
    addEffect(enve::make_shared<TrackMatteCaller>(
                  fTrackMatteSample,
                  static_cast<TrackMatteCaller::Mode>(fTrackMatteMode)));
}

/*
#
# Friction - https://friction.graphics
#
# Copyright (c) Ole-André Rodlie and contributors
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, version 3.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
*/

#include "effectcanvaspoint.h"

#include "rastereffect.h"
#include "Boxes/boundingbox.h"
#include "Animators/qpointfanimator.h"
#include "Animators/transformanimator.h"
#include "skia/skqtconversions.h"
#include "appsupport.h"

namespace {
bool sPointsVisibleInit = false;
bool sPointsVisible = true;
}

bool EffectCanvasPoint::pointsVisible()
{
    if (!sPointsVisibleInit) {
        sPointsVisible = AppSupport::getSettings(
                    QStringLiteral("view"),
                    QStringLiteral("effectPointControls"),
                    true).toBool();
        sPointsVisibleInit = true;
    }
    return sPointsVisible;
}

void EffectCanvasPoint::setPointsVisible(const bool visible)
{
    sPointsVisibleInit = true;
    sPointsVisible = visible;
    AppSupport::setSettings(QStringLiteral("view"),
                            QStringLiteral("effectPointControls"),
                            visible);
}

EffectCanvasPoint::EffectCanvasPoint(QPointFAnimator * const animator,
                                     RasterEffect * const effect,
                                     const Space space) :
    MovablePoint(MovablePointType::TYPE_GRADIENT_POINT),
    mEffect(effect), mAnimator(animator), mSpace(space)
{
    setRadius(12);
    setSelectionEnabled(false);
}

BoundingBox *EffectCanvasPoint::resolveHostBox() const
{
    const auto eff = mEffect.data();
    if (!eff) return nullptr;
    return eff->getFirstAncestor<BoundingBox>();
}

QRectF EffectCanvasPoint::hostRect() const
{
    const auto box = resolveHostBox();
    if (!box) return QRectF();
    return box->getRelBoundingRect();
}

QPointF EffectCanvasPoint::getRelativePos() const
{
    const auto anim = mAnimator.data();
    if (!anim) return QPointF();
    const QRectF r = hostRect();
    const QPointF v = anim->getEffectiveValue();
    if (mSpace == Space::Normalized) {
        return QPointF(r.left() + v.x() * r.width(),
                       r.top() + v.y() * r.height());
    }
    return r.center() + v;
}

void EffectCanvasPoint::setRelativePos(const QPointF &relPos)
{
    const auto anim = mAnimator.data();
    if (!anim) return;
    const QRectF r = hostRect();
    if (mSpace == Space::Normalized) {
        const qreal w = qFuzzyIsNull(r.width()) ? 0. : (relPos.x() - r.left()) / r.width();
        const qreal h = qFuzzyIsNull(r.height()) ? 0. : (relPos.y() - r.top()) / r.height();
        anim->setBaseValue(QPointF(w, h));
    } else {
        anim->setBaseValue(relPos - r.center());
    }
}

void EffectCanvasPoint::startTransform()
{
    MovablePoint::startTransform();
    const auto anim = mAnimator.data();
    if (anim) anim->prp_startTransform();
}

void EffectCanvasPoint::finishTransform()
{
    const auto anim = mAnimator.data();
    if (anim) anim->prp_finishTransform();
}

void EffectCanvasPoint::cancelTransform()
{
    const auto anim = mAnimator.data();
    if (anim) anim->prp_cancelTransform();
}

bool EffectCanvasPoint::isVisible(const CanvasMode mode) const
{
    if (!pointsVisible()) return false;
    return mode == CanvasMode::pointTransform ||
           mode == CanvasMode::boxTransform;
}

void EffectCanvasPoint::drawSk(SkCanvas * const canvas,
                               const CanvasMode mode,
                               const float invScale,
                               const bool keyOnCurrent,
                               const bool ctrlPressed)
{
    Q_UNUSED(mode)
    Q_UNUSED(ctrlPressed)

    const auto box = resolveHostBox();
    if (!box) return;

    // Lazy-sync the host box transform so hit-testing maps the
    // same way as drawing (the effect is constructed before it
    // knows its host box).
    if (getTransform() != box->getTransformAnimator()) {
        setTransform(box->getTransformAnimator());
    }

    const SkPoint absPos = toSkPoint(getAbsolutePos());
    const float r = 10.f * invScale;
    const float cross = 18.f * invScale;

    // offset-space handles draw a faint guide from the content
    // center so the displacement direction reads at a glance
    if (mSpace == Space::Offset) {
        const QPointF relCenter = hostRect().center();
        const QPointF absCenter = getTransform() ?
                    getTransform()->mapRelPosToAbs(relCenter) : relCenter;
        SkPaint guide;
        guide.setAntiAlias(true);
        guide.setColor(SkColorSetARGB(140, 0, 220, 255));
        guide.setStyle(SkPaint::kStroke_Style);
        guide.setStrokeWidth(1.f * invScale);
        const float intervals[2] = {4.f * invScale, 4.f * invScale};
        guide.setPathEffect(SkDashPathEffect::Make(intervals, 2, 0.f));
        canvas->drawLine(toSkPoint(absCenter), absPos, guide);
    }

    SkPaint pShadow;
    pShadow.setAntiAlias(true);
    pShadow.setColor(SkColorSetARGB(180, 0, 0, 0));
    pShadow.setStyle(SkPaint::kStroke_Style);
    pShadow.setStrokeWidth(3.f * invScale);

    SkPaint pLine;
    pLine.setAntiAlias(true);
    pLine.setColor(SkColorSetARGB(255, 0, 220, 255));
    pLine.setStyle(SkPaint::kStroke_Style);
    pLine.setStrokeWidth(1.5f * invScale);

    canvas->drawCircle(absPos.x(), absPos.y(), r, pShadow);
    canvas->drawCircle(absPos.x(), absPos.y(), r, pLine);

    // keyframe indicator: red dot inside the ring (current frame
    // holds a key), same convention as gradient points
    if (keyOnCurrent) {
        SkPaint pKey;
        pKey.setAntiAlias(true);
        pKey.setColor(SK_ColorRED);
        pKey.setStyle(SkPaint::kFill_Style);
        canvas->drawCircle(absPos.x(), absPos.y(), r * 0.5f, pKey);
    } else {
        SkPaint pDot;
        pDot.setAntiAlias(true);
        pDot.setColor(SK_ColorWHITE);
        pDot.setStyle(SkPaint::kFill_Style);
        canvas->drawCircle(absPos.x(), absPos.y(), 2.5f * invScale, pDot);
    }

    canvas->drawLine(absPos.x() - cross, absPos.y(), absPos.x() - r * 0.5f, absPos.y(), pShadow);
    canvas->drawLine(absPos.x() + r * 0.5f, absPos.y(), absPos.x() + cross, absPos.y(), pShadow);
    canvas->drawLine(absPos.x(), absPos.y() - cross, absPos.x(), absPos.y() - r * 0.5f, pShadow);
    canvas->drawLine(absPos.x(), absPos.y() + r * 0.5f, absPos.x(), absPos.y() + cross, pShadow);

    canvas->drawLine(absPos.x() - cross, absPos.y(), absPos.x() - r * 0.5f, absPos.y(), pLine);
    canvas->drawLine(absPos.x() + r * 0.5f, absPos.y(), absPos.x() + cross, absPos.y(), pLine);
    canvas->drawLine(absPos.x(), absPos.y() - cross, absPos.x(), absPos.y() - r * 0.5f, pLine);
    canvas->drawLine(absPos.x(), absPos.y() + r * 0.5f, absPos.x(), absPos.y() + cross, pLine);
}

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

// Faithful port of the user's BWF After Effects plugin (BWF.cpp).
// Pipeline: Threshold > SDF > ContourDP > NormalEmission > RadialBlur.
// Every stage below mirrors the AE source function-for-function; the
// math runs in the normalized 0..1 domain like the AE 16-bit path.

#include "blackwhiteflasheffect.h"
#include "Animators/qrealanimator.h"
#include "Animators/coloranimator.h"
#include "Boxes/boundingbox.h"
#include "MovablePoints/movablepoint.h"
#include "MovablePoints/pointshandler.h"
#include "skia/skqtconversions.h"
#include "appsupport.h"
#include <cmath>
#include <vector>
#include <algorithm>

namespace {

inline double clampF(const double val, const double minVal, const double maxVal)
{
    if (val < minVal) return minVal;
    if (val > maxVal) return maxVal;
    return val;
}

inline int clampL(const int val, const int minVal, const int maxVal)
{
    if (val < minVal) return minVal;
    if (val > maxVal) return maxVal;
    return val;
}

inline double lerpF(const double a, const double b, const double t)
{
    return a + (b - a) * t;
}

struct BwfPoint {
    int x;
    int y;
    BwfPoint() : x(0), y(0) {}
    BwfPoint(const int _x, const int _y) : x(_x), y(_y) {}
};

double sampleLumBilinear(const std::vector<double>& buf,
                         const double x, const double y,
                         const int w, const int h)
{
    const double fx = clampF(x, 0.0, double(w - 1));
    const double fy = clampF(y, 0.0, double(h - 1));
    const int ix0 = int(std::floor(fx));
    const int iy0 = int(std::floor(fy));
    const int ix1 = clampL(ix0 + 1, 0, w - 1);
    const int iy1 = clampL(iy0 + 1, 0, h - 1);
    const double tx = fx - double(ix0);
    const double ty = fy - double(iy0);
    const double v00 = buf[size_t(iy0) * w + ix0];
    const double v10 = buf[size_t(iy0) * w + ix1];
    const double v01 = buf[size_t(iy1) * w + ix0];
    const double v11 = buf[size_t(iy1) * w + ix1];
    return lerpF(lerpF(v00, v10, tx), lerpF(v01, v11, tx), ty);
}

double pointLineDist(const BwfPoint& p, const BwfPoint& a, const BwfPoint& b)
{
    const double dx = double(b.x - a.x);
    const double dy = double(b.y - a.y);
    const double lenSq = dx * dx + dy * dy;
    if (lenSq < 0.001) {
        const double dpx = double(p.x - a.x);
        const double dpy = double(p.y - a.y);
        return std::sqrt(dpx * dpx + dpy * dpy);
    }
    double t = (double(p.x - a.x) * dx + double(p.y - a.y) * dy) / lenSq;
    t = clampF(t, 0.0, 1.0);
    const double projX = double(a.x) + t * dx;
    const double projY = double(a.y) + t * dy;
    const double dpx = double(p.x) - projX;
    const double dpy = double(p.y) - projY;
    return std::sqrt(dpx * dpx + dpy * dpy);
}

void douglasPeuckerRecursive(std::vector<BwfPoint>& pts, const int start, const int end,
                             const double epsilon, std::vector<bool>& keep)
{
    if (end - start <= 1) return;
    double maxDist = 0.0;
    int maxIdx = start;
    for (int i = start + 1; i < end; i++) {
        const double d = pointLineDist(pts[size_t(i)], pts[size_t(start)], pts[size_t(end)]);
        if (d > maxDist) {
            maxDist = d;
            maxIdx = i;
        }
    }
    if (maxDist > epsilon) {
        keep[size_t(maxIdx)] = true;
        douglasPeuckerRecursive(pts, start, maxIdx, epsilon, keep);
        douglasPeuckerRecursive(pts, maxIdx, end, epsilon, keep);
    }
}

void douglasPeucker(std::vector<BwfPoint>& pts, const double epsilon)
{
    if (pts.size() <= 2) return;
    std::vector<bool> keep(pts.size(), false);
    keep[0] = true;
    keep[pts.size() - 1] = true;
    douglasPeuckerRecursive(pts, 0, int(pts.size()) - 1, epsilon, keep);
    std::vector<BwfPoint> result;
    result.reserve(pts.size());
    for (size_t i = 0; i < pts.size(); i++) {
        if (keep[i]) result.push_back(pts[i]);
    }
    pts = std::move(result);
}

// Stage 1: luminance threshold, AE stage1Threshold16 (normalized domain)
void stage1Threshold(const SkBitmap& input,
                     std::vector<double>& buf,
                     const double threshold01, const double contrast,
                     const int w, const int h)
{
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const uchar* px = static_cast<const uchar*>(input.getAddr(x, y));
            const double lum = (0.299 * px[0] + 0.587 * px[1] + 0.114 * px[2]) / 255.0;
            double cl = (lum - 0.5) * contrast + 0.5;
            cl = clampF(cl, 0.0, 1.0);
            buf[size_t(y) * w + x] = (cl > threshold01) ? 1.0 : 0.0;
        }
    }
}

bool isEdge(const std::vector<double>& binary,
            const int x, const int y, const int w, const int h)
{
    const double v = binary[size_t(y) * w + x];
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dy == 0) continue;
            const int nx = x + dx;
            const int ny = y + dy;
            if (nx < 0 || nx >= w || ny < 0 || ny >= h) continue;
            if (binary[size_t(ny) * w + nx] != v) return true;
        }
    }
    return false;
}

// Stage 2: signed distance field, AE stage2ComputeSDF verbatim
// (two 5x5 chamfer sweeps, white +INF / black -INF / edge 0)
void stage2ComputeSDF(const std::vector<double>& binary,
                      std::vector<double>& sdf,
                      const int w, const int h)
{
    const double INF = double(w + h) * 2.0;
    sdf.resize(size_t(w) * h);

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            if (isEdge(binary, x, y, w, h)) {
                sdf[size_t(y) * w + x] = 0.0;
            } else if (binary[size_t(y) * w + x] > 0.5) {
                sdf[size_t(y) * w + x] = INF;
            } else {
                sdf[size_t(y) * w + x] = -INF;
            }
        }
    }

    for (int y = 2; y < h - 2; y++) {
        for (int x = 2; x < w - 2; x++) {
            const double v = sdf[size_t(y) * w + x];
            const double absV = std::abs(v);
            if (absV < 0.001) continue;

            double best = absV;
            for (int dy = -2; dy <= 2; dy++) {
                for (int dx = -2; dx <= 2; dx++) {
                    if (dx == 0 && dy == 0) continue;
                    const double nv = sdf[size_t(y + dy) * w + (x + dx)];
                    double nd;
                    if (nv >= 0.0) {
                        nd = nv + std::sqrt(double(dx * dx + dy * dy));
                    } else {
                        nd = nv - std::sqrt(double(dx * dx + dy * dy));
                    }
                    double cand = (v >= 0.0) ? std::abs(nd) : -std::abs(nd);
                    cand = (v >= 0.0) ? std::max(cand, 0.0) : std::min(cand, 0.0);
                    const double absCand = std::abs(cand);
                    if (absCand < best) {
                        best = absCand;
                        sdf[size_t(y) * w + x] = cand;
                    }
                }
            }
        }
    }

    for (int y = h - 3; y >= 2; y--) {
        for (int x = w - 3; x >= 2; x--) {
            const double v = sdf[size_t(y) * w + x];
            const double absV = std::abs(v);
            if (absV < 0.001) continue;

            double best = absV;
            for (int dy = -2; dy <= 2; dy++) {
                for (int dx = -2; dx <= 2; dx++) {
                    if (dx == 0 && dy == 0) continue;
                    const double nv = sdf[size_t(y + dy) * w + (x + dx)];
                    double nd;
                    if (nv >= 0.0) {
                        nd = nv + std::sqrt(double(dx * dx + dy * dy));
                    } else {
                        nd = nv - std::sqrt(double(dx * dx + dy * dy));
                    }
                    double cand = (v >= 0.0) ? std::abs(nd) : -std::abs(nd);
                    cand = (v >= 0.0) ? std::max(cand, 0.0) : std::min(cand, 0.0);
                    const double absCand = std::abs(cand);
                    if (absCand < best) {
                        best = absCand;
                        sdf[size_t(y) * w + x] = cand;
                    }
                }
            }
        }
    }
}

// AE computeSDFGradient: central differences with clamped taps
void computeSDFGradient(const std::vector<double>& sdf,
                        const int x, const int y, const int w, const int h,
                        double& gx, double& gy)
{
    const double xm1 = sdf[size_t(y) * w + clampL(x - 1, 0, w - 1)];
    const double xp1 = sdf[size_t(y) * w + clampL(x + 1, 0, w - 1)];
    const double ym1 = sdf[size_t(clampL(y - 1, 0, h - 1)) * w + x];
    const double yp1 = sdf[size_t(clampL(y + 1, 0, h - 1)) * w + x];
    gx = (xp1 - xm1) * 0.5;
    gy = (yp1 - ym1) * 0.5;
}

// Stage 3a: AE traceContour (stack DFS over edge pixels)
void traceContour(const std::vector<double>& binary,
                  std::vector<unsigned char>& visited,
                  const int w, const int h,
                  const int startX, const int startY,
                  std::vector<BwfPoint>& contour)
{
    std::vector<BwfPoint> stack;
    stack.reserve(size_t(w) * h);
    stack.push_back(BwfPoint(startX, startY));
    visited[size_t(startY) * w + startX] = 1;

    contour.reserve(4096);
    while (!stack.empty()) {
        const BwfPoint p = stack.back();
        stack.pop_back();
        contour.push_back(p);
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                if (dx == 0 && dy == 0) continue;
                const int nx = p.x + dx;
                const int ny = p.y + dy;
                if (nx < 1 || nx >= w - 1 || ny < 1 || ny >= h - 1) continue;
                const int nidx = ny * w + nx;
                if (visited[size_t(nidx)]) continue;
                if (isEdge(binary, nx, ny, w, h)) {
                    visited[size_t(nidx)] = 1;
                    stack.push_back(BwfPoint(nx, ny));
                }
            }
        }
    }
}

// Stage 3: AE extractSimplifiedContours (white-side contours + Douglas-Peucker)
void extractSimplifiedContours(const std::vector<double>& binary,
                               const double simplifyTolerance,
                               const int w, const int h,
                               std::vector<BwfPoint>& outContourPoints)
{
    outContourPoints.clear();

    std::vector<unsigned char> visited(size_t(w) * h, 0);

    for (int y = 2; y < h - 2; y++) {
        for (int x = 2; x < w - 2; x++) {
            const int idx = y * w + x;
            if (visited[size_t(idx)]) continue;
            if (!isEdge(binary, x, y, w, h)) continue;
            if (binary[size_t(idx)] < 0.5) continue;

            std::vector<BwfPoint> contour;
            traceContour(binary, visited, w, h, x, y, contour);

            if (contour.size() < 3) {
                for (const auto& pt : contour) outContourPoints.push_back(pt);
                continue;
            }

            if (simplifyTolerance > 0.5) {
                douglasPeucker(contour, simplifyTolerance);
            }

            for (const auto& pt : contour) outContourPoints.push_back(pt);
        }
    }
}

// Stage 4: AE stage4NormalEmission (SDF-normal light stamps, max blend)
void stage4NormalEmission(const std::vector<double>& sdf,
                          std::vector<double>& lightMap,
                          const std::vector<BwfPoint>& contourPoints,
                          int sampleSpacing,
                          const double lightIntensity, const double lightLength,
                          const double edgeIntensity,
                          const int w, const int h)
{
    if (lightIntensity < 0.001 || contourPoints.empty()) return;
    if (sampleSpacing < 1) sampleSpacing = 1;

    const double stepLen = 2.0;
    int maxSteps = int(lightLength / stepLen) + 1;
    maxSteps = clampL(maxSteps, 1, 300);

    for (size_t i = 0; i < contourPoints.size(); i += size_t(sampleSpacing)) {
        const BwfPoint& cp = contourPoints[i];

        double gx = 0.0, gy = 0.0;
        computeSDFGradient(sdf, cp.x, cp.y, w, h, gx, gy);
        const double gradMag = std::sqrt(gx * gx + gy * gy);
        if (gradMag < 0.0001) continue;

        double nx = -gx / gradMag;
        double ny = -gy / gradMag;

        if (sdf[size_t(cp.y) * w + cp.x] < 0.0) {
            nx = -nx;
            ny = -ny;
        }

        const double baseIntensity = lightIntensity * edgeIntensity;

        for (int s = 1; s <= maxSteps; s++) {
            const double dist = double(s) * stepLen;
            const double px = double(cp.x) + nx * dist;
            const double py = double(cp.y) + ny * dist;
            const int ix = int(std::round(px));
            const int iy = int(std::round(py));
            if (ix < 0 || ix >= w || iy < 0 || iy >= h) break;

            double falloff = 1.0 - dist / (lightLength + 1.0);
            if (falloff <= 0.0) break;
            falloff = falloff * falloff;

            const double thickness = 1.0 + (1.0 - falloff) * 2.0;
            const int tR = int(std::ceil(thickness));
            const double intensity = falloff * baseIntensity;

            for (int dy = -tR; dy <= tR; dy++) {
                for (int dx = -tR; dx <= tR; dx++) {
                    const int sx = ix + dx;
                    const int sy = iy + dy;
                    if (sx < 0 || sx >= w || sy < 0 || sy >= h) continue;
                    const double d = std::sqrt(double(dx * dx + dy * dy));
                    if (d > thickness) continue;
                    const double soft = 1.0 - d / (thickness + 0.001);
                    const double val = intensity * soft * soft;
                    const size_t sidx = size_t(sy) * w + sx;
                    if (val > lightMap[sidx]) lightMap[sidx] = val;
                }
            }
        }
    }
}

// Stage 5: AE stage5RadialBlur (triangle-weighted radial samples, bilinear taps)
void stage5RadialBlur(const std::vector<double>& src,
                      std::vector<double>& dst,
                      const double bcx, const double bcy,
                      const double blurStrength, const int blurQuality,
                      const int w, const int h)
{
    if (blurStrength < 0.001) {
        dst = src;
        return;
    }

    const int samples = blurQuality * 2 + 1;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const double dx = double(x) - bcx;
            const double dy = double(y) - bcy;
            double pixelDist = std::sqrt(dx * dx + dy * dy);
            if (pixelDist < 0.001) pixelDist = 0.001;

            const double dirX = dx / pixelDist;
            const double dirY = dy / pixelDist;

            double blurRange = blurStrength * pixelDist * 0.5;
            if (blurRange < 1.0) blurRange = 1.0;

            double acc = 0.0;
            double weightSum = 0.0;
            const int half = (samples - 1) / 2;

            for (int i = -half; i <= half; i++) {
                const double t = double(i) / double(half);
                const double offset = t * blurRange;
                const double sx = double(x) + dirX * offset;
                const double sy = double(y) + dirY * offset;
                const double sw = 1.0 - std::abs(t);
                acc += sampleLumBilinear(src, sx, sy, w, h) * sw;
                weightSum += sw;
            }

            if (weightSum > 0.0) acc /= weightSum;
            dst[size_t(y) * w + x] = acc;
        }
    }
}

} // namespace

// Draggable canvas handle for the light/blur center. Writes the two
// center QrealAnimators; their values are pixels relative to the host
// box's bounding-rect top-left (the AE layer-space semantics), the
// point works in box-local coordinates like the pivot point does.
class BwfCenterPoint : public MovablePoint {
    e_OBJECT
protected:
    BwfCenterPoint(BlackWhiteFlashEffect * const effect) :
        MovablePoint(MovablePointType::TYPE_GRADIENT_POINT), mEffect(effect) {
        setRadius(12);
        setSelectionEnabled(false);
    }
public:
    bool isVisible(const CanvasMode mode) const {
        Q_UNUSED(mode)
        return true;
    }

    QPointF getRelativePos() const {
        const auto eff = mEffect.data();
        if (!eff) return QPointF();
        const auto box = eff->getFirstAncestor<BoundingBox>();
        const QRectF bRect = box ? box->getRelBoundingRect() : QRectF();
        return QPointF(bRect.left() + eff->mCenterX->getEffectiveValue(),
                       bRect.top() + eff->mCenterY->getEffectiveValue());
    }

    void setRelativePos(const QPointF &relPos) {
        const auto eff = mEffect.data();
        if (!eff) return;
        const auto box = eff->getFirstAncestor<BoundingBox>();
        if (!box) return;
        const QRectF bRect = box->getRelBoundingRect();
        eff->mCenterX->setCurrentBaseValue(relPos.x() - bRect.left());
        eff->mCenterY->setCurrentBaseValue(relPos.y() - bRect.top());
    }

    void startTransform() {
        MovablePoint::startTransform();
        const auto eff = mEffect.data();
        if (!eff) return;
        eff->mCenterX->prp_startTransform();
        eff->mCenterY->prp_startTransform();
    }

    void finishTransform() {
        const auto eff = mEffect.data();
        if (!eff) return;
        eff->mCenterX->prp_finishTransform();
        eff->mCenterY->prp_finishTransform();
    }

    void cancelTransform() {
        const auto eff = mEffect.data();
        if (!eff) return;
        eff->mCenterX->prp_cancelTransform();
        eff->mCenterY->prp_cancelTransform();
    }

    void drawSk(SkCanvas * const canvas,
                const CanvasMode mode,
                const float invScale,
                const bool keyOnCurrent,
                const bool ctrlPressed) {
        Q_UNUSED(mode)
        Q_UNUSED(keyOnCurrent)
        Q_UNUSED(ctrlPressed)

        const auto eff = mEffect.data();
        if (!eff) return;

        // Lazy-sync the host box transform so hit-testing maps the
        // same way as drawing (the effect is constructed before it
        // knows its host box).
        const auto box = eff->getFirstAncestor<BoundingBox>();
        if (!box) return;
        if (getTransform() != box->getTransformAnimator()) {
            setTransform(box->getTransformAnimator());
        }

        const SkPoint absPos = toSkPoint(getAbsolutePos());
        const float r = 10.f * invScale;
        const float cross = 18.f * invScale;

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

        SkPaint pDot;
        pDot.setAntiAlias(true);
        pDot.setColor(SkColorSetARGB(255, 255, 255, 255));
        pDot.setStyle(SkPaint::kFill_Style);
        canvas->drawCircle(absPos.x(), absPos.y(), 2.5f * invScale, pDot);

        canvas->drawLine(absPos.x() - cross, absPos.y(), absPos.x() - r * 0.5f, absPos.y(), pShadow);
        canvas->drawLine(absPos.x() + r * 0.5f, absPos.y(), absPos.x() + cross, absPos.y(), pShadow);
        canvas->drawLine(absPos.x(), absPos.y() - cross, absPos.x(), absPos.y() - r * 0.5f, pShadow);
        canvas->drawLine(absPos.x(), absPos.y() + r * 0.5f, absPos.x(), absPos.y() + cross, pShadow);

        canvas->drawLine(absPos.x() - cross, absPos.y(), absPos.x() - r * 0.5f, absPos.y(), pLine);
        canvas->drawLine(absPos.x() + r * 0.5f, absPos.y(), absPos.x() + cross, absPos.y(), pLine);
        canvas->drawLine(absPos.x(), absPos.y() - cross, absPos.x(), absPos.y() - r * 0.5f, pLine);
        canvas->drawLine(absPos.x(), absPos.y() + r * 0.5f, absPos.x(), absPos.y() + cross, pLine);
    }
private:
    const QPointer<BlackWhiteFlashEffect> mEffect;
};

BlackWhiteFlashEffect::BlackWhiteFlashEffect() :
    RasterEffect(QObject::tr("Black-White Flash"),
                 AppSupport::getRasterEffectHardwareSupport("BlackWhiteFlash",
                                                            HardwareSupport::cpuOnly),
                 false,
                 RasterEffectType::BLACK_WHITE_FLASH)
{
    mThreshold = enve::make_shared<QrealAnimator>(34.0, 0.0, 255.0, 1.0, QStringLiteral("阈值"));
    ca_addChild(mThreshold);

    mContrast = enve::make_shared<QrealAnimator>(142.0, 50.0, 300.0, 1.0, QStringLiteral("对比度"));
    ca_addChild(mContrast);

    mEdgeIntensity = enve::make_shared<QrealAnimator>(50.0, 0.0, 100.0, 1.0, QStringLiteral("边缘强度"));
    ca_addChild(mEdgeIntensity);

    // AE point params are unbounded; the default clamp range is
    // +/-10M (TEN_MIL) which is effectively unlimited - do not box
    // the center into a small range or canvas dragging sticks at it.
    mCenterX = enve::make_shared<QrealAnimator>(QStringLiteral("中心 X"));
    ca_addChild(mCenterX);

    mCenterY = enve::make_shared<QrealAnimator>(QStringLiteral("中心 Y"));
    ca_addChild(mCenterY);

    mLightIntensity = enve::make_shared<QrealAnimator>(80.0, 0.0, 100.0, 1.0, QStringLiteral("光线强度"));
    ca_addChild(mLightIntensity);

    mLightLength = enve::make_shared<QrealAnimator>(196.0, 1.0, 300.0, 1.0, QStringLiteral("光线长度"));
    ca_addChild(mLightLength);

    mContourSimplify = enve::make_shared<QrealAnimator>(1.0, 0.0, 50.0, 0.01, QStringLiteral("轮廓简化"));
    ca_addChild(mContourSimplify);

    mLineDensity = enve::make_shared<QrealAnimator>(1.0, 1.0, 20.0, 1.0, QStringLiteral("法线采样密度"));
    ca_addChild(mLineDensity);

    mBlurStrength = enve::make_shared<QrealAnimator>(15.0, 0.0, 100.0, 1.0, QStringLiteral("模糊强度"));
    ca_addChild(mBlurStrength);

    mBlurQuality = enve::make_shared<QrealAnimator>(15.0, 2.0, 16.0, 1.0, QStringLiteral("模糊品质"));
    ca_addChild(mBlurQuality);

    mFlashIntensity = enve::make_shared<QrealAnimator>(100.0, 0.0, 100.0, 1.0, QStringLiteral("闪光强度"));
    ca_addChild(mFlashIntensity);

    mFlashColor = enve::make_shared<ColorAnimator>(QStringLiteral("闪光颜色"));
    mFlashColor->setColor(QColor(255, 255, 255, 255));
    ca_addChild(mFlashColor);

    mBgColor = enve::make_shared<ColorAnimator>(QStringLiteral("背景颜色"));
    mBgColor->setColor(QColor(0, 0, 0, 255));
    ca_addChild(mBgColor);

    prp_enabledDrawingOnCanvas();

    // Property::prp_drawCanvasControls (the default implementation)
    // draws the handler's points, and the canvas dispatches dragging
    // through the same handler - one object for both concerns.
    setPointsHandler(enve::make_shared<PointsHandler>());
    getPointsHandler()->appendPt(enve::make_shared<BwfCenterPoint>(this));
}

namespace {

struct BlackWhiteFlashData {
    double mThreshold01 = 34.0 / 255.0;
    double mContrast = 1.42;
    double mEdgeIntensity = 0.5;
    // center in rendered-bitmap pixel space (AE layer pixels * resolution)
    double mCenterX = 0.0;
    double mCenterY = 0.0;
    double mLightIntensity = 0.8;
    double mLightLength = 196.0;
    double mContourSimplify = 1.0;
    int mLineDensity = 1;
    double mBlurStrength = 0.15;
    int mBlurQuality = 15;
    double mFlashIntensity = 1.0;
    double mFcR = 1.0, mFcG = 1.0, mFcB = 1.0;
    double mBgR = 0.0, mBgG = 0.0, mBgB = 0.0;
};

} // namespace

class BlackWhiteFlashEffectCaller : public RasterEffectCaller {
public:
    BlackWhiteFlashEffectCaller(const HardwareSupport hwSupport,
                                const BlackWhiteFlashData& data) :
        RasterEffectCaller(hwSupport, false), mData(data) {}

    // The AE pipeline (SDF / contour tracing / radial blur) is a
    // whole-image, order-dependent computation: force a single tile.
    int cpuThreads(const int available, const int area) const override {
        Q_UNUSED(available)
        Q_UNUSED(area)
        return 1;
    }

    void processCpu(CpuRenderTools& renderTools,
                    const CpuRenderData& data) override;
private:
    const BlackWhiteFlashData mData;
};

stdsptr<RasterEffectCaller> BlackWhiteFlashEffect::getEffectCaller(
        const qreal relFrame, const qreal resolution,
        const qreal influence, BoxRenderData * const data) const
{
    Q_UNUSED(data)

    BlackWhiteFlashData effData;
    effData.mThreshold01 = mThreshold->getEffectiveValue(relFrame) / 255.0;
    effData.mContrast = mContrast->getEffectiveValue(relFrame) / 100.0;
    effData.mEdgeIntensity = mEdgeIntensity->getEffectiveValue(relFrame) / 100.0;
    effData.mCenterX = mCenterX->getEffectiveValue(relFrame) * resolution;
    effData.mCenterY = mCenterY->getEffectiveValue(relFrame) * resolution;
    effData.mLightIntensity = mLightIntensity->getEffectiveValue(relFrame) / 100.0 * influence;
    effData.mLightLength = mLightLength->getEffectiveValue(relFrame);
    effData.mContourSimplify = mContourSimplify->getEffectiveValue(relFrame);
    effData.mLineDensity = qRound(mLineDensity->getEffectiveValue(relFrame));
    effData.mBlurStrength = mBlurStrength->getEffectiveValue(relFrame) / 100.0;
    effData.mBlurQuality = qRound(mBlurQuality->getEffectiveValue(relFrame));
    effData.mFlashIntensity = mFlashIntensity->getEffectiveValue(relFrame) / 100.0;
    const QColor flashCol = mFlashColor->getColor(relFrame);
    effData.mFcR = flashCol.redF();
    effData.mFcG = flashCol.greenF();
    effData.mFcB = flashCol.blueF();
    const QColor bgCol = mBgColor->getColor(relFrame);
    effData.mBgR = bgCol.redF();
    effData.mBgG = bgCol.greenF();
    effData.mBgB = bgCol.blueF();

    return enve::make_shared<BlackWhiteFlashEffectCaller>(
                instanceHwSupport(), effData);
}

void BlackWhiteFlashEffectCaller::processCpu(CpuRenderTools& renderTools,
                                             const CpuRenderData& data)
{
    const auto& srcBtmp = renderTools.fSrcBtmp;
    const auto& dstBtmp = renderTools.fDstBtmp;
    if (srcBtmp.empty() || srcBtmp.getPixels() == nullptr ||
        dstBtmp.empty() || dstBtmp.getPixels() == nullptr) {
        return;
    }

    const int w = srcBtmp.width();
    const int h = srcBtmp.height();
    if (w <= 0 || h <= 0) return;

    std::vector<double> binaryMap(size_t(w) * h, 0.0);
    std::vector<double> sdf;
    std::vector<BwfPoint> contourPoints;
    std::vector<double> lightMap(size_t(w) * h, 0.0);
    std::vector<double> blurred(size_t(w) * h, 0.0);

    stage1Threshold(srcBtmp, binaryMap,
                    mData.mThreshold01, mData.mContrast, w, h);

    stage2ComputeSDF(binaryMap, sdf, w, h);

    extractSimplifiedContours(binaryMap, mData.mContourSimplify,
                              w, h, contourPoints);

    stage4NormalEmission(sdf, lightMap, contourPoints,
                         mData.mLineDensity, mData.mLightIntensity,
                         mData.mLightLength, mData.mEdgeIntensity, w, h);

    for (size_t i = 0; i < binaryMap.size(); i++) {
        binaryMap[i] = clampF(binaryMap[i] + lightMap[i], 0.0, 1.0);
    }

    stage5RadialBlur(binaryMap, blurred,
                     mData.mCenterX, mData.mCenterY,
                     mData.mBlurStrength, mData.mBlurQuality, w, h);

    // Final composite, AE Render16 tail: lerp(bg, flash, v) then
    // lerp(src, that, flashIntensity), alpha passed through untouched.
    const int xMin = std::max(0, data.fTexTile.left());
    const int xMax = std::min(data.fTexTile.right(), w - 1);
    const int yMin = std::max(0, data.fTexTile.top());
    const int yMax = std::min(data.fTexTile.bottom(), h - 1);
    const int tileL = data.fTexTile.left();
    const int tileT = data.fTexTile.top();

    for (int yi = yMin; yi <= yMax; yi++) {
        auto dst = static_cast<uchar*>(dstBtmp.getAddr(xMin - tileL, yi - tileT));
        auto src = static_cast<const uchar*>(srcBtmp.getAddr(xMin, yi));
        for (int xi = xMin; xi <= xMax; xi++) {
            const double sr = *src++ / 255.0;
            const double sg = *src++ / 255.0;
            const double sb = *src++ / 255.0;
            const uchar sa = *src++;

            double v = blurred[size_t(yi) * w + xi];
            v = clampF(v, 0.0, 1.0);

            double outR = lerpF(mData.mBgR, mData.mFcR, v);
            double outG = lerpF(mData.mBgG, mData.mFcG, v);
            double outB = lerpF(mData.mBgB, mData.mFcB, v);

            outR = lerpF(sr, outR, mData.mFlashIntensity);
            outG = lerpF(sg, outG, mData.mFlashIntensity);
            outB = lerpF(sb, outB, mData.mFlashIntensity);

            *dst++ = static_cast<uchar>(clampF(outR * 255.0, 0.0, 255.0) + 0.5);
            *dst++ = static_cast<uchar>(clampF(outG * 255.0, 0.0, 255.0) + 0.5);
            *dst++ = static_cast<uchar>(clampF(outB * 255.0, 0.0, 255.0) + 0.5);
            *dst++ = sa;
        }
    }
}

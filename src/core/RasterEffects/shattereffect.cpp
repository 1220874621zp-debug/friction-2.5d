/*
#
# Friction - https://friction.graphics
#
# Copyright (c) Ole-André Rodlie and contributors
#
# See 'README.md' for more information.
#
*/

#include "shattereffect.h"
#include "gpurendertools.h"
#include "openglrastereffectcaller.h"

#include "Animators/qrealanimator.h"
#include "appsupport.h"
#include <QtMath>
#include <algorithm>

ShatterEffect::ShatterEffect() :
    RasterEffect(QObject::tr("Shatter"),
                 AppSupport::getRasterEffectHardwareSupport("Shatter",
                                                            HardwareSupport::gpuPreffered),
                 true,
                 RasterEffectType::SHATTER)
{
    mProgress = enve::make_shared<QrealAnimator>(0.0, 0.0, 100.0, 1.0, "progress");
    ca_addChild(mProgress);

    mPieces = enve::make_shared<QrealAnimator>(16.0, 4.0, 80.0, 1.0, "pieces");
    ca_addChild(mPieces);

    mDispersion = enve::make_shared<QrealAnimator>(50.0, 0.0, 200.0, 1.0, "dispersion");
    ca_addChild(mDispersion);

    mGravity = enve::make_shared<QrealAnimator>(60.0, -200.0, 200.0, 1.0, "gravity");
    ca_addChild(mGravity);

    mRotation = enve::make_shared<QrealAnimator>(3.0, 0.0, 10.0, 0.1, "rotation");
    ca_addChild(mRotation);

    mSeed = enve::make_shared<QrealAnimator>(1.0, 0.0, 100.0, 1.0, "seed");
    ca_addChild(mSeed);
}

class ShatterEffectCaller : public OpenGLRasterEffectCaller {
public:
    ShatterEffectCaller(const HardwareSupport hwSupport,
                        const qreal progress,
                        const qreal pieces,
                        const qreal dispersion,
                        const qreal gravity,
                        const qreal rotation,
                        const qreal seed) :
        OpenGLRasterEffectCaller(sInitialized, sProgramId,
                                 ":/shaders/shattereffect.frag",
                                 hwSupport),
        mProgress(progress),
        mPieces(pieces),
        mDispersion(dispersion),
        mGravity(gravity),
        mRotation(rotation),
        mSeed(seed) {}

    void processCpu(CpuRenderTools& renderTools,
                    const CpuRenderData& data) override;
protected:
    void iniVars(QGL33 * const gl) const override {
        sProgressU = gl->glGetUniformLocation(sProgramId, "progress");
        sPiecesU = gl->glGetUniformLocation(sProgramId, "pieces");
        sDispersionU = gl->glGetUniformLocation(sProgramId, "dispersion");
        sGravityU = gl->glGetUniformLocation(sProgramId, "gravity");
        sRotationU = gl->glGetUniformLocation(sProgramId, "rotation");
        sSeedU = gl->glGetUniformLocation(sProgramId, "seed");
    }

    void setVars(QGL33 * const gl) const override {
        gl->glUseProgram(sProgramId);
        gl->glUniform1f(sProgressU, toSkScalar(mProgress));
        gl->glUniform1f(sPiecesU, toSkScalar(mPieces));
        gl->glUniform1f(sDispersionU, toSkScalar(mDispersion));
        gl->glUniform1f(sGravityU, toSkScalar(mGravity));
        gl->glUniform1f(sRotationU, toSkScalar(mRotation));
        gl->glUniform1f(sSeedU, toSkScalar(mSeed));
    }
private:
    static bool sInitialized;
    static GLuint sProgramId;

    static GLint sProgressU;
    static GLint sPiecesU;
    static GLint sDispersionU;
    static GLint sGravityU;
    static GLint sRotationU;
    static GLint sSeedU;

    const qreal mProgress;
    const qreal mPieces;
    const qreal mDispersion;
    const qreal mGravity;
    const qreal mRotation;
    const qreal mSeed;
};

bool ShatterEffectCaller::sInitialized = false;
GLuint ShatterEffectCaller::sProgramId = 0;

GLint ShatterEffectCaller::sProgressU = -1;
GLint ShatterEffectCaller::sPiecesU = -1;
GLint ShatterEffectCaller::sDispersionU = -1;
GLint ShatterEffectCaller::sGravityU = -1;
GLint ShatterEffectCaller::sRotationU = -1;
GLint ShatterEffectCaller::sSeedU = -1;

stdsptr<RasterEffectCaller> ShatterEffect::getEffectCaller(
        const qreal relFrame, const qreal resolution,
        const qreal influence, BoxRenderData * const data) const {
    Q_UNUSED(resolution)
    Q_UNUSED(data)

    const qreal rawProgress = mProgress->getEffectiveValue(relFrame);
    const qreal progress = (rawProgress / 100.0) * influence;
    const qreal pieces = mPieces->getEffectiveValue(relFrame);
    const qreal dispersion = mDispersion->getEffectiveValue(relFrame);
    const qreal gravity = mGravity->getEffectiveValue(relFrame);
    const qreal rotation = mRotation->getEffectiveValue(relFrame);
    const qreal seed = mSeed->getEffectiveValue(relFrame);

    return enve::make_shared<ShatterEffectCaller>(
                instanceHwSupport(), progress, pieces, dispersion, gravity, rotation, seed);
}

void ShatterEffectCaller::processCpu(CpuRenderTools& renderTools,
                                     const CpuRenderData& data) {
    const auto& srcBtmp = renderTools.fSrcBtmp;
    const auto& dstBtmp = renderTools.fDstBtmp;
    if (srcBtmp.empty() || dstBtmp.empty()) return;

    const int imgWidth = srcBtmp.width();
    const int imgHeight = srcBtmp.height();
    if (imgWidth <= 0 || imgHeight <= 0) return;

    const int xMin = std::max(0, data.fTexTile.left());
    const int xMax = std::min((int)data.fTexTile.right(), imgWidth - 1);
    const int yMin = std::max(0, data.fTexTile.top());
    const int yMax = std::min((int)data.fTexTile.bottom(), imgHeight - 1);

    if (mProgress <= 0.0001) {
        for(int yi = yMin; yi <= yMax; yi++) {
            auto dst = static_cast<uchar*>(dstBtmp.getAddr(0, yi - yMin));
            const auto src = static_cast<const uchar*>(srcBtmp.getAddr(xMin, yi));
            std::memcpy(dst, src, (xMax - xMin + 1) * 4);
        }
        return;
    }

    const auto sampleSrc = [&srcBtmp, imgWidth, imgHeight](int x, int y, int ch) -> uchar {
        if (x < 0 || x >= imgWidth || y < 0 || y >= imgHeight) return 0;
        const auto p = static_cast<const uchar*>(srcBtmp.getAddr(x, y));
        return p[ch];
    };

    const qreal nPieces = std::max(4.0, std::min(80.0, mPieces));
    const qreal t = mProgress;

    const auto hash2 = [this](qreal px, qreal py) -> QPointF {
        qreal dot1 = px * (127.1 + mSeed) + py * (311.7 - mSeed);
        qreal dot2 = px * (269.5 + mSeed) + py * (183.3 + mSeed);
        qreal v1 = std::sin(dot1) * 43758.5453123;
        qreal v2 = std::sin(dot2) * 43758.5453123;
        return QPointF(v1 - std::floor(v1), v2 - std::floor(v2));
    };

    for(int yi = yMin; yi <= yMax; yi++) {
        auto dst = static_cast<uchar*>(dstBtmp.getAddr(0, yi - yMin));
        const qreal ny = qreal(yi) / imgHeight;

        for(int xi = xMin; xi <= xMax; xi++) {
            const qreal nx = qreal(xi) / imgWidth;

            const qreal px = nx * nPieces;
            const qreal py = ny * nPieces;
            const int ix = std::floor(px);
            const int iy = std::floor(py);
            const qreal fx = px - ix;
            const qreal fy = py - iy;

            qreal minDist = 10.0;
            qreal secondDist = 10.0;
            int bestCellX = ix;
            int bestCellY = iy;
            QPointF bestOffset(0, 0);

            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    int cx = ix + dx;
                    int cy = iy + dy;
                    QPointF h = hash2(cx, cy);
                    QPointF pt(0.5 + 0.4 * std::sin(6.2831 * h.x()),
                               0.5 + 0.4 * std::sin(6.2831 * h.y()));
                    qreal diffX = dx + pt.x() - fx;
                    qreal diffY = dy + pt.y() - fy;
                    qreal dist = std::sqrt(diffX * diffX + diffY * diffY);

                    if (dist < minDist) {
                        secondDist = minDist;
                        minDist = dist;
                        bestCellX = cx;
                        bestCellY = cy;
                        bestOffset = pt;
                    } else if (dist < secondDist) {
                        secondDist = dist;
                    }
                }
            }

            const qreal scX = (bestCellX + bestOffset.x()) / nPieces;
            const qreal scY = (bestCellY + bestOffset.y()) / nPieces;

            qreal sdirX = scX - 0.5;
            qreal sdirY = scY - 0.5;
            qreal cLen = std::sqrt(sdirX * sdirX + sdirY * sdirY);
            if (cLen > 0.001) { sdirX /= cLen; sdirY /= cLen; }
            else { sdirX = 0; sdirY = 1; }

            QPointF rnd = hash2(bestCellX * 13.37, bestCellY * 13.37);
            qreal randX = (rnd.x() - 0.5) * 2.0;
            qreal randY = (rnd.y() - 0.5) * 2.0;

            qreal velX = (sdirX * mDispersion * 0.4 + randX * mDispersion * 0.2) * 0.01;
            qreal velY = (sdirY * mDispersion * 0.4 + randY * mDispersion * 0.2) * 0.01;
            qreal gravY = (mGravity * 0.4) * 0.01;

            qreal deltaX = velX * t;
            qreal deltaY = velY * t + 0.5 * gravY * t * t;

            qreal spin = (rnd.x() - 0.5) * mRotation * 12.0 * t;
            qreal cosA = std::cos(spin);
            qreal sinA = std::sin(spin);

            qreal locX = nx - (scX + deltaX);
            qreal locY = ny - (scY + deltaY);

            // invRot * local
            qreal origLocX = cosA * locX + sinA * locY;
            qreal origLocY = -sinA * locX + cosA * locY;

            qreal origUvX = scX + origLocX;
            qreal origUvY = scY + origLocY;

            if (origUvX < 0.0 || origUvX > 1.0 || origUvY < 0.0 || origUvY > 1.0) {
                *dst++ = 0; *dst++ = 0; *dst++ = 0; *dst++ = 0;
                continue;
            }

            int sx = qRound(origUvX * imgWidth);
            int sy = qRound(origUvY * imgHeight);

            qreal edgeDist = secondDist - minDist;
            qreal edge = std::max(0.0, std::min(1.0, (edgeDist - 0.01) / 0.05));
            qreal highlight = 1.0 - std::max(0.0, std::min(1.0, (edgeDist - 0.02) / 0.06));

            uchar b = sampleSrc(sx, sy, 0);
            uchar g = sampleSrc(sx, sy, 1);
            uchar r = sampleSrc(sx, sy, 2);
            uchar a = sampleSrc(sx, sy, 3);

            r = std::min(255, int(r + highlight * 64));
            g = std::min(255, int(g + highlight * 64));
            b = std::min(255, int(b + highlight * 64));
            a = static_cast<uchar>(a * edge * std::max(0.0, std::min(1.0, 1.0 - t * 0.4)));

            *dst++ = b;
            *dst++ = g;
            *dst++ = r;
            *dst++ = a;
        }
    }
}

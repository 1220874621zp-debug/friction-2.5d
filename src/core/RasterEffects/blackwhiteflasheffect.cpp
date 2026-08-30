/*
#
# Friction - https://friction.graphics
#
# Copyright (c) Ole-André Rodlie and contributors
#
# See 'README.md' for more information.
#
*/

#include "blackwhiteflasheffect.h"
#include "gpurendertools.h"
#include "openglrastereffectcaller.h"
#include "Animators/qrealanimator.h"
#include "Animators/coloranimator.h"
#include "Boxes/boundingbox.h"
#include "skia/skqtconversions.h"
#include "appsupport.h"
#include <cmath>

BlackWhiteFlashEffect::BlackWhiteFlashEffect() :
    RasterEffect("blackWhiteFlash",
                 AppSupport::getRasterEffectHardwareSupport("BlackWhiteFlash",
                                                            HardwareSupport::gpuPreffered),
                 true,
                 RasterEffectType::BLACK_WHITE_FLASH)
{
    mCenterX = enve::make_shared<QrealAnimator>(50.0, -100.0, 200.0, 1.0, "x");
    ca_addChild(mCenterX);

    mCenterY = enve::make_shared<QrealAnimator>(50.0, -100.0, 200.0, 1.0, "y");
    ca_addChild(mCenterY);

    mThreshold = enve::make_shared<QrealAnimator>(50.0, 0.0, 100.0, 1.0, "threshold");
    ca_addChild(mThreshold);

    mContrast = enve::make_shared<QrealAnimator>(2.0, 0.1, 10.0, 0.1, "contrast");
    ca_addChild(mContrast);

    mLightIntensity = enve::make_shared<QrealAnimator>(80.0, 0.0, 200.0, 1.0, "intensity");
    ca_addChild(mLightIntensity);

    mLightLength = enve::make_shared<QrealAnimator>(25.0, 0.0, 100.0, 1.0, "distance");
    ca_addChild(mLightLength);

    mEdgeIntensity = enve::make_shared<QrealAnimator>(1.5, 0.0, 5.0, 0.1, "edge");
    ca_addChild(mEdgeIntensity);

    mInvert = enve::make_shared<QrealAnimator>(0.0, 0.0, 1.0, 1.0, "invert");
    ca_addChild(mInvert);

    mBlend = enve::make_shared<QrealAnimator>(100.0, 0.0, 100.0, 1.0, "blend");
    ca_addChild(mBlend);

    mFlashColor = enve::make_shared<ColorAnimator>("color");
    mFlashColor->setColor(QColor(255, 255, 255, 255));
    ca_addChild(mFlashColor);

    mBgColor = enve::make_shared<ColorAnimator>("bgcolor");
    mBgColor->setColor(QColor(0, 0, 0, 255));
    ca_addChild(mBgColor);

    prp_enabledDrawingOnCanvas();
}

class BlackWhiteFlashEffectCaller : public OpenGLRasterEffectCaller {
public:
    BlackWhiteFlashEffectCaller(const HardwareSupport hwSupport,
                                qreal centerX,
                                qreal centerY,
                                qreal threshold,
                                qreal contrast,
                                qreal lightIntensity,
                                qreal lightLength,
                                qreal edgeIntensity,
                                qreal invert,
                                qreal blend,
                                const QColor& flashColor,
                                const QColor& bgColor) :
        OpenGLRasterEffectCaller(sInitialized, sProgramId,
                                 ":/shaders/blackwhiteflasheffect.frag",
                                 hwSupport),
        mCenterX(centerX),
        mCenterY(centerY),
        mThreshold(threshold),
        mContrast(contrast),
        mLightIntensity(lightIntensity),
        mLightLength(lightLength),
        mEdgeIntensity(edgeIntensity),
        mInvert(invert),
        mBlend(blend),
        mFlashColor(flashColor),
        mBgColor(bgColor) {}

    void processCpu(CpuRenderTools& renderTools, const CpuRenderData& data) override;
protected:
    void iniVars(QGL33 * const gl) const override {
        sAnchorU = gl->glGetUniformLocation(sProgramId, "anchor");
        sThresholdU = gl->glGetUniformLocation(sProgramId, "threshold");
        sContrastU = gl->glGetUniformLocation(sProgramId, "contrast");
        sLightIntensityU = gl->glGetUniformLocation(sProgramId, "lightIntensity");
        sLightLengthU = gl->glGetUniformLocation(sProgramId, "lightLength");
        sEdgeIntensityU = gl->glGetUniformLocation(sProgramId, "edgeIntensity");
        sInvertU = gl->glGetUniformLocation(sProgramId, "invert");
        sBlendU = gl->glGetUniformLocation(sProgramId, "blend");
        sFlashColorU = gl->glGetUniformLocation(sProgramId, "flashColor");
        sBgColorU = gl->glGetUniformLocation(sProgramId, "bgColor");
    }

    void setVars(QGL33 * const gl) const override {
        gl->glUseProgram(sProgramId);
        gl->glUniform2f(sAnchorU, toSkScalar(mCenterX * 0.01), toSkScalar(mCenterY * 0.01));
        gl->glUniform1f(sThresholdU, toSkScalar(mThreshold));
        gl->glUniform1f(sContrastU, toSkScalar(mContrast));
        gl->glUniform1f(sLightIntensityU, toSkScalar(mLightIntensity));
        gl->glUniform1f(sLightLengthU, toSkScalar(mLightLength));
        gl->glUniform1f(sEdgeIntensityU, toSkScalar(mEdgeIntensity));
        gl->glUniform1f(sInvertU, toSkScalar(mInvert));
        gl->glUniform1f(sBlendU, toSkScalar(mBlend));
        gl->glUniform4f(sFlashColorU, mFlashColor.redF(), mFlashColor.greenF(), mFlashColor.blueF(), mFlashColor.alphaF());
        gl->glUniform4f(sBgColorU, mBgColor.redF(), mBgColor.greenF(), mBgColor.blueF(), mBgColor.alphaF());
    }
private:
    static bool sInitialized;
    static GLuint sProgramId;

    static GLint sAnchorU;
    static GLint sThresholdU;
    static GLint sContrastU;
    static GLint sLightIntensityU;
    static GLint sLightLengthU;
    static GLint sEdgeIntensityU;
    static GLint sInvertU;
    static GLint sBlendU;
    static GLint sFlashColorU;
    static GLint sBgColorU;

    const qreal mCenterX;
    const qreal mCenterY;
    const qreal mThreshold;
    const qreal mContrast;
    const qreal mLightIntensity;
    const qreal mLightLength;
    const qreal mEdgeIntensity;
    const qreal mInvert;
    const qreal mBlend;
    const QColor mFlashColor;
    const QColor mBgColor;
};

bool BlackWhiteFlashEffectCaller::sInitialized = false;
GLuint BlackWhiteFlashEffectCaller::sProgramId = 0;

GLint BlackWhiteFlashEffectCaller::sAnchorU = -1;
GLint BlackWhiteFlashEffectCaller::sThresholdU = -1;
GLint BlackWhiteFlashEffectCaller::sContrastU = -1;
GLint BlackWhiteFlashEffectCaller::sLightIntensityU = -1;
GLint BlackWhiteFlashEffectCaller::sLightLengthU = -1;
GLint BlackWhiteFlashEffectCaller::sEdgeIntensityU = -1;
GLint BlackWhiteFlashEffectCaller::sInvertU = -1;
GLint BlackWhiteFlashEffectCaller::sBlendU = -1;
GLint BlackWhiteFlashEffectCaller::sFlashColorU = -1;
GLint BlackWhiteFlashEffectCaller::sBgColorU = -1;

stdsptr<RasterEffectCaller> BlackWhiteFlashEffect::getEffectCaller(
        const qreal relFrame, const qreal resolution,
        const qreal influence, BoxRenderData * const data) const {
    Q_UNUSED(resolution)
    Q_UNUSED(data)

    const qreal centerX = mCenterX->getEffectiveValue(relFrame);
    const qreal centerY = mCenterY->getEffectiveValue(relFrame);
    const qreal threshold = mThreshold->getEffectiveValue(relFrame);
    const qreal contrast = mContrast->getEffectiveValue(relFrame);
    const qreal lightIntensity = mLightIntensity->getEffectiveValue(relFrame) * influence;
    const qreal lightLength = mLightLength->getEffectiveValue(relFrame);
    const qreal edgeIntensity = mEdgeIntensity->getEffectiveValue(relFrame);
    const qreal invert = mInvert->getEffectiveValue(relFrame);
    const qreal blend = mBlend->getEffectiveValue(relFrame);
    const QColor flashColor = mFlashColor->getColor(relFrame);
    const QColor bgColor = mBgColor->getColor(relFrame);

    return enve::make_shared<BlackWhiteFlashEffectCaller>(
                instanceHwSupport(), centerX, centerY, threshold, contrast, lightIntensity,
                lightLength, edgeIntensity, invert, blend, flashColor, bgColor);
}

void BlackWhiteFlashEffectCaller::processCpu(CpuRenderTools& renderTools, const CpuRenderData& data) {
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

    const qreal blendF = (mBlend * 0.01);

    for(int yi = yMin; yi <= yMax; yi++) {
        auto dst = static_cast<uchar*>(dstBtmp.getAddr(0, yi - yMin));
        auto src = static_cast<uchar*>(srcBtmp.getAddr(xMin, yi));

        for(int xi = xMin; xi <= xMax; xi++) {
            const uchar r = *src++;
            const uchar g = *src++;
            const uchar b = *src++;
            const uchar a = *src++;

            if (a < 2) {
                *dst++ = 0; *dst++ = 0; *dst++ = 0; *dst++ = 0;
                continue;
            }

            const qreal lum = (0.299 * r + 0.587 * g + 0.114 * b) / 255.0;
            qreal cl = (lum - 0.5) * mContrast + 0.5;
            qreal val = (cl > mThreshold * 0.01) ? 1.0 : 0.0;
            if (mInvert > 0.5) val = 1.0 - val;

            const qreal fR = mBgColor.redF() * (1.0 - val) + mFlashColor.redF() * val;
            const qreal fG = mBgColor.greenF() * (1.0 - val) + mFlashColor.greenF() * val;
            const qreal fB = mBgColor.blueF() * (1.0 - val) + mFlashColor.blueF() * val;

            const qreal outR = r * (1.0 - blendF) + fR * 255.0 * blendF;
            const qreal outG = g * (1.0 - blendF) + fG * 255.0 * blendF;
            const qreal outB = b * (1.0 - blendF) + fB * 255.0 * blendF;

            *dst++ = static_cast<uchar>(std::max(0.0, std::min(255.0, outR)));
            *dst++ = static_cast<uchar>(std::max(0.0, std::min(255.0, outG)));
            *dst++ = static_cast<uchar>(std::max(0.0, std::min(255.0, outB)));
            *dst++ = a;
        }
    }
}

void BlackWhiteFlashEffect::prp_drawCanvasControls(
        SkCanvas * const canvas, const CanvasMode mode,
        const float invScale, const bool ctrlPressed) {
    Q_UNUSED(mode)
    Q_UNUSED(ctrlPressed)

    const auto box = getFirstAncestor<BoundingBox>();
    if (!box) return;

    const QRectF bRect = box->getRelBoundingRect();
    const qreal cx = bRect.left() + (mCenterX->getEffectiveValue() * 0.01) * bRect.width();
    const qreal cy = bRect.top() + (mCenterY->getEffectiveValue() * 0.01) * bRect.height();
    const SkPoint pt = toSkPoint(QPointF(cx, cy));

    const float r = 10.0f * invScale;
    const float cross = 18.0f * invScale;

    SkPaint pShadow;
    pShadow.setAntiAlias(true);
    pShadow.setColor(SkColorSetARGB(180, 0, 0, 0));
    pShadow.setStyle(SkPaint::kStroke_Style);
    pShadow.setStrokeWidth(3.0f * invScale);

    SkPaint pLine;
    pLine.setAntiAlias(true);
    pLine.setColor(SkColorSetARGB(255, 0, 220, 255));
    pLine.setStyle(SkPaint::kStroke_Style);
    pLine.setStrokeWidth(1.5f * invScale);

    canvas->drawCircle(pt.fX, pt.fY, r, pShadow);
    canvas->drawCircle(pt.fX, pt.fY, r, pLine);

    SkPaint pDot;
    pDot.setAntiAlias(true);
    pDot.setColor(SkColorSetARGB(255, 255, 255, 255));
    pDot.setStyle(SkPaint::kFill_Style);
    canvas->drawCircle(pt.fX, pt.fY, 2.5f * invScale, pDot);

    canvas->drawLine(pt.fX - cross, pt.fY, pt.fX - r * 0.5f, pt.fY, pShadow);
    canvas->drawLine(pt.fX + r * 0.5f, pt.fY, pt.fX + cross, pt.fY, pShadow);
    canvas->drawLine(pt.fX, pt.fY - cross, pt.fX, pt.fY - r * 0.5f, pShadow);
    canvas->drawLine(pt.fX, pt.fY + r * 0.5f, pt.fX, pt.fY + cross, pShadow);

    canvas->drawLine(pt.fX - cross, pt.fY, pt.fX - r * 0.5f, pt.fY, pLine);
    canvas->drawLine(pt.fX + r * 0.5f, pt.fY, pt.fX + cross, pt.fY, pLine);
    canvas->drawLine(pt.fX, pt.fY - cross, pt.fX, pt.fY - r * 0.5f, pLine);
    canvas->drawLine(pt.fX, pt.fY + r * 0.5f, pt.fX, pt.fY + cross, pLine);
}

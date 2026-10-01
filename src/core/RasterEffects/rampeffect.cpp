/*
#
# Friction - https://friction.graphics
#
# Copyright (c) Ole-André Rodlie and contributors
#
# See 'README.md' for more information.
#
*/

#include "rampeffect.h"
#include "gpurendertools.h"
#include "openglrastereffectcaller.h"
#include "Animators/qrealanimator.h"
#include "Animators/qpointfanimator.h"
#include "Animators/coloranimator.h"
#include "Properties/comboboxproperty.h"
#include "MovablePoints/pointshandler.h"
#include "RasterEffects/effectcanvaspoint.h"
#include "appsupport.h"

RampEffect::RampEffect() :
    RasterEffect(QObject::tr("渐变 (Ramp)"),
                 AppSupport::getRasterEffectHardwareSupport("Ramp",
                                                            HardwareSupport::gpuPreffered),
                 true,
                 RasterEffectType::RAMP)
{
    mStartPoint = enve::make_shared<QPointFAnimator>(QStringLiteral("起始点"));
    mStartPoint->setBaseValue(0.25, 0.25);
    ca_addChild(mStartPoint);

    mStartColor = enve::make_shared<ColorAnimator>(QStringLiteral("起始颜色"));
    mStartColor->setColor(QColor(0, 0, 0, 255));
    ca_addChild(mStartColor);

    mEndPoint = enve::make_shared<QPointFAnimator>(QStringLiteral("结束点"));
    mEndPoint->setBaseValue(0.75, 0.75);
    ca_addChild(mEndPoint);

    mEndColor = enve::make_shared<ColorAnimator>(QStringLiteral("结束颜色"));
    mEndColor->setColor(QColor(255, 255, 255, 255));
    ca_addChild(mEndColor);

    mShape = enve::make_shared<ComboBoxProperty>(
                QStringLiteral("渐变形状"), QStringList()
                << QStringLiteral("线性渐变") << QStringLiteral("径向渐变"));
    ca_addChild(mShape);

    mMix = enve::make_shared<QrealAnimator>(0.0, 0.0, 100.0, 1.0,
                                            QStringLiteral("与原图混合"));
    ca_addChild(mMix);

    // AE-style draggable canvas handles for both points
    // (0..1 UV over the host box's content rect)
    setPointsHandler(enve::make_shared<PointsHandler>());
    getPointsHandler()->appendPt(enve::make_shared<EffectCanvasPoint>(
                mStartPoint.get(), this, EffectCanvasPoint::Space::Normalized));
    getPointsHandler()->appendPt(enve::make_shared<EffectCanvasPoint>(
                mEndPoint.get(), this, EffectCanvasPoint::Space::Normalized));
}

class RampEffectCaller : public OpenGLRasterEffectCaller {
public:
    RampEffectCaller(const HardwareSupport hwSupport,
                     const QPointF& startPoint,
                     const QColor& startColor,
                     const QPointF& endPoint,
                     const QColor& endColor,
                     const int shape,
                     const qreal mix) :
        OpenGLRasterEffectCaller(sInitialized, sProgramId,
                                 ":/shaders/rampeffect.frag",
                                 hwSupport),
        mStartPoint(startPoint),
        mStartColor(startColor),
        mEndPoint(endPoint),
        mEndColor(endColor),
        mShape(shape),
        mMix(mix) {}

    void processCpu(CpuRenderTools& renderTools, const CpuRenderData& data) override;
protected:
    void iniVars(QGL33 * const gl) const override {
        sStartPointU = gl->glGetUniformLocation(sProgramId, "startPoint");
        sStartColorU = gl->glGetUniformLocation(sProgramId, "startColor");
        sEndPointU = gl->glGetUniformLocation(sProgramId, "endPoint");
        sEndColorU = gl->glGetUniformLocation(sProgramId, "endColor");
        sShapeU = gl->glGetUniformLocation(sProgramId, "shape");
        sMixU = gl->glGetUniformLocation(sProgramId, "mixOriginal");
    }

    void setVars(QGL33 * const gl) const override {
        gl->glUseProgram(sProgramId);
        gl->glUniform2f(sStartPointU, toSkScalar(mStartPoint.x()), toSkScalar(mStartPoint.y()));
        gl->glUniform4f(sStartColorU, mStartColor.redF(), mStartColor.greenF(), mStartColor.blueF(), mStartColor.alphaF());
        gl->glUniform2f(sEndPointU, toSkScalar(mEndPoint.x()), toSkScalar(mEndPoint.y()));
        gl->glUniform4f(sEndColorU, mEndColor.redF(), mEndColor.greenF(), mEndColor.blueF(), mEndColor.alphaF());
        gl->glUniform1i(sShapeU, mShape);
        gl->glUniform1f(sMixU, toSkScalar(mMix));
    }
private:
    static bool sInitialized;
    static GLuint sProgramId;

    static GLint sStartPointU;
    static GLint sStartColorU;
    static GLint sEndPointU;
    static GLint sEndColorU;
    static GLint sShapeU;
    static GLint sMixU;

    const QPointF mStartPoint;
    const QColor mStartColor;
    const QPointF mEndPoint;
    const QColor mEndColor;
    const int mShape;
    const qreal mMix;
};

bool RampEffectCaller::sInitialized = false;
GLuint RampEffectCaller::sProgramId = 0;

GLint RampEffectCaller::sStartPointU = -1;
GLint RampEffectCaller::sStartColorU = -1;
GLint RampEffectCaller::sEndPointU = -1;
GLint RampEffectCaller::sEndColorU = -1;
GLint RampEffectCaller::sShapeU = -1;
GLint RampEffectCaller::sMixU = -1;

stdsptr<RasterEffectCaller> RampEffect::getEffectCaller(
        const qreal relFrame, const qreal resolution,
        const qreal influence, BoxRenderData * const data) const {
    Q_UNUSED(resolution)
    Q_UNUSED(data)

    const QPointF startPoint = mStartPoint->getEffectiveValue(relFrame);
    const QColor startColor = mStartColor->getColor(relFrame);
    const QPointF endPoint = mEndPoint->getEffectiveValue(relFrame);
    const QColor endColor = mEndColor->getColor(relFrame);
    const int shape = mShape->getCurrentValue();
    // fold the effect-row influence into the blend-back factor:
    // influence 0 => show the original, 1 => the slider value
    const qreal mix = 1.0 - (1.0 - mMix->getEffectiveValue(relFrame) / 100.0) * influence;

    return enve::make_shared<RampEffectCaller>(
                instanceHwSupport(), startPoint, startColor,
                endPoint, endColor, shape, mix);
}

void RampEffectCaller::processCpu(CpuRenderTools& renderTools, const CpuRenderData& data) {
    const auto& srcBtmp = renderTools.fSrcBtmp;
    const auto& dstBtmp = renderTools.fDstBtmp;
    if (srcBtmp.empty() || dstBtmp.empty()) return;

    const int imgWidth = srcBtmp.width();
    const int imgHeight = srcBtmp.height();
    if (imgWidth <= 0 || imgHeight <= 0) return;

    const int xMin = std::max(0, data.fTexTile.left());
    const int xMax = std::min((int)data.fTexTile.right() - 1, imgWidth - 1);
    const int yMin = std::max(0, data.fTexTile.top());
    const int yMax = std::min((int)data.fTexTile.bottom() - 1, imgHeight - 1);

    // gradient math in pixel space so radial stays circular
    const qreal sx = mStartPoint.x() * imgWidth;
    const qreal sy = mStartPoint.y() * imgHeight;
    const qreal dx = mEndPoint.x() * imgWidth - sx;
    const qreal dy = mEndPoint.y() * imgHeight - sy;
    const qreal len2 = dx * dx + dy * dy;
    const qreal radius = std::sqrt(len2);

    const qreal c1r = mStartColor.redF(), c1g = mStartColor.greenF();
    const qreal c1b = mStartColor.blueF(), c1a = mStartColor.alphaF();
    const qreal c2r = mEndColor.redF(), c2g = mEndColor.greenF();
    const qreal c2b = mEndColor.blueF(), c2a = mEndColor.alphaF();
    const qreal mixO = std::max(0.0, std::min(1.0, mMix));

    for(int yi = yMin; yi <= yMax; yi++) {
        auto dst = static_cast<uchar*>(dstBtmp.getAddr(0, yi - yMin));
        auto src = static_cast<uchar*>(srcBtmp.getAddr(xMin, yi));
        const qreal py = yi + 0.5;

        for(int xi = xMin; xi <= xMax; xi++) {
            const uchar r = *src++;
            const uchar g = *src++;
            const uchar b = *src++;
            const uchar a = *src++;

            const qreal px = xi + 0.5;
            qreal t;
            if (mShape == 0) {
                t = len2 > 1e-8 ? ((px - sx) * dx + (py - sy) * dy) / len2 : 0.0;
            } else {
                const qreal rx = px - sx, ry = py - sy;
                t = radius > 1e-6 ? std::sqrt(rx * rx + ry * ry) / radius : 0.0;
            }
            t = std::max(0.0, std::min(1.0, t));

            const qreal ra = c1a * (1.0 - t) + c2a * t;
            const qreal rr = (c1r * (1.0 - t) + c2r * t) * ra;
            const qreal rg = (c1g * (1.0 - t) + c2g * t) * ra;
            const qreal rb = (c1b * (1.0 - t) + c2b * t) * ra;

            // premultiplied "over": generated ramp covers the source
            const qreal inv = 1.0 - ra;
            qreal outR = rr * 255.0 + r * inv;
            qreal outG = rg * 255.0 + g * inv;
            qreal outB = rb * 255.0 + b * inv;
            qreal outA = ra * 255.0 + a * inv;

            outR = outR * (1.0 - mixO) + r * mixO;
            outG = outG * (1.0 - mixO) + g * mixO;
            outB = outB * (1.0 - mixO) + b * mixO;
            outA = outA * (1.0 - mixO) + a * mixO;

            *dst++ = static_cast<uchar>(std::max(0.0, std::min(255.0, outR)));
            *dst++ = static_cast<uchar>(std::max(0.0, std::min(255.0, outG)));
            *dst++ = static_cast<uchar>(std::max(0.0, std::min(255.0, outB)));
            *dst++ = static_cast<uchar>(std::max(0.0, std::min(255.0, outA)));
        }
    }
}

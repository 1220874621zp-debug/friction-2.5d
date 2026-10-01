/*
#
# Friction - https://friction.graphics
#
# Copyright (c) Ole-André Rodlie and contributors
#
# See 'README.md' for more information.
#
*/

#include "grideffect.h"
#include "gpurendertools.h"
#include "openglrastereffectcaller.h"
#include "Animators/qrealanimator.h"
#include "Animators/qpointfanimator.h"
#include "Animators/coloranimator.h"
#include "MovablePoints/pointshandler.h"
#include "RasterEffects/effectcanvaspoint.h"
#include "appsupport.h"

GridEffect::GridEffect() :
    RasterEffect(QObject::tr("网格 (Grid)"),
                 AppSupport::getRasterEffectHardwareSupport("Grid",
                                                            HardwareSupport::gpuPreffered),
                 true,
                 RasterEffectType::GRID)
{
    mAnchor = enve::make_shared<QPointFAnimator>(QStringLiteral("锚点"));
    mAnchor->setBaseValue(0.5, 0.5);
    ca_addChild(mAnchor);

    mSizeW = enve::make_shared<QrealAnimator>(200.0, 1.0, 4000.0, 1.0,
                                              QStringLiteral("宽度"));
    ca_addChild(mSizeW);

    mSizeH = enve::make_shared<QrealAnimator>(200.0, 1.0, 4000.0, 1.0,
                                              QStringLiteral("高度"));
    ca_addChild(mSizeH);

    mBorder = enve::make_shared<QrealAnimator>(2.0, 1.0, 200.0, 0.5,
                                               QStringLiteral("边框"));
    ca_addChild(mBorder);

    mColor = enve::make_shared<ColorAnimator>(QStringLiteral("颜色"));
    mColor->setColor(QColor(255, 255, 255, 255));
    ca_addChild(mColor);

    mInvert = enve::make_shared<QrealAnimator>(0.0, 0.0, 1.0, 1.0,
                                               QStringLiteral("反转网格"));
    ca_addChild(mInvert);

    mMix = enve::make_shared<QrealAnimator>(0.0, 0.0, 100.0, 1.0,
                                            QStringLiteral("与原图混合"));
    ca_addChild(mMix);

    // AE-style draggable canvas handle for the anchor
    // (0..1 UV over the host box's content rect)
    setPointsHandler(enve::make_shared<PointsHandler>());
    getPointsHandler()->appendPt(enve::make_shared<EffectCanvasPoint>(
                mAnchor.get(), this, EffectCanvasPoint::Space::Normalized));
}

class GridEffectCaller : public OpenGLRasterEffectCaller {
public:
    GridEffectCaller(const HardwareSupport hwSupport,
                     const QPointF& anchor,
                     const qreal sizeW,
                     const qreal sizeH,
                     const qreal border,
                     const QColor& color,
                     const qreal invert,
                     const qreal mix) :
        OpenGLRasterEffectCaller(sInitialized, sProgramId,
                                 ":/shaders/grideffect.frag",
                                 hwSupport),
        mAnchor(anchor),
        mSizeW(sizeW),
        mSizeH(sizeH),
        mBorder(border),
        mColor(color),
        mInvert(invert),
        mMix(mix) {}

    void processCpu(CpuRenderTools& renderTools, const CpuRenderData& data) override;
protected:
    void iniVars(QGL33 * const gl) const override {
        sAnchorU = gl->glGetUniformLocation(sProgramId, "anchor");
        sCellSizeU = gl->glGetUniformLocation(sProgramId, "cellSize");
        sBorderU = gl->glGetUniformLocation(sProgramId, "border");
        sColorU = gl->glGetUniformLocation(sProgramId, "color");
        sInvertU = gl->glGetUniformLocation(sProgramId, "invert");
        sMixU = gl->glGetUniformLocation(sProgramId, "mixOriginal");
    }

    void setVars(QGL33 * const gl) const override {
        gl->glUseProgram(sProgramId);
        gl->glUniform2f(sAnchorU, toSkScalar(mAnchor.x()), toSkScalar(mAnchor.y()));
        gl->glUniform2f(sCellSizeU, toSkScalar(mSizeW), toSkScalar(mSizeH));
        gl->glUniform1f(sBorderU, toSkScalar(mBorder));
        gl->glUniform4f(sColorU, mColor.redF(), mColor.greenF(), mColor.blueF(), mColor.alphaF());
        gl->glUniform1f(sInvertU, toSkScalar(mInvert));
        gl->glUniform1f(sMixU, toSkScalar(mMix));
    }
private:
    static bool sInitialized;
    static GLuint sProgramId;

    static GLint sAnchorU;
    static GLint sCellSizeU;
    static GLint sBorderU;
    static GLint sColorU;
    static GLint sInvertU;
    static GLint sMixU;

    const QPointF mAnchor;
    const qreal mSizeW;
    const qreal mSizeH;
    const qreal mBorder;
    const QColor mColor;
    const qreal mInvert;
    const qreal mMix;
};

bool GridEffectCaller::sInitialized = false;
GLuint GridEffectCaller::sProgramId = 0;

GLint GridEffectCaller::sAnchorU = -1;
GLint GridEffectCaller::sCellSizeU = -1;
GLint GridEffectCaller::sBorderU = -1;
GLint GridEffectCaller::sColorU = -1;
GLint GridEffectCaller::sInvertU = -1;
GLint GridEffectCaller::sMixU = -1;

stdsptr<RasterEffectCaller> GridEffect::getEffectCaller(
        const qreal relFrame, const qreal resolution,
        const qreal influence, BoxRenderData * const data) const {
    Q_UNUSED(data)

    const QPointF anchor = mAnchor->getEffectiveValue(relFrame);
    // pixel-space params scale with the render resolution
    const qreal sizeW = std::max(1.0, mSizeW->getEffectiveValue(relFrame) * resolution);
    const qreal sizeH = std::max(1.0, mSizeH->getEffectiveValue(relFrame) * resolution);
    const qreal border = std::max(0.0, mBorder->getEffectiveValue(relFrame) * resolution);
    const QColor color = mColor->getColor(relFrame);
    const qreal invert = mInvert->getEffectiveValue(relFrame);
    // fold the effect-row influence into the blend-back factor
    const qreal mix = 1.0 - (1.0 - mMix->getEffectiveValue(relFrame) / 100.0) * influence;

    return enve::make_shared<GridEffectCaller>(
                instanceHwSupport(), anchor, sizeW, sizeH,
                border, color, invert, mix);
}

void GridEffectCaller::processCpu(CpuRenderTools& renderTools, const CpuRenderData& data) {
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

    const qreal ax = mAnchor.x() * imgWidth;
    const qreal ay = mAnchor.y() * imgHeight;
    const qreal cw = std::max(1.0, mSizeW);
    const qreal ch = std::max(1.0, mSizeH);
    const qreal half = mBorder * 0.5;
    const qreal mixO = std::max(0.0, std::min(1.0, mMix));
    const bool inv = mInvert > 0.5;

    const qreal ca = mColor.alphaF();
    const qreal cr = mColor.redF() * ca;
    const qreal cg = mColor.greenF() * ca;
    const qreal cb = mColor.blueF() * ca;

    // distance to the nearest grid line, wrapped into one cell
    auto edgeDist = [](const qreal rel, const qreal cell) {
        qreal m = std::fmod(rel, cell);
        if (m < 0.0) m += cell;
        return std::min(m, cell - m);
    };

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
            const qreal d = std::min(edgeDist(px - ax, cw),
                                     edgeDist(py - ay, ch));
            // 1.5px smoothstep ramp for antialiasing
            qreal line = 1.0 - (d - (half - 0.75)) / 1.5;
            line = std::max(0.0, std::min(1.0, line));
            if (inv) line = 1.0 - line;

            // premultiplied grid color masked by the line coverage
            const qreal ga = ca * line;
            const qreal gr = cr * line;
            const qreal gg = cg * line;
            const qreal gb = cb * line;

            // premultiplied "over": generated grid covers the source
            const qreal invA = 1.0 - ga;
            qreal outR = gr * 255.0 + r * invA;
            qreal outG = gg * 255.0 + g * invA;
            qreal outB = gb * 255.0 + b * invA;
            qreal outA = ga * 255.0 + a * invA;

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

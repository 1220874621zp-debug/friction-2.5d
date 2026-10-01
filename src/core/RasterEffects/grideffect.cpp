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
#include "glhelpers.h"
#include "gpurendertools.h"
#include "cpurendertools.h"
#include "Animators/qrealanimator.h"
#include "Animators/qpointfanimator.h"
#include "Animators/coloranimator.h"
#include "MovablePoints/pointshandler.h"
#include "RasterEffects/effectcanvaspoint.h"
#include "Boxes/boundingbox.h"
#include "Boxes/boxrenderdata.h"
#include "skia/skqtconversions.h"
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

class GridEffectCaller : public RasterEffectCaller {
public:
    GridEffectCaller(const HardwareSupport hwSupport,
                     const QPointF& anchor,
                     const qreal sizeW,
                     const qreal sizeH,
                     const qreal border,
                     const QColor& color,
                     const qreal invert,
                     const qreal mix,
                     const QRectF& contentAbs) :
        RasterEffectCaller(hwSupport),
        mAnchor(anchor),
        mSizeW(sizeW),
        mSizeH(sizeH),
        mBorder(border),
        mColor(color),
        mInvert(invert),
        mMix(mix),
        mContentAbs(contentAbs) {}

    void processGpu(QGL33 * const gl, GpuRenderTools& renderTools) override;
    void processCpu(CpuRenderTools& renderTools, const CpuRenderData& data) override;

    // anchor in content-rect UV -> texture UV (the texture may carry
    // the base margin and chained effect margins around the content)
    QPointF anchorInTexture(const QRectF& texAbs) const;
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
    // rendered-space rect of the host box content (anchor domain);
    // empty for offscreen previews (whole texture stands in)
    const QRectF mContentAbs;
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

    const QPointF anchor = mAnchor->getEffectiveValue(relFrame);
    // pixel-space params scale with the render resolution
    const qreal sizeW = std::max(1.0, mSizeW->getEffectiveValue(relFrame) * resolution);
    const qreal sizeH = std::max(1.0, mSizeH->getEffectiveValue(relFrame) * resolution);
    const qreal border = std::max(0.0, mBorder->getEffectiveValue(relFrame) * resolution);
    const QColor color = mColor->getColor(relFrame);
    const qreal invert = mInvert->getEffectiveValue(relFrame);
    // fold the effect-row influence into the blend-back factor
    const qreal mix = 1.0 - (1.0 - mMix->getEffectiveValue(relFrame) / 100.0) * influence;

    // the anchor lives in content-rect UV (the canvas handle domain);
    // the render texture carries the base margin + chained effect
    // margins around the content, so the caller needs the content's
    // rendered-space rect to place the anchor correctly. Reading
    // data->fRelBoundingRect here is wrong (still empty at assembly
    // time) - the box's own rect is the same value and is valid now.
    QRectF contentAbs;
    if(data) {
        const QRectF rel = data->fParentBox ?
                    data->fParentBox->getRelBoundingRect() :
                    data->fRelBoundingRect;
        contentAbs = toQTransform(data->getFullRenderTransform())
                     .mapRect(rel);
    }

    return enve::make_shared<GridEffectCaller>(
                instanceHwSupport(), anchor, sizeW, sizeH,
                border, color, invert, mix, contentAbs);
}

QPointF GridEffectCaller::anchorInTexture(const QRectF& texAbs) const {
    if(mContentAbs.width() <= 0. || mContentAbs.height() <= 0. ||
       texAbs.width() <= 0 || texAbs.height() <= 0) {
        // no content domain (offscreen preview): whole texture stands in
        return mAnchor;
    }
    const qreal texW = texAbs.width();
    const qreal texH = texAbs.height();
    const qreal ox = (mContentAbs.left() - texAbs.left()) / texW;
    const qreal oy = (mContentAbs.top() - texAbs.top()) / texH;
    const qreal cx = mContentAbs.width() / texW;
    const qreal cy = mContentAbs.height() / texH;
    return QPointF(ox + mAnchor.x() * cx, oy + mAnchor.y() * cy);
}

void GridEffectCaller::processGpu(QGL33 * const gl,
                                  GpuRenderTools& renderTools) {
    renderTools.switchToOpenGL(gl);

    if(!sInitialized) {
        try {
            gIniProgram(gl, sProgramId, GL_TEXTURED_VERT,
                        ":/shaders/grideffect.frag");
        } catch(...) {
            RuntimeThrow("Could not initialize a program for "
                         "'grideffect.frag'");
        }
        gl->glUseProgram(sProgramId);
        const auto texLocation = gl->glGetUniformLocation(sProgramId, "tex");
        gl->glUniform1i(texLocation, 0);
        sAnchorU = gl->glGetUniformLocation(sProgramId, "anchor");
        sCellSizeU = gl->glGetUniformLocation(sProgramId, "cellSize");
        sBorderU = gl->glGetUniformLocation(sProgramId, "border");
        sColorU = gl->glGetUniformLocation(sProgramId, "color");
        sInvertU = gl->glGetUniformLocation(sProgramId, "invert");
        sMixU = gl->glGetUniformLocation(sProgramId, "mixOriginal");
        sInitialized = true;
    }

    renderTools.requestTargetFbo().bind(gl);
    gl->glClear(GL_COLOR_BUFFER_BIT);

    gl->glUseProgram(sProgramId);

    const QPointF texAnchor = anchorInTexture(renderTools.fGlobalRect);
    gl->glUniform2f(sAnchorU, toSkScalar(texAnchor.x()),
                    toSkScalar(texAnchor.y()));
    gl->glUniform2f(sCellSizeU, toSkScalar(mSizeW), toSkScalar(mSizeH));
    gl->glUniform1f(sBorderU, toSkScalar(mBorder));
    gl->glUniform4f(sColorU, mColor.redF(), mColor.greenF(),
                    mColor.blueF(), mColor.alphaF());
    gl->glUniform1f(sInvertU, toSkScalar(mInvert));
    gl->glUniform1f(sMixU, toSkScalar(mMix));

    gl->glActiveTexture(GL_TEXTURE0);
    renderTools.getSrcTexture().bind(gl);

    gl->glBindVertexArray(renderTools.getSquareVAO());
    gl->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    renderTools.swapTextures();
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

    // anchor in content-rect UV -> bitmap pixels; the bitmap may carry
    // margins around the content (base margin + chained effect margins)
    qreal ax = mAnchor.x() * imgWidth;
    qreal ay = mAnchor.y() * imgHeight;
    if(mContentAbs.width() > 0. && mContentAbs.height() > 0.) {
        const QPointF off(mContentAbs.left() - data.fPos.x(),
                          mContentAbs.top() - data.fPos.y());
        ax = off.x() + mAnchor.x() * mContentAbs.width();
        ay = off.y() + mAnchor.y() * mContentAbs.height();
    }
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

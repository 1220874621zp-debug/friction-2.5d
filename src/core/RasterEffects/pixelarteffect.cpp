/*
#
# Friction - https://friction.graphics
#
# Copyright (c) Ole-André Rodlie and contributors
#
# See 'README.md' for more information.
#
*/

#include "pixelarteffect.h"
#include "gpurendertools.h"
#include "openglrastereffectcaller.h"
#include "Animators/qrealanimator.h"
#include "appsupport.h"

PixelArtEffect::PixelArtEffect() :
    RasterEffect("pixelArt",
                 AppSupport::getRasterEffectHardwareSupport("PixelArt",
                                                            HardwareSupport::gpuPreffered),
                 true,
                 RasterEffectType::PIXEL_ART)
{
    mPixelSize = enve::make_shared<QrealAnimator>(8.0, 1.0, 64.0, 1.0, "size");
    ca_addChild(mPixelSize);

    mPaletteSize = enve::make_shared<QrealAnimator>(16.0, 2.0, 256.0, 1.0, "levels");
    ca_addChild(mPaletteSize);

    mDither = enve::make_shared<QrealAnimator>(15.0, 0.0, 100.0, 1.0, "randomness");
    ca_addChild(mDither);

    mEdgeSharpness = enve::make_shared<QrealAnimator>(20.0, 0.0, 100.0, 1.0, "edge");
    ca_addChild(mEdgeSharpness);

    mSaturation = enve::make_shared<QrealAnimator>(120.0, 0.0, 200.0, 1.0, "saturation");
    ca_addChild(mSaturation);

    mScanline = enve::make_shared<QrealAnimator>(10.0, 0.0, 100.0, 1.0, "line count");
    ca_addChild(mScanline);

    mChromatic = enve::make_shared<QrealAnimator>(5.0, 0.0, 50.0, 0.5, "chromatic");
    ca_addChild(mChromatic);
}

class PixelArtEffectCaller : public OpenGLRasterEffectCaller {
public:
    PixelArtEffectCaller(const HardwareSupport hwSupport,
                         qreal pixelSize,
                         qreal paletteSize,
                         qreal dither,
                         qreal edgeSharpness,
                         qreal saturation,
                         qreal scanline,
                         qreal chromatic) :
        OpenGLRasterEffectCaller(sInitialized, sProgramId,
                                 ":/shaders/pixelarteffect.frag",
                                 hwSupport),
        mPixelSize(pixelSize),
        mPaletteSize(paletteSize),
        mDither(dither),
        mEdgeSharpness(edgeSharpness),
        mSaturation(saturation),
        mScanline(scanline),
        mChromatic(chromatic) {}

    void processCpu(CpuRenderTools& renderTools, const CpuRenderData& data) override;
protected:
    void iniVars(QGL33 * const gl) const override {
        sPixelSizeU = gl->glGetUniformLocation(sProgramId, "pixelSize");
        sPaletteSizeU = gl->glGetUniformLocation(sProgramId, "paletteSize");
        sDitherU = gl->glGetUniformLocation(sProgramId, "dither");
        sEdgeSharpnessU = gl->glGetUniformLocation(sProgramId, "edgeSharpness");
        sSaturationU = gl->glGetUniformLocation(sProgramId, "saturation");
        sScanlineU = gl->glGetUniformLocation(sProgramId, "scanline");
        sChromaticU = gl->glGetUniformLocation(sProgramId, "chromatic");
    }

    void setVars(QGL33 * const gl) const override {
        gl->glUseProgram(sProgramId);
        gl->glUniform1f(sPixelSizeU, toSkScalar(mPixelSize));
        gl->glUniform1f(sPaletteSizeU, toSkScalar(mPaletteSize));
        gl->glUniform1f(sDitherU, toSkScalar(mDither));
        gl->glUniform1f(sEdgeSharpnessU, toSkScalar(mEdgeSharpness));
        gl->glUniform1f(sSaturationU, toSkScalar(mSaturation));
        gl->glUniform1f(sScanlineU, toSkScalar(mScanline));
        gl->glUniform1f(sChromaticU, toSkScalar(mChromatic));
    }
private:
    static bool sInitialized;
    static GLuint sProgramId;

    static GLint sPixelSizeU;
    static GLint sPaletteSizeU;
    static GLint sDitherU;
    static GLint sEdgeSharpnessU;
    static GLint sSaturationU;
    static GLint sScanlineU;
    static GLint sChromaticU;

    const qreal mPixelSize;
    const qreal mPaletteSize;
    const qreal mDither;
    const qreal mEdgeSharpness;
    const qreal mSaturation;
    const qreal mScanline;
    const qreal mChromatic;
};

bool PixelArtEffectCaller::sInitialized = false;
GLuint PixelArtEffectCaller::sProgramId = 0;

GLint PixelArtEffectCaller::sPixelSizeU = -1;
GLint PixelArtEffectCaller::sPaletteSizeU = -1;
GLint PixelArtEffectCaller::sDitherU = -1;
GLint PixelArtEffectCaller::sEdgeSharpnessU = -1;
GLint PixelArtEffectCaller::sSaturationU = -1;
GLint PixelArtEffectCaller::sScanlineU = -1;
GLint PixelArtEffectCaller::sChromaticU = -1;

stdsptr<RasterEffectCaller> PixelArtEffect::getEffectCaller(
        const qreal relFrame, const qreal resolution,
        const qreal influence, BoxRenderData * const data) const {
    Q_UNUSED(resolution)
    Q_UNUSED(data)

    const qreal pixelSize = mPixelSize->getEffectiveValue(relFrame);
    const qreal paletteSize = mPaletteSize->getEffectiveValue(relFrame);
    const qreal dither = mDither->getEffectiveValue(relFrame);
    const qreal edgeSharpness = mEdgeSharpness->getEffectiveValue(relFrame);
    const qreal saturation = mSaturation->getEffectiveValue(relFrame);
    const qreal scanline = mScanline->getEffectiveValue(relFrame);
    const qreal chromatic = mChromatic->getEffectiveValue(relFrame) * influence;

    return enve::make_shared<PixelArtEffectCaller>(
                instanceHwSupport(), pixelSize, paletteSize, dither,
                edgeSharpness, saturation, scanline, chromatic);
}

void PixelArtEffectCaller::processCpu(CpuRenderTools& renderTools, const CpuRenderData& data) {
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

    const int bSize = std::max(1, qRound(mPixelSize));
    const qreal levels = std::max(2.0, mPaletteSize);

    for(int yi = yMin; yi <= yMax; yi++) {
        auto dst = static_cast<uchar*>(dstBtmp.getAddr(0, yi - yMin));
        const int by = (yi / bSize) * bSize;

        for(int xi = xMin; xi <= xMax; xi++) {
            const int bx = (xi / bSize) * bSize;
            const auto p = static_cast<const uchar*>(srcBtmp.getAddr(std::min(imgWidth - 1, bx), std::min(imgHeight - 1, by)));

            const uchar r = p[0];
            const uchar g = p[1];
            const uchar b = p[2];
            const uchar a = p[3];

            qreal qR = std::floor((r / 255.0) * (levels - 1.0) + 0.5) / (levels - 1.0);
            qreal qG = std::floor((g / 255.0) * (levels - 1.0) + 0.5) / (levels - 1.0);
            qreal qB = std::floor((b / 255.0) * (levels - 1.0) + 0.5) / (levels - 1.0);

            *dst++ = static_cast<uchar>(qR * 255.0);
            *dst++ = static_cast<uchar>(qG * 255.0);
            *dst++ = static_cast<uchar>(qB * 255.0);
            *dst++ = a;
        }
    }
}

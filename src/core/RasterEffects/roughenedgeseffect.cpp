/*
#
# Friction - https://friction.graphics
#
# Copyright (c) Ole-André Rodlie and contributors
#
# See 'README.md' for more information.
#
*/

#include "roughenedgeseffect.h"
#include "gpurendertools.h"
#include "openglrastereffectcaller.h"
#include "Animators/qrealanimator.h"
#include "appsupport.h"

RoughenEdgesEffect::RoughenEdgesEffect() :
    RasterEffect(QObject::tr("Roughen Edges"),
                 AppSupport::getRasterEffectHardwareSupport("RoughenEdges",
                                                            HardwareSupport::gpuPreffered),
                 true,
                 RasterEffectType::ROUGHEN_EDGES)
{
    mBorder = enve::make_shared<QrealAnimator>(15.0, 0.0, 200.0, 1.0, "border");
    ca_addChild(mBorder);

    mEdgeSharpness = enve::make_shared<QrealAnimator>(5.0, 0.1, 20.0, 0.5, "edge sharpness");
    ca_addChild(mEdgeSharpness);

    mScale = enve::make_shared<QrealAnimator>(15.0, 1.0, 100.0, 0.5, "scale");
    ca_addChild(mScale);

    mComplexity = enve::make_shared<QrealAnimator>(3.0, 1.0, 5.0, 1.0, "complexity");
    ca_addChild(mComplexity);

    mEvolution = enve::make_shared<QrealAnimator>(0.0, 0.0, 100.0, 1.0, "evolution");
    ca_addChild(mEvolution);
}

class RoughenEdgesEffectCaller : public OpenGLRasterEffectCaller {
public:
    RoughenEdgesEffectCaller(const HardwareSupport hwSupport,
                             const qreal border,
                             const qreal edgeSharpness,
                             const qreal scale,
                             const qreal complexity,
                             const qreal evolution) :
        OpenGLRasterEffectCaller(sInitialized, sProgramId,
                                 ":/shaders/roughenedgeseffect.frag",
                                 hwSupport),
        mBorder(border),
        mEdgeSharpness(edgeSharpness),
        mScale(scale),
        mComplexity(complexity),
        mEvolution(evolution) {}

    void processCpu(CpuRenderTools& renderTools,
                    const CpuRenderData& data) override
    {
        const auto& srcBtmp = renderTools.fSrcBtmp;
        auto& dstBtmp = renderTools.fDstBtmp;

        if (srcBtmp.empty() || srcBtmp.getPixels() == nullptr ||
            dstBtmp.empty() || dstBtmp.getPixels() == nullptr) {
            return;
        }

        const int imgWidth = srcBtmp.width();
        const int imgHeight = srcBtmp.height();
        if (imgWidth <= 0 || imgHeight <= 0) return;

        const int xMin = std::max(0, data.fTexTile.left());
        const int xMax = std::min((int)data.fTexTile.right(), imgWidth - 1);
        const int yMin = std::max(0, data.fTexTile.top());
        const int yMax = std::min((int)data.fTexTile.bottom(), imgHeight - 1);

        // border 0 is an exact passthrough, like the shader
        if (mBorder <= 0.001) {
            for (int yi = yMin; yi <= yMax; yi++) {
                auto dst = static_cast<uchar*>(dstBtmp.getAddr(0, yi - yMin));
                for (int xi = xMin; xi <= xMax; xi++) {
                    const auto src = static_cast<const uchar*>(
                                srcBtmp.getAddr(xi, yi));
                    const int offset = (xi - xMin) * 4;
                    dst[offset + 0] = src[0];
                    dst[offset + 1] = src[1];
                    dst[offset + 2] = src[2];
                    dst[offset + 3] = src[3];
                }
            }
            return;
        }

        // CPU mirror of roughenedgeseffect.frag: gradient fbm
        // displacement plus a noise-threshold alpha erosion that
        // chews the silhouette into a ragged fringe
        const auto hash2 = [](const qreal x, const qreal y) {
            const qreal h = std::sin(x * 127.1 + y * 311.7) * 43758.5453123;
            return h - std::floor(h);
        };
        const auto gnoise = [&hash2](const qreal x, const qreal y) {
            // perlin-style gradient noise in [-1, 1]
            const auto grad = [&hash2](const int ix, const int iy,
                                       const qreal dx, const qreal dy) {
                const qreal a = hash2(ix, iy) * 6.28318530718;
                return std::cos(a) * dx + std::sin(a) * dy;
            };
            const int ix = int(std::floor(x));
            const int iy = int(std::floor(y));
            const qreal fx = x - ix;
            const qreal fy = y - iy;
            const qreal ux = fx * fx * (3. - 2. * fx);
            const qreal uy = fy * fy * (3. - 2. * fy);
            const qreal n00 = grad(ix, iy, fx, fy);
            const qreal n10 = grad(ix + 1, iy, fx - 1., fy);
            const qreal n01 = grad(ix, iy + 1, fx, fy - 1.);
            const qreal n11 = grad(ix + 1, iy + 1, fx - 1., fy - 1.);
            const qreal a = n00 + (n10 - n00) * ux;
            const qreal b = n01 + (n11 - n01) * ux;
            return a + (b - a) * uy;
        };
        const int oct = qBound(1, int(mComplexity), 5);
        const qreal sc = std::max(mScale * 2., 4.);
        const qreal evX = mEvolution * 0.1;
        const qreal evY = mEvolution * 0.07;
        const auto fbm = [&gnoise, oct](const qreal x, const qreal y) {
            qreal val = 0.;
            qreal amp = 0.5;
            qreal freq = 1.;
            for (int i = 0; i < oct; i++) {
                val += amp * gnoise(x * freq, y * freq);
                freq *= 2.;
                amp *= 0.5;
            }
            return val;
        };
        const qreal sharpness = std::max(mEdgeSharpness, 0.5);
        const qreal edgeW = std::max(0.04, 1.5 / sharpness);
        const auto smooth01 = [](const qreal e0, const qreal e1,
                                 const qreal x) {
            const qreal t = qBound(0., (x - e0) / std::max(e1 - e0, 0.0001), 1.);
            return t * t * (3. - 2. * t);
        };

        const qreal dispBase = mBorder * 0.003 * std::max(imgWidth, imgHeight);
        const qreal thrBase = mBorder * 0.006;
        for (int yi = yMin; yi <= yMax; yi++) {
            auto dst = static_cast<uchar*>(dstBtmp.getAddr(0, yi - yMin));
            for (int xi = xMin; xi <= xMax; xi++) {
                const qreal n = fbm(xi / sc + evX, yi / sc + evY);
                // (n, -n) displacement, uv-domain in the shader ->
                // pixels here
                const int sx = qBound(0, xi + qRound(n * dispBase),
                                      imgWidth - 1);
                const int sy = qBound(0, yi + qRound(-n * dispBase),
                                      imgHeight - 1);
                const auto src = static_cast<const uchar*>(
                            srcBtmp.getAddr(sx, sy));
                const qreal a = src[3] / 255.;
                const qreal threshold = 0.5 - thrBase * n;
                const qreal eroded = smooth01(threshold - edgeW,
                                              threshold + edgeW, a);
                const int outA = qRound(eroded * src[3]);
                const int offset = (xi - xMin) * 4;
                dst[offset + 0] = static_cast<uchar>(
                            qRound(src[0] * eroded));
                dst[offset + 1] = static_cast<uchar>(
                            qRound(src[1] * eroded));
                dst[offset + 2] = static_cast<uchar>(
                            qRound(src[2] * eroded));
                dst[offset + 3] = static_cast<uchar>(outA);
            }
        }
    }

protected:
    void iniVars(QGL33 * const gl) const override {
        sBorderU = gl->glGetUniformLocation(sProgramId, "border");
        sEdgeSharpnessU = gl->glGetUniformLocation(sProgramId, "edgeSharpness");
        sScaleU = gl->glGetUniformLocation(sProgramId, "scale");
        sComplexityU = gl->glGetUniformLocation(sProgramId, "complexity");
        sEvolutionU = gl->glGetUniformLocation(sProgramId, "evolution");
    }

    void setVars(QGL33 * const gl) const override {
        gl->glUseProgram(sProgramId);
        gl->glUniform1f(sBorderU, toSkScalar(mBorder));
        gl->glUniform1f(sEdgeSharpnessU, toSkScalar(mEdgeSharpness));
        gl->glUniform1f(sScaleU, toSkScalar(mScale));
        gl->glUniform1f(sComplexityU, toSkScalar(mComplexity));
        gl->glUniform1f(sEvolutionU, toSkScalar(mEvolution));
    }

private:
    static bool sInitialized;
    static GLuint sProgramId;

    static GLint sBorderU;
    static GLint sEdgeSharpnessU;
    static GLint sScaleU;
    static GLint sComplexityU;
    static GLint sEvolutionU;

    const qreal mBorder;
    const qreal mEdgeSharpness;
    const qreal mScale;
    const qreal mComplexity;
    const qreal mEvolution;
};

bool RoughenEdgesEffectCaller::sInitialized = false;
GLuint RoughenEdgesEffectCaller::sProgramId = 0;
GLint RoughenEdgesEffectCaller::sBorderU = 0;
GLint RoughenEdgesEffectCaller::sEdgeSharpnessU = 0;
GLint RoughenEdgesEffectCaller::sScaleU = 0;
GLint RoughenEdgesEffectCaller::sComplexityU = 0;
GLint RoughenEdgesEffectCaller::sEvolutionU = 0;

stdsptr<RasterEffectCaller> RoughenEdgesEffect::getEffectCaller(
        const qreal relFrame,
        const qreal resolution,
        const qreal influence,
        BoxRenderData * const data) const
{
    Q_UNUSED(resolution)
    Q_UNUSED(data)
    const auto hwSupport = instanceHwSupport();
    const auto border = mBorder->getEffectiveValue(relFrame) * influence;
    const auto edgeSharpness = mEdgeSharpness->getEffectiveValue(relFrame);
    const auto scale = mScale->getEffectiveValue(relFrame);
    const auto complexity = mComplexity->getEffectiveValue(relFrame);
    const auto evolution = mEvolution->getEffectiveValue(relFrame);
    return enve::make_shared<RoughenEdgesEffectCaller>(
                hwSupport, border, edgeSharpness, scale, complexity, evolution);
}

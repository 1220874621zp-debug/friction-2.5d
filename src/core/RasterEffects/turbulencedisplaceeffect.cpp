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

#include "turbulencedisplaceeffect.h"

#include "glhelpers.h"
#include "gpurendertools.h"
#include "cpurendertools.h"
#include "exceptions.h"
#include "simplemath.h"
#include "appsupport.h"

#include <algorithm>
#include <cmath>
#include <random>

TurbulentDisplaceEffect::TurbulentDisplaceEffect() :
    RasterEffect(QObject::tr("湍流置换 (Turbulent Displace)"),
                 AppSupport::getRasterEffectHardwareSupport("TurbulentDisplace",
                                                            HardwareSupport::gpuPreffered),
                 true,
                 RasterEffectType::TURBULENT_DISPLACE)
{
    mDisplacement = enve::make_shared<QrealAnimator>(50.0, -500.0, 500.0, 1.0,
                                                     QObject::tr("置换量"));
    ca_addChild(mDisplacement);
    connect(mDisplacement.get(), &QrealAnimator::effectiveValueChanged,
            this, &RasterEffect::forcedMarginChanged);
    ca_setGUIProperty(mDisplacement.get());

    mSize = enve::make_shared<QrealAnimator>(100.0, 1.0, 2000.0, 1.0,
                                             QObject::tr("大小"));
    ca_addChild(mSize);

    mComplexity = enve::make_shared<QrealAnimator>(2.0, 1.0, 8.0, 1.0,
                                                   QObject::tr("复杂度"));
    ca_addChild(mComplexity);

    mEvolution = enve::make_shared<QrealAnimator>(0.0, -36000.0, 36000.0, 1.0,
                                                  QObject::tr("演化"));
    ca_addChild(mEvolution);

    mEvolveSpeed = enve::make_shared<QrealAnimator>(0.0, -100.0, 100.0, 0.1,
                                                    QObject::tr("演化速度"));
    ca_addChild(mEvolveSpeed);

    mSeed = enve::make_shared<QrealAnimator>(0.0, 0.0, 9999.0, 1.0,
                                             QObject::tr("随机种子"));
    ca_addChild(mSeed);

    const auto pins = QStringList() <<
            QObject::tr("全部") <<
            QObject::tr("水平") <<
            QObject::tr("垂直") <<
            QObject::tr("无");
    mPinning = enve::make_shared<ComboBoxProperty>(QObject::tr("固定"), pins);
    ca_addChild(mPinning);

    mLoopEvo = enve::make_shared<BoolAnimator>(QObject::tr("循环演化"));
    mLoopEvo->setCurrentBoolValue(false);
    ca_addChild(mLoopEvo);

    mLoopCycle = enve::make_shared<QrealAnimator>(1.0, 0.1, 100.0, 0.1,
                                                  QObject::tr("循环周期(圈)"));
    ca_addChild(mLoopCycle);
}

QMargins TurbulentDisplaceEffect::getMargin() const {
    return QMargins() + qCeil(qAbs(mDisplacement->getEffectiveValue()));
}

// classic 3D gradient noise (Ken Perlin reference implementation),
// permutation table shuffled per random seed
namespace {
class Perlin3 {
public:
    explicit Perlin3(const unsigned seed) {
        for(int i = 0; i < 256; i++) mPerm[i] = i;
        std::mt19937 gen(seed);
        std::shuffle(std::begin(mPerm), std::begin(mPerm) + 256, gen);
        for(int i = 0; i < 256; i++) mPerm[256 + i] = mPerm[i];
    }

    // roughly [-1, 1]
    double noise(const double x, const double y, const double z) const {
        const int xi = qFloor(x), yi = qFloor(y), zi = qFloor(z);
        const double xf = x - xi, yf = y - yi, zf = z - zi;
        const double u = fade(xf), v = fade(yf), w = fade(zf);
        const int X = xi & 255, Y = yi & 255, Z = zi & 255;
        const int A = mPerm[X] + Y,     AA = mPerm[A] + Z,     AB = mPerm[A + 1] + Z;
        const int B = mPerm[X + 1] + Y, BA = mPerm[B] + Z,     BB = mPerm[B + 1] + Z;
        const double x1 = lerp(u, grad(mPerm[AA],     xf,      yf,      zf),
                                  grad(mPerm[BA],     xf - 1., yf,      zf));
        const double x2 = lerp(u, grad(mPerm[AB],     xf,      yf - 1., zf),
                                  grad(mPerm[BB],     xf - 1., yf - 1., zf));
        const double y1 = lerp(v, x1, x2);
        const double x3 = lerp(u, grad(mPerm[AA + 1], xf,      yf,      zf - 1.),
                                  grad(mPerm[BA + 1], xf - 1., yf,      zf - 1.));
        const double x4 = lerp(u, grad(mPerm[AB + 1], xf,      yf - 1., zf - 1.),
                                  grad(mPerm[BB + 1], xf - 1., yf - 1., zf - 1.));
        const double y2 = lerp(v, x3, x4);
        return lerp(w, y1, y2);
    }
private:
    static double fade(const double t) {
        return t*t*t*(t*(t*6. - 15.) + 10.);
    }
    static double lerp(const double t, const double a, const double b) {
        return a + t*(b - a);
    }
    static double grad(const int hash, const double x,
                       const double y, const double z) {
        const int h = hash & 15;
        const double uu = h < 8 ? x : y;
        const double vv = h < 4 ? y : (h == 12 || h == 14 ? x : z);
        return ((h & 1) == 0 ? uu : -uu) + ((h & 2) == 0 ? vv : -vv);
    }
    int mPerm[512];
};

// fractal sum of gradient noise octaves, signed, roughly [-1, 1];
// per-octave offsets decorrelate the octaves
double turbField(const Perlin3& nz, const double x, const double y,
                 const double z, const int oct) {
    double sum = 0., amp = 1., norm = 0., freq = 1.;
    for(int o = 0; o < oct; o++) {
        const double n = nz.noise(x*freq + o*17.31, y*freq - o*29.13,
                                  z*0.7 + o*11.73);
        sum += n*amp;
        norm += amp;
        amp *= 0.5;
        freq *= 2.0;
    }
    return norm > 0. ? sum/norm : 0.;
}

// cyclic evolution: crossfade two phase-shifted fields over the cycle
// so the last frame of a revolution matches the first (mix by phase)
double evoField(const Perlin3& nz, const double x, const double y,
                const double z, const int oct,
                const bool loop, const double cycle) {
    if(!loop) return turbField(nz, x, y, z, oct);
    const double len = 4.0*std::max(0.01, cycle);
    const double tt = z/len;
    const double t = tt - qFloor(tt);
    const double a = turbField(nz, x, y, t*len, oct);
    const double b = turbField(nz, x, y, (t - 1.0)*len, oct);
    return a + t*(b - a);
}
}

class TurbulentDisplaceEffectCaller : public RasterEffectCaller {
public:
    TurbulentDisplaceEffectCaller(const HardwareSupport hwSupport,
                                  const qreal amount,
                                  const qreal size,
                                  const int octaves,
                                  const qreal zPos,
                                  const qreal seed,
                                  const int pinMode,
                                  const bool loopEvo,
                                  const qreal cycle,
                                  const QMargins& margin) :
        RasterEffectCaller(hwSupport, true, margin),
        mAmount(static_cast<float>(amount)),
        mSize(static_cast<float>(size)),
        mOctaves(octaves),
        mZPos(static_cast<float>(zPos)),
        mSeed(static_cast<float>(seed)),
        mPinMode(pinMode),
        mLoopEvo(loopEvo),
        mCycle(static_cast<float>(cycle)) {}

    void processGpu(QGL33 * const gl, GpuRenderTools &renderTools) override;
    void processCpu(CpuRenderTools& renderTools,
                    const CpuRenderData& data) override;
private:
    static bool sInitialized;
    static GLuint sProgramId;

    static GLint sAmountU;
    static GLint sSizeU;
    static GLint sOctavesU;
    static GLint sZPosU;
    static GLint sSeedU;
    static GLint sPinModeU;
    static GLint sLoopEvoU;
    static GLint sCycleU;
    static GLint sImgSizeU;

    const float mAmount;
    const float mSize;
    const int mOctaves;
    const float mZPos;
    const float mSeed;
    const int mPinMode;
    const bool mLoopEvo;
    const float mCycle;
};

bool TurbulentDisplaceEffectCaller::sInitialized = false;
GLuint TurbulentDisplaceEffectCaller::sProgramId = 0;

GLint TurbulentDisplaceEffectCaller::sAmountU = -1;
GLint TurbulentDisplaceEffectCaller::sSizeU = -1;
GLint TurbulentDisplaceEffectCaller::sOctavesU = -1;
GLint TurbulentDisplaceEffectCaller::sZPosU = -1;
GLint TurbulentDisplaceEffectCaller::sSeedU = -1;
GLint TurbulentDisplaceEffectCaller::sPinModeU = -1;
GLint TurbulentDisplaceEffectCaller::sLoopEvoU = -1;
GLint TurbulentDisplaceEffectCaller::sCycleU = -1;
GLint TurbulentDisplaceEffectCaller::sImgSizeU = -1;

stdsptr<RasterEffectCaller> TurbulentDisplaceEffect::getEffectCaller(
        const qreal relFrame, const qreal resolution,
        const qreal influence, BoxRenderData * const data) const {
    Q_UNUSED(data)

    const qreal disp = mDisplacement->getEffectiveValue(relFrame)*influence;
    if(isZero4Dec(disp)) return nullptr;
    const qreal amount = disp*resolution;
    const qreal size = std::max(0.5, mSize->getEffectiveValue(relFrame)*resolution);
    const int oct = qBound(1, qRound(mComplexity->getEffectiveValue(relFrame)), 8);
    const qreal evolution = mEvolution->getEffectiveValue(relFrame);
    const qreal speed = mEvolveSpeed->getEffectiveValue(relFrame);
    // 4 noise-units per evolution revolution, so cycle=1 loop length
    // matches the shader's cyclic crossfade window
    const qreal zPos = evolution*(4.0/360.0) + relFrame*speed*0.05;
    const qreal seed = mSeed->getEffectiveValue(relFrame);
    const int pinMode = mPinning->getCurrentValue();
    const bool loop = mLoopEvo->getBoolValue(relFrame);
    const qreal cycle = mLoopCycle->getEffectiveValue(relFrame);

    const QMargins margin = QMargins() + qCeil(qAbs(amount));

    return enve::make_shared<TurbulentDisplaceEffectCaller>(
                instanceHwSupport(), amount, size, oct, zPos,
                seed, pinMode, loop, cycle, margin);
}

void TurbulentDisplaceEffectCaller::processGpu(QGL33 * const gl,
                                               GpuRenderTools &renderTools) {
    renderTools.switchToOpenGL(gl);

    if(!sInitialized) {
        try {
            gIniProgram(gl, sProgramId, GL_TEXTURED_VERT,
                        ":/shaders/turbulencedisplaceeffect.frag");
        } catch(...) {
            RuntimeThrow("Could not initialize a program for "
                         "'turbulencedisplaceeffect.frag'");
        }
        gl->glUseProgram(sProgramId);
        const auto texLocation = gl->glGetUniformLocation(sProgramId, "tex");
        gl->glUniform1i(texLocation, 0);
        sAmountU = gl->glGetUniformLocation(sProgramId, "amount");
        sSizeU = gl->glGetUniformLocation(sProgramId, "size");
        sOctavesU = gl->glGetUniformLocation(sProgramId, "octaves");
        sZPosU = gl->glGetUniformLocation(sProgramId, "zPos");
        sSeedU = gl->glGetUniformLocation(sProgramId, "seed");
        sPinModeU = gl->glGetUniformLocation(sProgramId, "pinMode");
        sLoopEvoU = gl->glGetUniformLocation(sProgramId, "loopEvo");
        sCycleU = gl->glGetUniformLocation(sProgramId, "cycle");
        sImgSizeU = gl->glGetUniformLocation(sProgramId, "imgSize");
        sInitialized = true;
    }

    renderTools.requestTargetFbo().bind(gl);
    gl->glClear(GL_COLOR_BUFFER_BIT);

    gl->glUseProgram(sProgramId);

    gl->glUniform1f(sAmountU, mAmount);
    gl->glUniform1f(sSizeU, mSize);
    gl->glUniform1f(sOctavesU, static_cast<float>(mOctaves));
    gl->glUniform1f(sZPosU, mZPos);
    gl->glUniform1f(sSeedU, mSeed);
    gl->glUniform1i(sPinModeU, mPinMode);
    gl->glUniform1f(sLoopEvoU, mLoopEvo ? 1.0f : 0.0f);
    gl->glUniform1f(sCycleU, mCycle);

    gl->glActiveTexture(GL_TEXTURE0);
    const auto& srcTex = renderTools.getSrcTexture();
    // actual source texture size (may include the effect margin), the
    // noise domain and the pinning weights are anchored to it
    gl->glUniform2f(sImgSizeU, std::max(1, srcTex.fWidth),
                    std::max(1, srcTex.fHeight));
    srcTex.bind(gl);

    gl->glBindVertexArray(renderTools.getSquareVAO());
    gl->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    renderTools.swapTextures();
}

void TurbulentDisplaceEffectCaller::processCpu(CpuRenderTools& renderTools,
                                               const CpuRenderData& data) {
    const auto& srcBtmp = renderTools.fSrcBtmp;
    const auto& dstBtmp = renderTools.fDstBtmp;

    if(srcBtmp.empty() || srcBtmp.getPixels() == nullptr ||
       dstBtmp.empty() || dstBtmp.getPixels() == nullptr) return;

    const int imgWidth = srcBtmp.width();
    const int imgHeight = srcBtmp.height();
    if(imgWidth <= 0 || imgHeight <= 0) return;

    const int xMin = std::max(0, data.fTexTile.left());
    const int xMax = std::min(static_cast<int>(data.fTexTile.right()) - 1,
                              imgWidth - 1);
    const int yMin = std::max(0, data.fTexTile.top());
    const int yMax = std::min(static_cast<int>(data.fTexTile.bottom()) - 1,
                              imgHeight - 1);

    const Perlin3 nz(static_cast<unsigned>(qBound(0, qRound(mSeed), 9999)));
    const double zBase = mZPos + qMax(0.f, mSeed)*0.917;
    const double size = std::max(0.5f, mSize);
    const double invW = 1.0/imgWidth;
    const double invH = 1.0/imgHeight;
    const double sdx = mSeed*7.31, sdy = -mSeed*3.71;
    const bool pinAll = mPinMode == 0;
    const bool pinH = pinAll || mPinMode == 1;
    const bool pinV = pinAll || mPinMode == 2;
    const double pi = 3.14159265358979323846;

    const auto sampleBilinear = [&](const double sx, const double sy,
                                    uchar* dst) {
        const double cx = qBound(0.0, sx, imgWidth - 1.0);
        const double cy = qBound(0.0, sy, imgHeight - 1.0);
        const int x0 = static_cast<int>(cx);
        const int y0 = static_cast<int>(cy);
        const int x1 = std::min(x0 + 1, imgWidth - 1);
        const int y1 = std::min(y0 + 1, imgHeight - 1);
        const double fx = cx - x0, fy = cy - y0;
        const auto p00 = static_cast<const uchar*>(srcBtmp.getAddr(x0, y0));
        const auto p10 = static_cast<const uchar*>(srcBtmp.getAddr(x1, y0));
        const auto p01 = static_cast<const uchar*>(srcBtmp.getAddr(x0, y1));
        const auto p11 = static_cast<const uchar*>(srcBtmp.getAddr(x1, y1));
        for(int c = 0; c < 4; c++) {
            const double top = p00[c] + fx*(p10[c] - p00[c]);
            const double bot = p01[c] + fx*(p11[c] - p01[c]);
            dst[c] = static_cast<uchar>(
                        qBound(0.0, top + fy*(bot - top), 255.0) + 0.5);
        }
    };

    for(int yi = yMin; yi <= yMax; yi++) {
        auto dst = static_cast<uchar*>(dstBtmp.getAddr(0, yi - yMin));
        const double v = (yi + 0.5)*invH;
        double wy = 1.0;
        if(pinV) wy = std::sin(pi*v);

        for(int xi = xMin; xi <= xMax; xi++) {
            const double u = (xi + 0.5)*invW;
            double w = wy;
            if(pinH) w *= std::sin(pi*u);

            const double nx = xi/size + sdx;
            const double ny = yi/size + sdy;
            const double fx = evoField(nz, nx, ny, zBase, mOctaves,
                                       mLoopEvo, mCycle);
            const double fy = evoField(nz, nx + 137.7, ny - 91.3, zBase,
                                       mOctaves, mLoopEvo, mCycle);

            sampleBilinear(xi + fx*mAmount*w, yi + fy*mAmount*w, dst);
            dst += 4;
        }
    }
}

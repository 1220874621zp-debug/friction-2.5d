/*
#
# Friction - https://friction.graphics
#
# Copyright (c) Ole-André Roddie and contributors
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

#include "curveseffect.h"
#include "gpurendertools.h"
#include "openglrastereffectcaller.h"

#include "Animators/qrealanimator.h"
#include "Properties/comboboxproperty.h"
#include "appsupport.h"

#include <cmath>
#include <cstdint>

namespace {

// normalized anchor input positions: 0 / 64 / 128 / 192 / 255
const qreal sInX[CurvesChannelAnimator::Count] = {
    0.0, 64.0 / 255.0, 128.0 / 255.0, 192.0 / 255.0, 1.0
};

// Fritsch-Carlson monotone cubic through the five anchors. The
// tangents are secant-limited so the spline never overshoots a
// neighbouring anchor - steep edits can't invent banding halos.
// x/y live normalized 0..1; y comes in as output levels 0..255.
struct CurveSpline {
    qreal x[5];
    qreal y[5];
    qreal m[5];

    explicit CurveSpline(const qreal out[5]) {
        for (int i = 0; i < 5; i++) {
            x[i] = sInX[i];
            y[i] = qBound(0., out[i], 255.) / 255.;
        }
        qreal d[4];
        for (int i = 0; i < 4; i++) {
            d[i] = (y[i + 1] - y[i]) / (x[i + 1] - x[i]);
        }
        m[0] = d[0];
        m[4] = d[3];
        for (int i = 1; i < 4; i++) {
            if (d[i - 1] * d[i] <= 0.) {
                m[i] = 0.;
            } else {
                m[i] = 0.5 * (d[i - 1] + d[i]);
                const qreal lim = 3. * std::min(std::abs(d[i - 1]),
                                                std::abs(d[i]));
                if (std::abs(m[i]) > lim) {
                    m[i] = m[i] < 0. ? -lim : lim;
                }
            }
        }
    }

    // Hermite evaluation on the segment containing t
    qreal eval(qreal t) const {
        t = qBound(0., t, 1.);
        int k = 3;
        for (int i = 0; i < 4; i++) {
            if (t < x[i + 1] || i == 3) { k = i; break; }
        }
        const qreal h = x[k + 1] - x[k];
        const qreal u = (t - x[k]) / h;
        const qreal u2 = u * u;
        const qreal u3 = u2 * u;
        return (2. * u3 - 3. * u2 + 1.) * y[k]
             + (u3 - 2. * u2 + u) * h * m[k]
             + (-2. * u3 + 3. * u2) * y[k + 1]
             + (u3 - u2) * h * m[k + 1];
    }
};

}

qreal CurvesChannelAnimator::inputX(const int i) {
    return qBound(0, i, Count - 1) == i ? sInX[i] : sInX[0];
}

qreal CurvesChannelAnimator::defaultY(const int i) {
    static const qreal ys[Count] = { 0., 64., 128., 192., 255. };
    return qBound(0, i, Count - 1) == i ? ys[i] : ys[0];
}

void CurvesEffect::buildLUT(const qreal y[CurvesChannelAnimator::Count],
                            uint8_t lut[256])
{
    const CurveSpline spline(y);
    for (int v = 0; v < 256; v++) {
        const qreal out = spline.eval(v / 255.);
        lut[v] = static_cast<uint8_t>(qBound(0., out * 255. + 0.5, 255.));
    }
}

bool CurvesEffect::isIdentity(const qreal y[CurvesChannelAnimator::Count])
{
    for (int i = 0; i < CurvesChannelAnimator::Count; i++) {
        if (!qFuzzyIsNull(y[i] - CurvesChannelAnimator::defaultY(i))) {
            return false;
        }
    }
    return true;
}

CurvesChannelAnimator::CurvesChannelAnimator(const QString& name) :
    StaticComplexAnimator(name) {
    // the anchor names show as the expanded timeline rows and are
    // how the effect preview finds parameters by name
    static const char* sAnchorNames[Count] = {
        QT_TR_NOOP("黑场"), QT_TR_NOOP("暗部"), QT_TR_NOOP("中间调"),
        QT_TR_NOOP("亮部"), QT_TR_NOOP("白场")
    };
    for (int i = 0; i < Count; i++) {
        const auto anchor = enve::make_shared<QrealAnimator>(
                    defaultY(i), 0., 255., 1.,
                    QObject::tr(sAnchorNames[i]));
        ca_addChild(anchor);
    }
}

CurvesEffect::CurvesEffect() :
    RasterEffect(QObject::tr("曲线 (Curves)"),
                 AppSupport::getRasterEffectHardwareSupport(
                     "Curves", HardwareSupport::gpuPreffered),
                 true,
                 RasterEffectType::CURVES)
{
    const auto channels = QStringList() <<
                QObject::tr("RGB") <<
                QObject::tr("红") <<
                QObject::tr("绿") <<
                QObject::tr("蓝");
    mChannel = enve::make_shared<ComboBoxProperty>(
                QObject::tr("通道"), channels);
    ca_addChild(mChannel);

    // the wrapper names double as the timeline rows for the four
    // channels; the anchors inside are named by the GUI editor only
    const QStringList wrapNames = QStringList() <<
                QStringLiteral("RGB") << QStringLiteral("红") <<
                QStringLiteral("绿") << QStringLiteral("蓝");
    for (int i = 0; i < 4; i++) {
        mChannels[i] = enve::make_shared<CurvesChannelAnimator>(wrapNames[i]);
        ca_addChild(mChannels[i]);
    }
}

class CurvesEffectCaller : public OpenGLRasterEffectCaller {
public:
    CurvesEffectCaller(const HardwareSupport hwSupport,
                       const qreal y[4][5]) :
        OpenGLRasterEffectCaller(sInitialized, sProgramId,
                                 ":/shaders/curveseffect.frag",
                                 hwSupport)
    {
        // composite = master first, then the per-channel curve
        // (matching PS); the GPU samples a 66-point float table, the
        // CPU a 256-entry byte LUT chain
        const CurveSpline master(y[0]);
        const CurveSpline chans[3] = { CurveSpline(y[1]),
                                       CurveSpline(y[2]),
                                       CurveSpline(y[3]) };
        for (int i = 0; i < 66; i++) {
            const qreal t = i / 65.;
            const qreal mid = master.eval(t);
            mLut[i][0] = static_cast<float>(chans[0].eval(mid));
            mLut[i][1] = static_cast<float>(chans[1].eval(mid));
            mLut[i][2] = static_cast<float>(chans[2].eval(mid));
        }

        uint8_t mLut8[256];
        uint8_t clut[3][256];
        CurvesEffect::buildLUT(y[0], mLut8);
        for (int c = 0; c < 3; c++) {
            CurvesEffect::buildLUT(y[c + 1], clut[c]);
        }
        for (int v = 0; v < 256; v++) {
            const uint8_t m = mLut8[v];
            mComp[0][v] = clut[0][m];
            mComp[1][v] = clut[1][m];
            mComp[2][v] = clut[2][m];
        }
    }

    void processCpu(CpuRenderTools& renderTools,
                    const CpuRenderData& data);
protected:
    void iniVars(QGL33 * const gl) const {
        sLutU = gl->glGetUniformLocation(sProgramId, "lut");
    }

    void setVars(QGL33 * const gl) const {
        gl->glUseProgram(sProgramId);
        gl->glUniform3fv(sLutU, 66, &mLut[0][0]);
    }
private:
    static bool sInitialized;
    static GLuint sProgramId;

    static GLint sLutU;

    float mLut[66][3];
    uint8_t mComp[3][256];
};

bool CurvesEffectCaller::sInitialized = false;
GLuint CurvesEffectCaller::sProgramId = 0;

GLint CurvesEffectCaller::sLutU = -1;

stdsptr<RasterEffectCaller> CurvesEffect::getEffectCaller(
        const qreal relFrame, const qreal resolution,
        const qreal influence, BoxRenderData * const data) const
{
    Q_UNUSED(resolution)
    Q_UNUSED(influence)
    Q_UNUSED(data)

    qreal y[4][5];
    bool identity = true;
    for (int c = 0; c < 4; c++) {
        const auto wrap = mChannels[c];
        for (int i = 0; i < CurvesChannelAnimator::Count; i++) {
            const auto anchor = wrap->getAnchor(i);
            const qreal v = anchor ?
                        qBound(0., anchor->getEffectiveValue(relFrame), 255.) :
                        CurvesChannelAnimator::defaultY(i);
            y[c][i] = v;
            if (!qFuzzyIsNull(v - CurvesChannelAnimator::defaultY(i))) {
                identity = false;
            }
        }
    }

    // identity curve = no caller, AE-style passthrough
    if (identity) { return nullptr; }

    return enve::make_shared<CurvesEffectCaller>(
                instanceHwSupport(), y);
}

void CurvesEffectCaller::processCpu(CpuRenderTools& renderTools,
                                    const CpuRenderData& data)
{
    const auto& srcBtmp = renderTools.fSrcBtmp;
    const auto& dstBtmp = renderTools.fDstBtmp;

    if (srcBtmp.empty() || srcBtmp.getPixels() == nullptr ||
        dstBtmp.empty() || dstBtmp.getPixels() == nullptr) {
        return;
    }

    const int imgWidth = srcBtmp.width();
    const int imgHeight = srcBtmp.height();
    if (imgWidth <= 0 || imgHeight <= 0) return;

    const int xMin = std::max(0, data.fTexTile.left());
    const int xMax = std::min((int)data.fTexTile.right() - 1, imgWidth - 1);
    const int yMin = std::max(0, data.fTexTile.top());
    const int yMax = std::min((int)data.fTexTile.bottom() - 1, imgHeight - 1);

    // composite LUTs applied to the raw (premultiplied) channels,
    // same as Levels; kN32 is little-endian BGRA in memory:
    // byte 0 = B, 1 = G, 2 = R
    for (int yi = yMin; yi <= yMax; yi++) {
        auto dst = static_cast<uchar*>(dstBtmp.getAddr(0, yi - yMin));
        auto src = static_cast<uchar*>(srcBtmp.getAddr(xMin, yi));

        for (int xi = xMin; xi <= xMax; xi++) {
            const uint8_t b = *src++;
            const uint8_t g = *src++;
            const uint8_t r = *src++;
            const uint8_t a = *src++;

            *dst++ = mComp[2][b];
            *dst++ = mComp[1][g];
            *dst++ = mComp[0][r];
            *dst++ = a;
        }
    }
}

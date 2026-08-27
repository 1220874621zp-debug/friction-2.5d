#ifndef TRACKMATTECALLER_H
#define TRACKMATTECALLER_H

#include "rastereffectcaller.h"
#include "Boxes/boxrenderdata.h"

#include <QPoint>

// AE-style track matte: masks the host layer's final image with the
// rendered image of another layer. The matte layer's render data is
// queued by the host during setup (main thread) and guaranteed to be
// finished before the effects phase runs (addDependent), exactly like
// the motion blur samples.
class CORE_EXPORT TrackMatteCaller : public RasterEffectCaller {
    e_OBJECT
public:
    enum class Mode {
        none      = 0,
        alpha     = 1,
        alphaInv  = 2,
        luma      = 3,
        lumaInv   = 4
    };

    TrackMatteCaller(stdsptr<BoxRenderData> matte, const Mode mode);

    void processGpu(QGL33* const gl,
                    GpuRenderTools& renderTools) override;
    void processCpu(CpuRenderTools& renderTools,
                    const CpuRenderData& data) override;

    // color filter mapping a matte pixel to an alpha-only pixel for the
    // given mode (rgb output is zero - only alpha is consumed by DstIn)
    static sk_sp<SkColorFilter> makeAlphaFilter(const Mode mode);
private:
    stdsptr<BoxRenderData> mMatte;
    Mode mMode;

    // gpu
    static bool sInitialized;
    static GLuint sProgramId;
    static GLint sMaskTexLoc;
    static GLint sModeLoc;
    static GLint sRect2Loc;
    static void sInitialize(QGL33* const gl);
};

#endif // TRACKMATTECALLER_H

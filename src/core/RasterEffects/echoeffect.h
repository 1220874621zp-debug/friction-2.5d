#ifndef ECHOEFFECT_H
#define ECHOEFFECT_H

#include "rastereffect.h"

class BoundingBox;
class ComboBoxProperty;

// AE-style echo: composites the layer's own image sampled at past
// (or future) frames with per-echo decaying intensity. stateless and
// per-frame deterministic like the particle emitter - the samples are
// re-rendered on demand via queExternalRender, so any frame can be
// rendered independently (fits the random-access render pipeline)
class CORE_EXPORT EchoEffect : public RasterEffect {
    friend class SelfRef;
    EchoEffect();
public:
    FrameRange prp_getIdenticalRelRange(const int relFrame) const;

    stdsptr<RasterEffectCaller> getEffectCaller(
            const qreal relFrame, const qreal resolution,
            const qreal influence, BoxRenderData* const data) const;
private:
    FrameRange getEchoPropsIdenticalRange(const int relFrame) const;

    // reentrancy guard lives as a file-scope thread_local in the cpp
    // (getEffectCaller runs on several scheduler threads at once)
    qptr<BoundingBox> mParentBox;
    qsptr<QrealAnimator> mEchoTime;
    qsptr<QrealAnimator> mEchoCount;
    qsptr<QrealAnimator> mStartIntensity;
    qsptr<QrealAnimator> mDecay;
    // per-echo motion blur: each echo is averaged over a shutter window of
    // motionBlur * echoTime centred on the echo's own frame (0 = off, the
    // echo is a single sharp copy like before)
    qsptr<QrealAnimator> mMotionBlur;
    qsptr<ComboBoxProperty> mOperator;
};

#endif // ECHOEFFECT_H

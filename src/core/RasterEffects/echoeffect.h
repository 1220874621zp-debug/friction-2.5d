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

    mutable bool mBlocked = false;
    qptr<BoundingBox> mParentBox;
    qsptr<QrealAnimator> mEchoTime;
    qsptr<QrealAnimator> mEchoCount;
    qsptr<QrealAnimator> mStartIntensity;
    qsptr<QrealAnimator> mDecay;
    qsptr<ComboBoxProperty> mOperator;
};

#endif // ECHOEFFECT_H

#ifndef BONEWARPEFFECT_H
#define BONEWARPEFFECT_H

#include "rastereffect.h"
#include "Animators/qrealanimator.h"

#include "Properties/boxtargetproperty.h"

class Bone;

// simple Moho-style bone warping for raster layers: the layer's
// rendered image (already in scene space) is bent by the bones of a
// linked chain. Weights fall off with the distance to each bone
// segment; sampling is backward-LBS in scene space. The bind pose is
// captured when the target chain is set (or re-captured from the menu).
class CORE_EXPORT BoneWarpEffect : public RasterEffect {
    Q_OBJECT
public:
    BoneWarpEffect();

    stdsptr<RasterEffectCaller> getEffectCaller(
            const qreal relFrame, const qreal resolution,
            const qreal influence, BoxRenderData * const data) const;

    void prp_setupTreeViewMenu(PropertyMenu * const menu);

    // re-capture the bind pose (current bone transforms) and re-arm the
    // live-follow connections
    void rebind();
    // diagnostics: number of bones captured by the last rebind
    int boundBoneCount() const { return mBind.count(); }

    // point the warp at a bone chain (bind flow / auto-attach)
    void setChainRoot(Bone* const root);
private:
    struct BindRec {
        Bone* bone;
        QMatrix bindTotal;
    };
    QList<BindRec> collectBindRecs() const;
    void clearFollowConns();

    qsptr<BoxTargetProperty> mBonesRoot; // root bone of the chain

    mutable QList<BindRec> mBind;
    mutable bool mBindDirty = true;
    // the host layer lives INSIDE the followed bone chain: reacting to
    // a bone change dirties the host, which propagates back up as a
    // bone range change - without this guard the signal chain loops
    // forever (stack overflow)
    bool mInFollow = false;
    ConnContext mFollowConn;
signals:
    void rebound();
};

#endif // BONEWARPEFFECT_H

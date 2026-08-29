#ifndef BONELAYER_H
#define BONELAYER_H

#include "containerbox.h"

// container for FK bones: a plain group-semantics container shown with
// its own row icon; bones added with the bone tool land here. Moho-style
// it may also hold the artwork layers that the bones drive (bound
// layers are re-parented into the individual bones)
class CORE_EXPORT BoneLayer : public ContainerBox {
    e_OBJECT
protected:
    BoneLayer();
public:
    QString tagNameXEV() const { return "BoneLayer"; }

    // convert an existing group into a bone layer by swapping the
    // shell: a new BoneLayer takes the group's exact spot in its
    // parent (same stacking index), every child is re-parented into
    // it keeping its world appearance, and the emptied group
    // self-removes. The group's own transform is baked into the
    // children (the new layer starts with an identity transform).
    // Returns the new bone layer, or null on a bad/parentless group.
    static BoneLayer* convertFromGroup(ContainerBox* const group);

    // timeline drop-onto-the-row handler: plain groups are FLATTENED
    // into this bone layer - their children become direct children
    // (world appearance preserved) and the emptied shell is removed.
    // Nesting a group inside the rig isolates its blend-mode layers
    // from their backdrop and visibly shifts colors (the same reason
    // the PSD importer flattens folders), hence the flattening.
    // Plain layers/sounds are simply re-parented in.
    void absorbDroppedBoxes(const QList<eBoxOrSound*>& boxes);
};

#endif // BONELAYER_H

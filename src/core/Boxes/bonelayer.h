#ifndef BONELAYER_H
#define BONELAYER_H

#include "containerbox.h"

// container for FK bones: a plain layer-type group shown with its own
// row icon; bones added with the bone tool land here (skinning comes in
// a later phase, so the layer itself never renders content)
class CORE_EXPORT BoneLayer : public ContainerBox {
    e_OBJECT
protected:
    BoneLayer();
public:
    QString tagNameXEV() const { return "BoneLayer"; }
};

#endif // BONELAYER_H

#include "bonelayer.h"

BoneLayer::BoneLayer() :
    ContainerBox(QStringLiteral("\u9AA8\u9ABC\u5C42"),
                 eBoxType::boneLayer) {
    // explicitly created rig container: survives being empty
    mKeepWhenEmpty = true;
}

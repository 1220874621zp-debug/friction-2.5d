#include "Boxes/cameralayer.h"
#include "Animators/qrealanimator.h"
#include "canvas.h"
#include "Private/document.h"

CameraLayer::CameraLayer() :
    BoundingBox(QStringLiteral("\u6444\u50CF\u673A"), eBoxType::cameraLayer) {
    mPanX = enve::make_shared<QrealAnimator>(0., -100000., 100000., 1.,
                QStringLiteral("\u5E73\u79FB X"));
    mPanY = enve::make_shared<QrealAnimator>(0., -100000., 100000., 1.,
                QStringLiteral("\u5E73\u79FB Y"));
    mZoom = enve::make_shared<QrealAnimator>(1., 0.01, 100., 0.01,
                QStringLiteral("\u7F29\u653E"));
    mRotZ = enve::make_shared<QrealAnimator>(0., -36000., 36000., 1.,
                QStringLiteral("\u65CB\u8F6C"));
    mRotX = enve::make_shared<QrealAnimator>(0., -89., 89., 1.,
                QStringLiteral("\u503E\u659C X"));
    mRotY = enve::make_shared<QrealAnimator>(0., -89., 89., 1.,
                QStringLiteral("\u503E\u659C Y"));
    mFocal = enve::make_shared<QrealAnimator>(800., 1., 100000., 1.,
                QStringLiteral("\u7126\u8DDD"));
    ca_addChild(mPanX);
    ca_addChild(mPanY);
    ca_addChild(mZoom);
    ca_addChild(mRotZ);
    ca_addChild(mRotX);
    ca_addChild(mRotY);
    ca_addChild(mFocal);

    // camera changes must invalidate every 3D layer's render data and
    // the scene frame cache - a plain box-child animator change does
    // NOT reach them (each layer believes nothing of its own changed
    // and would keep serving its cached render data with the OLD
    // camera matrix). NOTE: prp_afterChangedAbsRange is a virtual
    // METHOD, not a signal - the Qt signal emitted from it is
    // prp_absFrameRangeChanged
    const auto changed = [this](const FrameRange& range, const bool) {
        const auto scene = getParentScene();
        if(scene) scene->sceneCameraChanged(range);
    };
    const QrealAnimator* anims[] = { mPanX.data(), mPanY.data(),
                                     mZoom.data(), mRotZ.data(),
                                     mRotX.data(), mRotY.data(),
                                     mFocal.data() };
    for(const auto anim : anims) {
        connect(anim, &Property::prp_absFrameRangeChanged, changed);
    }
}

// combined camera transform in canvas/world space:
//   point -> translate(-pan) -> center -> scale -> rotZ -> tilt
//         -> project -> back from center
// (the tilt math mirrors AdvancedTransformAnimator::get3DTransformAtFrame:
//  plane point (x,y,0) rotated by Rx*Ry, then x' = f*vx/(f+vz))
SkMatrix CameraLayer::getCameraTransformAtFrame(const qreal relFrame,
                                                const qreal canvasW,
                                                const qreal canvasH) const {
    const qreal panX = mPanX->getEffectiveValue(relFrame);
    const qreal panY = mPanY->getEffectiveValue(relFrame);
    const qreal zoom = mZoom->getEffectiveValue(relFrame);
    const qreal rotZ = mRotZ->getEffectiveValue(relFrame);
    const qreal rotX = mRotX->getEffectiveValue(relFrame);
    const qreal rotY = mRotY->getEffectiveValue(relFrame);
    if(qAbs(panX) < 0.001 && qAbs(panY) < 0.001 &&
       qAbs(zoom - 1.) < 0.001 && qAbs(rotZ) < 0.001 &&
       qAbs(rotX) < 0.001 && qAbs(rotY) < 0.001) {
        return SkMatrix();
    }
    const qreal cx = canvasW * 0.5;
    const qreal cy = canvasH * 0.5;
    SkMatrix result;
    result.setTranslate(toSkScalar(cx), toSkScalar(cy));
    if(qAbs(rotX) > 0.001 || qAbs(rotY) > 0.001) {
        const qreal f = mFocal->getEffectiveValue(relFrame);
        const qreal rx = qDegreesToRadians(rotX);
        const qreal ry = qDegreesToRadians(rotY);
        const qreal crx = std::cos(rx);
        const qreal srx = std::sin(rx);
        const qreal cry = std::cos(ry);
        const qreal sry = std::sin(ry);
        SkMatrix h;
        h.setAll(toSkScalar(f*cry),       toSkScalar(0.),
                 toSkScalar(0.),
                 toSkScalar(f*srx*sry),   toSkScalar(f*crx),
                 toSkScalar(0.),
                 toSkScalar(-crx*sry),    toSkScalar(srx),
                 toSkScalar(f));
        result.preConcat(h);
    }
    if(qAbs(rotZ) > 0.001) {
        SkMatrix r;
        r.setRotate(toSkScalar(rotZ));
        result.preConcat(r);
    }
    if(qAbs(zoom - 1.) > 0.001) {
        SkMatrix s;
        s.setScale(toSkScalar(zoom), toSkScalar(zoom));
        result.preConcat(s);
    }
    SkMatrix toCenter;
    toCenter.setTranslate(toSkScalar(-cx), toSkScalar(-cy));
    result.preConcat(toCenter);
    if(qAbs(panX) > 0.001 || qAbs(panY) > 0.001) {
        // content moves opposite to the camera pan
        SkMatrix p;
        p.setTranslate(toSkScalar(-panX), toSkScalar(-panY));
        result.preConcat(p);
    }
    return result;
}

bool CameraLayer::hasPerspectiveAtFrame(const qreal relFrame) const {
    return qAbs(mRotX->getEffectiveValue(relFrame)) > 0.001 ||
           qAbs(mRotY->getEffectiveValue(relFrame)) > 0.001;
}

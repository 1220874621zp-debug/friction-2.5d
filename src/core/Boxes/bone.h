#ifndef BONE_H
#define BONE_H

#include "containerbox.h"

class QrealAnimator;
class Property;

// 2D FK bone: a transform-only CONTAINER drawn as a pivot-to-tail
// segment in local space (head at the origin, tail at (length, 0)).
// Child bones are Bone boxes nested inside, so the regular inherited-
// transform chain composes rotations for free. No renderable content
// of its own (skinning comes in a later phase).
class CORE_EXPORT Bone : public ContainerBox {
    e_OBJECT
protected:
    Bone();
public:
    bool relPointInsidePath(const QPointF &relPos) const;

    // editing-time visual, drawn by Canvas for every visible bone
    void drawBone(SkCanvas* const canvas, const CanvasMode mode,
                  const float invScale, const bool ctrlPressed) const;

    qreal getLength() const;
    QrealAnimator* lengthAnimator() const { return mLength.get(); }
    // local-space tail position (length, 0)
    QPointF getTailRelPos() const;
    // world-space head/tail at the current frame
    QPointF getHeadAbsPos() const;
    QPointF getTailAbsPos() const;

    // create a child bone whose head snaps to this bone's tail
    Bone* addChildBone();

    // bind the currently selected visual layers into this bone (they
    // become children and follow its transform; world position is
    // preserved - the bind pose is the current pose)
    void bindSelectedLayers();
    // move every non-bone child layer back to this bone's parent
    void unbindLayers();
    // unbind a single layer (layer-side menu entry): move it back to
    // this bone's parent keeping its world appearance
    void unbindLayer(BoundingBox* const layer);
    // re-parent this bone under 'parent' keeping its world transform
    // (bone parent-link tool); returns false on no-op/cycle
    bool setParentBone(Bone* const parent);

    // key EVERY animatable channel of this bone (position/rotation/
    // scale/shear/pivot + length) at the current frame. Staggered
    // per-channel keys leave the un-keyed channels interpolating
    // through the pose frame (visible drift); freezing pins the pose
    void freezeChannels();
    // toolbar toggle: when on, every bone pose operation freezes the
    // bone (all channels keyed) instead of keying just the touched
    // channel (set/persisted by the timeline toolbar button)
    static bool sAutoFreezePose;

    // lazily create the canvas overlay (tail joint); called from
    // BoundingBox::prp_updateCanvasProps which owns the props list
    Property* ensureBoneOverlay();

    // diagnostics: append a line to bone_diag.txt next to the exe and
    // mirror it to qDebug (the debug-log dialog proved unreliable for
    // capturing qDebug output)
    static void diag(const QString& line);
    // snapshot of every bone in the scene (name/flags/parent chain)
    static void diagSceneState(const QString& when);

    void prp_setupTreeViewMenu(PropertyMenu * const menu) override;
private:
    qsptr<QrealAnimator> mLength;
    qsptr<Property> mOverlay;
};

#endif // BONE_H

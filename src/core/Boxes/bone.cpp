#include "bone.h"

#include "Animators/transformanimator.h"
#include "Animators/qpointfanimator.h"
#include "MovablePoints/pointshandler.h"
#include "bonelayer.h"
#include <QDebug>
#include <QDebug>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QCoreApplication>
#include "include/effects/SkDashPathEffect.h"
#include "matrixdecomposition.h"
#include "canvas.h"
#include "Private/document.h"
#include "Psd/psdimagebox.h"
#include <QTimer>

// draggable tail joint: dragging rotates the bone around its head
// (pivot); the length itself is edited on the property row or during
// bone creation. Undo/redo and auto-keyframing ride the standard
// prp_start/finishTransform pipeline (MotionKeyPoint pattern).
class BoneTailPoint : public MovablePoint {
    e_OBJECT
public:
    BoneTailPoint(BasicTransformAnimator * const trans,
                  Bone * const bone)
        : MovablePoint(trans, TYPE_PIVOT_POINT), mBone(bone) {
        setRadius(5);
    }

    QPointF getRelativePos() const override {
        return mBone->getTailRelPos();
    }

    void setRelativePos(const QPointF &relPos) override {
        const qreal deg = qRadiansToDegrees(qAtan2(relPos.y(), relPos.x()));
        mBone->getBoxTransformAnimator()->getRotAnimator()->
                setCurrentBaseValue(deg);
    }

    void startTransform() override {
        mRotAtStart = mBone->getBoxTransformAnimator()->
                getRotAnimator()->getEffectiveValue();
        mBone->getBoxTransformAnimator()->getRotAnimator()->prp_startTransform();
    }
    void finishTransform() override {
        const auto rot = mBone->getBoxTransformAnimator()->getRotAnimator();
        rot->prp_finishTransform();
        // Moho-style auto-keyframing: posing a bone records a rotation
        // key at the current frame (no-op drags do not create keys)
        if(qAbs(rot->getEffectiveValue() - mRotAtStart) > 0.01) {
            rot->anim_saveCurrentValueAsKey();
        }
        if(Document::sInstance) Document::sInstance->actionFinished();
    }
    void cancelTransform() override {
        mBone->getBoxTransformAnimator()->getRotAnimator()->prp_cancelTransform();
    }

    void drawSk(SkCanvas * const canvas, const CanvasMode mode,
                const float invScale, const bool keyOnCurrent,
                const bool ctrlPressed) override {
        Q_UNUSED(mode) Q_UNUSED(ctrlPressed)
        drawOnAbsPosSk(canvas,
                       toSkPoint(getAbsolutePos()),
                       invScale, toSkColor(QColor(120, 170, 255)),
                       keyOnCurrent);
    }
private:
    Bone * const mBone;
    qreal mRotAtStart = 0.;
};

// canvas overlay property (NOT in the property tree): owns the tail
// point so picking/dragging works through the regular PointsHandler
// chain; the bone graphics themselves are drawn by Canvas::drawBones
class BoneOverlay final : public Property {
    Q_OBJECT
public:
    BoneOverlay(Bone * const bone) : Property("bone overlay") {
        setPointsHandler(enve::make_shared<PointsHandler>());
        const auto handler = getPointsHandler();
        handler->appendPt(enve::make_shared<BoneTailPoint>(
                              bone->getBoxTransformAnimator(), bone));
        prp_setName(QStringLiteral("bone overlay"));
        prp_enabledDrawingOnCanvas();
    }

    // overlay only - never serialized (MotionPathHandler pattern)
    QDomElement prp_writePropertyXEV_impl(const XevExporter& exp) const {
        return exp.createElement("BoneOverlay");
    }
    void prp_readPropertyXEV_impl(const QDomElement& ele,
                                  const XevImporter& imp) {
        Q_UNUSED(ele) Q_UNUSED(imp)
    }
};

Bone::Bone() : ContainerBox(QObject::tr("Bone"),
                            eBoxType::bone) {
    // structural rig node: an empty bone (layers unbound, no child
    // bones) must NOT be auto-removed like an emptied group
    mKeepWhenEmpty = true;
    mLength = enve::make_shared<QrealAnimator>(100, 10, 2000, 1,
                                               QObject::tr("Length"));
    ca_addChild(mLength);

    connect(this, &BoundingBox::prp_sceneChanged,
            this, [this](Canvas* const oldS, Canvas* const newS) {
        if(oldS) oldS->removeBone(this);
        if(newS) newS->addBone(this);
    });
}

qreal Bone::getLength() const {
    return mLength->getEffectiveValue();
}

QPointF Bone::getTailRelPos() const {
    return QPointF(getLength(), 0);
}

QPointF Bone::getHeadAbsPos() const {
    return getTotalTransform().map(QPointF(0, 0));
}

QPointF Bone::getTailAbsPos() const {
    return getTotalTransform().map(getTailRelPos());
}

bool Bone::relPointInsidePath(const QPointF &relPos) const {
    // hit test: distance to the head-tail segment
    const QPointF p1(0, 0);
    const QPointF p2 = getTailRelPos();
    const QPointF d = p2 - p1;
    const qreal denom = d.x()*d.x() + d.y()*d.y();
    const qreal u = denom > 0 ? qBound(0., ((relPos.x()-p1.x())*d.x() +
                                            (relPos.y()-p1.y())*d.y()) / denom, 1.)
                              : 0.;
    const QPointF proj(p1.x() + u*d.x(), p1.y() + u*d.y());
    const qreal threshold = qMax(8., 0.08*getLength());
    return QLineF(relPos, proj).length() <= threshold;
}

void Bone::drawBone(SkCanvas * const canvas, const CanvasMode mode,
                    const float invScale, const bool ctrlPressed) const {
    Q_UNUSED(mode) Q_UNUSED(ctrlPressed)
    if(!isVisible()) return;
    // inherited visibility: hiding the bone LAYER (or any ancestor)
    // must hide its bones too, like it hides regular group content
    for(auto p = getParentGroup(); p; p = p->getParentGroup()) {
        if(!p->isVisible()) return;
    }
    const auto total = getTotalTransform();
    const QPointF head = total.map(QPointF(0, 0));
    const QPointF tail = total.map(getTailRelPos());

    // Moho-style tapered bone: a filled quad, wide at the head joint
    // and narrowing towards the tail
    const QPointF dir = tail - head;
    const qreal len = qMax(1., QLineF(head, tail).length());
    const QPointF n(-dir.y()/len, dir.x()/len);
    const qreal hw = 5.f*invScale;  // head half-width
    const qreal tw = 1.2f*invScale; // tail half-width

    const bool sel = isSelected();
    const QColor main = sel ? QColor(120, 170, 255) : QColor(170, 170, 170);
    QColor fill = main; fill.setAlpha(60);

    SkPath quad;
    quad.moveTo(toSkPoint(head + n*hw));
    quad.lineTo(toSkPoint(head - n*hw));
    quad.lineTo(toSkPoint(tail - n*tw));
    quad.lineTo(toSkPoint(tail + n*tw));
    quad.close();

    SkPaint paint;
    paint.setAntiAlias(true);
    // dark halo underneath for contrast on any background
    SkPath halo = quad; halo.offset(0, 0);
    paint.setStyle(SkPaint::kStroke_Style);
    paint.setStrokeWidth(3.f*invScale);
    paint.setColor(SkColorSetA(SK_ColorBLACK, 100));
    canvas->drawPath(quad, paint);

    paint.setStyle(SkPaint::kFill_Style);
    paint.setColor(toSkColor(fill));
    canvas->drawPath(quad, paint);
    paint.setStyle(SkPaint::kStroke_Style);
    paint.setStrokeWidth(1.2f*invScale);
    paint.setColor(toSkColor(main));
    canvas->drawPath(quad, paint);

    // dashed link to the parent bone's tail (rig structure hint)
    if(const auto parentBone = enve_cast<Bone*>(getParentGroup())) {
        const QPointF pTail = parentBone->getTailAbsPos();
        SkPath link;
        link.moveTo(toSkPoint(head));
        link.lineTo(toSkPoint(pTail));
        SkPaint lp;
        lp.setAntiAlias(true);
        lp.setStyle(SkPaint::kStroke_Style);
        lp.setStrokeWidth(1.2f*invScale);
        lp.setColor(SkColorSetARGB(160, 150, 170, 210));
        const SkScalar dash[2] = {4.f*invScale, 4.f*invScale};
        lp.setPathEffect(SkDashPathEffect::Make(dash, 2, 0));
        canvas->drawPath(link, lp);
        lp.setPathEffect(nullptr);
    }

    // joints: filled head circle, small hollow tail circle
    paint.setStyle(SkPaint::kFill_Style);
    paint.setColor(toSkColor(main));
    canvas->drawCircle(toSkPoint(head), 3.5f*invScale, paint);
    paint.setStyle(SkPaint::kStroke_Style);
    canvas->drawCircle(toSkPoint(tail), 2.5f*invScale, paint);
}

Bone* Bone::addChildBone() {
    const auto child = enve::make_shared<Bone>();
    addContained(child);
    // snap the child head onto this bone's tail: local (length, 0)
    child->getBoxTransformAnimator()->setPivot(0, 0);
    const QPointF tail = getTailRelPos();
    child->getBoxTransformAnimator()->setPosition(tail.x(), tail.y());
    return child.get();
}

Property* Bone::ensureBoneOverlay() {
    if(!mOverlay) {
        mOverlay = enve::make_shared<BoneOverlay>(this);
    }
    return mOverlay.get();
}


void Bone::diag(const QString& line) {
    qDebug() << "[BONE-DIAG]" << line;
    QFile f(QCoreApplication::applicationDirPath() +
            QStringLiteral("/bone_diag.txt"));
    if(f.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream s(&f);
        s << QDateTime::currentDateTime().toString(
                     QStringLiteral("hh:mm:ss.zzz "))
          << line << "\n";
        f.close();
    }
}

void Bone::diagSceneState(const QString& when) {
    QString out = QStringLiteral("SNAPSHOT %1 bones:").arg(when);
    // resolve the active scene through any live bone (static context)
    Canvas* scene = nullptr;
    for(const auto& c : Document::sInstance ?
             Document::sInstance->fScenes : QList<qsptr<Canvas>>()) {
        if(c && !c->getBones().isEmpty()) { scene = c.get(); break; }
    }
    if(!scene) { diag(out + QStringLiteral(" (none found)")); return; }
    for(const auto b : scene->getBones()) {
        if(!b) continue;
        QString chain;
        for(auto p = b->getParentGroup(); p; p = p->getParentGroup()) {
            chain += QStringLiteral("/") + p->prp_getName();
        }
        out += QStringLiteral("\n  %1 vis=%2 solo=%3 shy=%4 hiddenByTrack=%5 len=%6 parentChain=%7")
               .arg(b->prp_getName())
               .arg(b->isVisible()).arg(b->isSolo()).arg(b->isShy())
               .arg(b->isHiddenByTrack()).arg(b->getLength()).arg(chain);
    }
    diag(out);
}

// does the bone already carry non-bone artwork? (first-bind detection
// for the stacking-slot preservation below)
static bool boneHasArtwork(const Bone* const bone) {
    for(const auto& c : bone->getContained()) {
        if(enve_cast<Bone*>(c.data())) continue;
        if(enve_cast<BoundingBox*>(c.data())) return true;
    }
    return false;
}

void Bone::bindSelectedLayers() {
    const auto scene = getParentScene();
    if(!scene) return;
    diag(QStringLiteral("bindLayers into %1 selected=%2")
         .arg(prp_getName())
         .arg(scene->getSelectedBoxesList().count()));
    const auto& sel = scene->getSelectedBoxesList();
    int bound = 0;
    for(const auto box : sel) {
        if(!box || box == this) continue;
        if(enve_cast<Bone*>(box) || enve_cast<BoneLayer*>(box)) continue;
        // a layer that is an ANCESTOR of this bone would create a cycle
        if(isAncestor(box)) continue;
        // reparent bind: the layer becomes a child of this bone and
        // follows its transform rigidly (world position preserved -
        // the bind pose is the current pose; no mesh deformation).
        // A bound layer renders at its BONE's slot, so the bone is
        // placed to keep the layer's position in the stack (first
        // bind only). For a chain-root bone it takes the layer's old
        // slot; for a NESTED bone the layer's order is kept against
        // the artwork already bound to the nearest ancestor bone by
        // comparing the newcomer's old slot with the chain root's
        // slot in the shared container (the root sits at the
        // first-bound layer's position, preserving the artwork order)
        const auto myParent = getParentGroup();
        const bool firstBind = !boneHasArtwork(this);
        const auto oldParent = box->getParentGroup();
        const int oldSlot = oldParent ?
                    oldParent->getContainedIndex(box) : -1;
        // topmost bone ancestor (sits next to the artwork rows)
        ContainerBox* chainRoot = this;
        for(auto p = myParent; p && enve_cast<Bone*>(p);
            p = p->getParentGroup()) {
            chainRoot = p;
        }
        const auto rootParent = chainRoot->getParentGroup();
        const int rootSlot = rootParent ?
                    rootParent->getContainedIndex(chainRoot) : -1;
        reparentKeepWorld(box, this);
        if(firstBind && myParent) {
            if(oldParent == myParent && oldSlot >= 0) {
                // chain root: take the layer's old slot (slot-1: the
                // layer's own slot was vacated by the reparent)
                const int from = myParent->getContainedIndex(this);
                const int to = qMax(0, oldSlot - 1);
                if(from >= 0 && from != to) {
                    myParent->moveContainedInList(this, from, to);
                }
            } else if(enve_cast<Bone*>(myParent) && oldParent &&
                      oldParent == rootParent &&
                      oldSlot >= 0 && rootSlot >= 0) {
                // nested bone: climb to the nearest ancestor bone with
                // bound artwork and order the chain segment on that
                // path against its artwork
                ContainerBox* artParent = myParent;
                ContainerBox* unit = this;
                while(artParent && enve_cast<Bone*>(artParent)) {
                    const auto ab = static_cast<Bone*>(artParent);
                    if(boneHasArtwork(ab)) break;
                    unit = artParent;
                    artParent = artParent->getParentGroup();
                }
                if(artParent && enve_cast<Bone*>(artParent)) {
                    eBoxOrSound* firstLayer = nullptr;
                    eBoxOrSound* lastLayer = nullptr;
                    for(const auto& c : artParent->getContained()) {
                        if(!c || enve_cast<Bone*>(c.data())) continue;
                        if(!enve_cast<BoundingBox*>(c.data())) continue;
                        if(!firstLayer) firstLayer = c.data();
                        lastLayer = c.data();
                    }
                    if(firstLayer && lastLayer) {
                        if(oldSlot > rootSlot) {
                            artParent->moveContainedBelow(unit, lastLayer);
                        } else {
                            artParent->moveContainedAbove(unit, firstLayer);
                        }
                    }
                }
            }
        }
        // blank-pixels investigation: pixel state of the bound layer at
        // bind time and again once the render churn settles
        {
            QString state;
            if(const auto psd = enve_cast<PsdImageBox*>(box)) {
                state = QStringLiteral(" psd file=%1 loaded=%2")
                        .arg(QFile::exists(psd->filePath()) ? "Y" : "N")
                        .arg(psd->hasLoadedImage() ? "Y" : "N");
            }
            diag(QStringLiteral("bound '%1'%2")
                 .arg(box->prp_getName(), state));
            QTimer::singleShot(2000,
                               [w = QPointer<BoundingBox>(box)]() {
                if(!w) return;
                QString s2;
                if(const auto psd = enve_cast<PsdImageBox*>(w.data())) {
                    s2 = QStringLiteral(" psd file=%1 loaded=%2")
                         .arg(QFile::exists(psd->filePath()) ? "Y" : "N")
                         .arg(psd->hasLoadedImage() ? "Y" : "N");
                }
                Bone::diag(QStringLiteral("bound-recheck '%1'%2")
                           .arg(w->prp_getName(), s2));
            });
        }
        bound++;
    }
    if(bound > 0 && Document::sInstance) {
        Document::sInstance->actionFinished();
    }
}

// unbind destination: the nearest ancestor that is NOT a bone - for a
// mid-chain bone the direct parent is just another bone, so moving
// "one level up" would re-bind the layer into that bone (confusing
// hierarchy). Walk up past every bone to the rig layer / scene level.
static ContainerBox* nonBoneAncestor(Bone* const from) {
    ContainerBox* p = from->getParentGroup();
    while(p && enve_cast<Bone*>(p)) p = p->getParentGroup();
    return p;
}

void Bone::unbindLayers() {
    diag(QStringLiteral("unbindLayers on %1").arg(prp_getName()));
    diagSceneState(QStringLiteral("before unbindLayers"));
    // reparent bind: unbind = move every non-bone layer in this bone's
    // subtree back out to the nearest non-bone ancestor (the rig
    // layer), keeping each one's world appearance; child bones stay
    const auto dest = nonBoneAncestor(this);
    if(!dest) return;
    // collect first: reparenting mutates the very getContained()
    // lists the walk is iterating over
    QList<BoundingBox*> layers;
    QList<Bone*> stack{this};
    while(!stack.isEmpty()) {
        const auto b = stack.takeLast();
        if(!b) continue;
        for(const auto& c : b->getContained()) {
            if(const auto child = enve_cast<Bone*>(c.data())) {
                stack.append(child);
            } else if(const auto layer =
                      enve_cast<BoundingBox*>(c.data())) {
                layers.append(layer);
            }
        }
    }
    int moved = 0;
    // the layers take the BONE's stacking slot instead of clumping at
    // the top of the container (each right after the previous one,
    // keeping their relative order)
    const int baseSlot = dest->getContainedIndex(this);
    for(const auto layer : layers) {
        reparentKeepWorld(layer, dest);
        if(baseSlot >= 0) {
            const int from = dest->getContainedIndex(layer);
            const int to = qMin(baseSlot + moved,
                                dest->getContained().count() - 1);
            if(from >= 0 && from != to) {
                dest->moveContainedInList(layer, from, to);
            }
        }
        moved++;
    }
    if(moved > 0 && Document::sInstance) {
        Document::sInstance->actionFinished();
    }
}

void Bone::unbindLayer(BoundingBox* const layer) {
    diag(QStringLiteral("unbindLayer %1 from %2")
         .arg(layer ? layer->prp_getName() : QStringLiteral("?"),
              prp_getName()));
    if(!layer) return;
    // reparent bind: unbind = move the layer out of the bone chain to
    // the nearest non-bone ancestor, keeping its world appearance and
    // its visual stacking slot (the bone's position)
    if(const auto dest = nonBoneAncestor(this)) {
        const int slot = dest->getContainedIndex(this);
        reparentKeepWorld(layer, dest);
        if(slot >= 0) {
            const int from = dest->getContainedIndex(layer);
            if(from >= 0 && from != slot) {
                dest->moveContainedInList(layer, from, slot);
            }
        }
    }
    // keep the layer selected so its new location is obvious
    if(const auto scene = getParentScene()) {
        scene->clearBoxesSelection();
        scene->addBoxToSelection(layer);
    }
    if(Document::sInstance) Document::sInstance->actionFinished();
}

bool Bone::setParentBone(Bone* const parent) {
    if(!parent || parent == this) return false;
    // (the earlier same-parent restriction silently rejected almost
    // every link in a chain - bone3's parent is bone2 while bone1's
    // parent is the rig layer; reparentKeepWorld handles any parent)
    // cycle guard: the new parent must not be a descendant of this
    for(auto p = parent; p; ) {
        if(p == this) return false;
        p = enve_cast<Bone*>(p->getParentGroup());
    }
    reparentKeepWorld(this, parent);
    if(const auto scene = getParentScene()) {
        scene->clearBoxesSelection();
        scene->addBoxToSelection(this);
    }
    if(Document::sInstance) Document::sInstance->actionFinished();
    return true;
}

void Bone::freezeChannels() {
    diag(QStringLiteral("freezePose '%1' @%2")
         .arg(prp_getName())
         .arg(anim_getCurrentAbsFrame()));
    // BoxTransformAnimator is a ComplexAnimator: one save-keys call
    // recurses into every channel (pivot, position, rotation, scale,
    // shear); length is the Bone-specific extra channel
    getBoxTransformAnimator()->anim_saveCurrentValueAsKey();
    lengthAnimator()->anim_saveCurrentValueAsKey();
}

void Bone::freezePose() {
    const auto scene = getParentScene();
    bool expanded = false;
    if(scene && isSelected()) {
        // freezing one of several selected bones freezes the whole
        // selection - one click pins the posed rig part
        for(const auto& box : scene->getSelectedBoxesList()) {
            if(const auto bone = enve_cast<Bone*>(box)) {
                bone->freezeChannels();
                expanded = true;
            }
        }
    }
    if(!expanded) freezeChannels();
    if(Document::sInstance) Document::sInstance->actionFinished();
}

void Bone::prp_setupTreeViewMenu(PropertyMenu * const menu) {
    // bind actions FIRST: the base BoundingBox menu is very long and
    // would bury them at the bottom where nobody looks
    const QIcon icon = QIcon::fromTheme("group");
    menu->addPlainAction<Bone>(
                icon,
                tr("Bind Selected Layers to This Bone"),
                [](Bone* const bone) { bone->bindSelectedLayers(); });
    menu->addPlainAction<Bone>(
                icon,
                tr("Unbind Layers"),
                [](Bone* const bone) { bone->unbindLayers(); });
    menu->addPlainAction<Bone>(
                icon,
                tr("Freeze Pose"),
                [](Bone* const bone) { bone->freezePose(); });
    ContainerBox::prp_setupTreeViewMenu(menu);
}

#include "bone.moc"

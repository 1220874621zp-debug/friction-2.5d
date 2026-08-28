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
#include "RasterEffects/bonewarpeffect.h"
#include "RasterEffects/rastereffectcollection.h"
#include "canvas.h"
#include "Private/document.h"

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
        mBone->getBoxTransformAnimator()->getRotAnimator()->prp_startTransform();
    }
    void finishTransform() override {
        mBone->getBoxTransformAnimator()->getRotAnimator()->prp_finishTransform();
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

Bone::Bone() : ContainerBox(QStringLiteral("\u9AA8\u9ABC"),
                            eBoxType::bone) {
    // structural rig node: an empty bone (layers unbound, no child
    // bones) must NOT be auto-removed like an emptied group
    mKeepWhenEmpty = true;
    mLength = enve::make_shared<QrealAnimator>(100, 10, 2000, 1,
                                               QStringLiteral("\u957F\u5EA6"));
    ca_addChild(mLength);
    mWarpRadius = enve::make_shared<QrealAnimator>(150, 10, 2000, 1,
                                                   QStringLiteral("\u5F71\u54CD\u534A\u5F84"));
    mWarpStrength = enve::make_shared<QrealAnimator>(1, 0, 1, 0.01,
                                                     QStringLiteral("\u5F3A\u5EA6"));
    ca_addChild(mWarpRadius);
    ca_addChild(mWarpStrength);

    connect(this, &BoundingBox::prp_sceneChanged,
            this, [this](Canvas* const oldS, Canvas* const newS) {
        if(oldS) oldS->removeBone(this);
        if(newS) newS->addBone(this);
    });
}

qreal Bone::getLength() const {
    return mLength->getEffectiveValue();
}

qreal Bone::warpRadius() const {
    return mWarpRadius->getEffectiveValue();
}

qreal Bone::warpStrength() const {
    return mWarpStrength->getEffectiveValue();
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

// key-aware channel write: with keys present the animator's effective
// value comes from keys, so a plain base-value write would be silently
// ignored (and a reparent compensation that "didn't happen" shifts the
// layer) - write into the key at the current frame instead
static void setChannelValue(QrealAnimator* const anim, const qreal v) {
    if(!anim) return;
    if(anim->anim_hasKeys()) {
        anim->saveValueToKey(anim->anim_getCurrentRelFrame(), v);
    } else {
        anim->setCurrentBaseValue(v);
    }
}

static void setChannelValue(QPointFAnimator* const anim,
                            const qreal x, const qreal y) {
    if(!anim) return;
    setChannelValue(anim->getXAnimator(), x);
    setChannelValue(anim->getYAnimator(), y);
}

// reparent keeping the FULL world transform: solve the child's new
// relative transform as totalBefore * newParentTotal^-1 and write the
// decomposed TRS back. A translation-only compensation would leave the
// content rotated/scaled whenever the bone itself carries rotation or
// scale - e.g. binding to a mid-chain bone tilted by the chain drag
static void reparentKeepWorld(BoundingBox* const layer,
                               ContainerBox* const newParent) {
    const auto transform = layer->getBoxTransformAnimator();
    const QMatrix totalBefore = layer->getTotalTransform();
    const QMatrix parentTotal = newParent->getTotalTransform();
    const auto ref = layer->ref<BoundingBox>();
    layer->removeFromParent_k();
    newParent->addContained(ref);
    if(!transform) return;
    const QMatrix targetRel = totalBefore*parentTotal.inverted();
    const QPointF pivot(transform->getPivotX(), transform->getPivotY());
    const auto v = MatrixDecomposition::decomposePivoted(targetRel, pivot);
    setChannelValue(transform->getPosAnimator(), v.fMoveX, v.fMoveY);
    setChannelValue(transform->getRotAnimator(), v.fRotation);
    setChannelValue(transform->getScaleAnimator(), v.fScaleX, v.fScaleY);
    setChannelValue(transform->getShearAnimator(), v.fShearX, v.fShearY);
}

// harness bisect switch (auto-attach of the warp effect)
bool Bone::sSkipAutoWarpAttach = false;

// does the layer already carry a bone warp effect?
static bool hasBoneWarpEffect(BoundingBox* const layer) {
    const auto coll = layer->rasterEffectsCollection();
    if(!coll) return false;
    const int n = coll->ca_getNumberOfChildren();
    for(int i = 0; i < n; i++) {
        if(enve_cast<BoneWarpEffect*>(coll->getChild(i))) return true;
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
        // skip anything already inside
        // Moho semantics: the layer is driven ONLY by the bone warp.
        // (the earlier design also re-parented the layer into the bone,
        // but the rigid parent transform exactly cancels the backward
        // warp and the net visual effect is nothing)
        // ORDER: add to the layer FIRST, set the chain root AFTER -
        // setting the target fires the rebind chain which must not run
        // while the effect is still unparented (bind-time crash)
        if(!sSkipAutoWarpAttach && !hasBoneWarpEffect(box)) {
            const auto warp = enve::make_shared<BoneWarpEffect>();
            box->addRasterEffect(warp);
            warp->setChainRoot(this);
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

// remove every bone warp effect from the layer (its bones move it)
static void removeWarpEffects(BoundingBox* const layer) {
    const auto coll = layer->rasterEffectsCollection();
    if(!coll) return;
    QList<BoneWarpEffect*> found;
    for(int i = 0; i < coll->ca_getNumberOfChildren(); i++) {
        const auto eff = enve_cast<BoneWarpEffect*>(coll->getChild(i));
        if(eff) found << eff;
    }
    for(const auto eff : found) {
        layer->removeRasterEffect(eff->ref<RasterEffect>());
    }
}

void Bone::unbindLayers() {
    diag(QStringLiteral("unbindLayers on %1").arg(prp_getName()));
    diagSceneState(QStringLiteral("before unbindLayers"));
    // warp-only bind: unbind = drop the warp effects from every layer
    // in this bone's subtree (layers may also live inside via the
    // parent-link tool - those keep their parent, only the warp goes)
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
    for(const auto layer : layers) {
        removeWarpEffects(layer);
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
    // warp-only bind: unbind = drop the warp effect (no reparenting)
    removeWarpEffects(layer);
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

void Bone::prp_setupTreeViewMenu(PropertyMenu * const menu) {
    // bind actions FIRST: the base BoundingBox menu is very long and
    // would bury them at the bottom where nobody looks
    const QIcon icon = QIcon::fromTheme("group");
    menu->addPlainAction<Bone>(
                icon,
                QStringLiteral("\u7ED1\u5B9A\u9009\u4E2D\u56FE\u5C42\u5230\u6B64\u9AA8\u9ABC"),
                [](Bone* const bone) { bone->bindSelectedLayers(); });
    menu->addPlainAction<Bone>(
                icon,
                QStringLiteral("\u89E3\u7ED1\u56FE\u5C42"),
                [](Bone* const bone) { bone->unbindLayers(); });
    ContainerBox::prp_setupTreeViewMenu(menu);
}

#include "bone.moc"

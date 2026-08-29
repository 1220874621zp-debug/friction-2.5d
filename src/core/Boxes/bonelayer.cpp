#include "bonelayer.h"
#include "Private/document.h"
#include "Boxes/bone.h"
#include "Boxes/imagebox.h"
#include "Psd/psdimagebox.h"
#include <QFile>
#include <QTimer>

namespace {

QString psdPixelState(BoundingBox* const layer) {
    if(const auto psd = enve_cast<PsdImageBox*>(layer)) {
        return QStringLiteral(" psd file=%1 loaded=%2")
                .arg(QFile::exists(psd->filePath()) ? "Y" : "N")
                .arg(psd->hasLoadedImage() ? "Y" : "N");
    }
    if(const auto img = enve_cast<ImageBox*>(layer)) {
        return QStringLiteral(" img file=%1 loaded=%2")
                .arg(QFile::exists(img->filePath()) ? "Y" : "N")
                .arg(img->hasLoadedImage() ? "Y" : "N");
    }
    return QString();
}

// blank-pixels investigation: log each moved layer's pixel state right
// at move time, then again once the render churn settles - eviction /
// reload races only show up in the second pass (bone_diag.txt)
void diagMovedLayers(const QList<QPointer<BoundingBox>>& layers) {
    for(const auto& w : layers) {
        if(!w) continue;
        // self-heal: a missing PsdImageBox pixel-cache file would leave
        // the box blank forever (dashed bounds only) - re-extract it
        // from the .fpsd package right away
        if(const auto psd = enve_cast<PsdImageBox*>(w.data())) {
            psd->ensureCachedFile();
        }
        Bone::diag(QStringLiteral("moved '%1'%2")
                   .arg(w->prp_getName(), psdPixelState(w.data())));
    }
    QTimer::singleShot(2000, [layers]() {
        for(const auto& w : layers) {
            if(!w) continue;
            Bone::diag(QStringLiteral("recheck '%1'%2")
                       .arg(w->prp_getName(), psdPixelState(w.data())));
        }
    });
}

} // namespace

BoneLayer::BoneLayer() :
    ContainerBox(QObject::tr("Bone Layer"),
                 eBoxType::boneLayer) {
    // explicitly created rig container: survives being empty
    mKeepWhenEmpty = true;
}

BoneLayer* BoneLayer::convertFromGroup(ContainerBox* const group) {
    if(!group) return nullptr;
    // bone containers are already rig nodes - nothing to convert
    const auto type = group->getBoxType();
    if(type == eBoxType::boneLayer || type == eBoxType::bone) return nullptr;
    const auto parent = group->getParentGroup();
    if(!parent) return nullptr;
    // take the group's exact spot so the stacking order (and the
    // timeline row order) is unchanged by the conversion
    const int index = parent->getContainedIndex(group);
    const auto newLayer = enve::make_shared<BoneLayer>();
    // insert first, THEN name: insertContained()/addContained() runs the
    // name through prp_sFixName() which strips non-ASCII characters
    if(index >= 0) parent->insertContained(index, newLayer);
    else parent->addContained(newLayer);
    newLayer->prp_setName(group->prp_getName());
    // snapshot into a plain list first: the emptied group self-removes
    // on the last child leaving, which would invalidate getContained()
    QList<qsptr<eBoxOrSound>> children;
    for(const auto& c : group->getContained()) children << c;
    // addContained PREPENDS (insertContained(0, ...)) while painting
    // walks the list from the END, so the snapshot must be fed in
    // reverse to preserve the original stacking order (the same
    // pattern ungroup uses)
    QList<QPointer<BoundingBox>> moved;
    for(int i = children.count() - 1; i >= 0; i--) {
        if(const auto layer = enve_cast<BoundingBox*>(children.at(i).data())) {
            reparentKeepWorld(layer, newLayer.get());
            moved << layer;
        }
    }
    diagMovedLayers(moved);
    // non-BoundingBox stragglers (blend-effect shadows etc.) may keep
    // the group alive - remove the shell explicitly when it survived
    if(group->getParentGroup()) group->removeFromParent_k();
    if(Document::sInstance) Document::sInstance->actionFinished();
    return newLayer.get();
}

void BoneLayer::absorbDroppedBoxes(const QList<eBoxOrSound*>& boxes) {
    int absorbed = 0;
    QList<QPointer<BoundingBox>> moved;
    // reversed iteration + the prepend inside addContained keeps the
    // dragged items' own stacking order intact
    for(int i = boxes.count() - 1; i >= 0; i--) {
        const auto box = boxes.at(i);
        if(!box) continue;
        if(const auto cont = enve_cast<ContainerBox*>(box)) {
            // never drop a container into itself or into its own
            // subtree (cycle)
            if(cont == this || isAncestor(cont)) continue;
            if(cont->getBoxType() == eBoxType::group && !cont->isLink()) {
                // flatten the group shell: its children move directly
                // under this bone layer, the shell self-removes
                QList<qsptr<eBoxOrSound>> children;
                for(const auto& c : cont->getContained()) children << c;
                for(int j = children.count() - 1; j >= 0; j--) {
                    if(const auto layer =
                            enve_cast<BoundingBox*>(children.at(j).data())) {
                        reparentKeepWorld(layer, this);
                        moved << layer;
                    }
                }
                if(cont->getParentGroup()) cont->removeFromParent_k();
                absorbed++;
                continue;
            }
        }
        if(const auto layer = enve_cast<BoundingBox*>(box)) {
            if(layer == this || isAncestor(layer)) continue;
            reparentKeepWorld(layer, this);
            moved << layer;
            absorbed++;
        } else {
            // sounds etc. are not transformable - plain insert
            // (same name save/restore as reparentKeepWorld: prp_sFixName
            // strips non-ASCII names on every insert)
            const QString name = box->prp_getName();
            insertContained(0, box->ref<eBoxOrSound>());
            box->prp_setName(name);
            absorbed++;
        }
    }
    diagMovedLayers(moved);
    if(absorbed > 0 && Document::sInstance) {
        Document::sInstance->actionFinished();
    }
}

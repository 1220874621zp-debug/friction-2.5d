#ifndef SKINMESH_H
#define SKINMESH_H

#include "skia/skiaincludes.h"
#include "include/core/SkPixmap.h"

#include <QVector>
#include <QTransform>
#include <QString>

class Bone;
class BoundingBox;

// per-vertex skin weights: up to 4 influencing bones referenced by
// palette index (SkinBindData::fDefs order), weights sum to 1
struct SkinVtxWeights {
    int fIdx[4] = {-1, -1, -1, -1};
    float fW[4] = {0.f, 0.f, 0.f, 0.f};
    int fCount = 0;
};

// triangle mesh over an image, vertex positions in image pixel space
// (== the ImageBox rel space, origin at the image top-left corner)
struct SkinMesh {
    QVector<SkPoint> fPos;
    QVector<SkinVtxWeights> fW;
    // SkVertices::MakeCopy takes uint16 indices, so the generator must
    // keep the existing-vertex count below 65536
    QVector<uint16_t> fIndices;
    int fCellPx = 0;
    int fImgW = 0;
    int fImgH = 0;

    bool isValid() const
    {
        return fPos.count() >= 3 && !fIndices.isEmpty();
    }
};

// one serialized bone slot of the bind: bones are re-resolved at
// evaluation time by matching fName against the chain walk, so the
// file survives hierarchy edits that keep the names
struct SkinBoneDef {
    QString fName;
    QTransform fBindTotal;  // bone rel -> scene at bind time
    QPointF fBindHead;      // scene-space head (bone local origin)
    QPointF fBindTail;      // scene-space tail
    qreal fBindAngle = 0.;  // scene-space bone direction (radians)
    qreal fRadius = 100.;   // influence radius in scene px at bind time
};

// all bind state of one skinned image layer; the palette bone pointers
// are resolved per evaluation, the serialized part is name+pose only
struct SkinBindData {
    QVector<SkinBoneDef> fDefs;
    SkinMesh fMesh;
    QTransform fBindBoxTotal;   // image rel -> scene at bind time

    bool hasBind() const { return !fDefs.isEmpty(); }
};

namespace SkinMeshGen {

// auto-generate a triangular-lattice mesh following the image alpha
// (port of AnimeEffects GridMeshCreator: staggered triangular cells
// whose existence follows alpha coverage, plus a simplified burr
// reduction that pulls boundary vertices in towards opaque pixels)
bool generate(const SkPixmap& pm, const int cellPx, SkinMesh& mesh);

// fallback when no pixels are available at bind time: a uniform grid
// over the whole image rectangle
void generateUniform(const int w, const int h, SkinMesh& mesh);

// compute per-vertex bone weights from the palette (capsule falloff
// with the AnimeEffects root/tail angular twist suppression); call
// after generate() with the palette captured at bind time
void computeWeights(SkinBindData& skin);

// collect the bone chain (root + bone descendants), depth-first
QList<Bone*> collectChain(Bone* const root);

// evaluate the deformed vertex positions (image/rel space) for the
// frame; bones are matched by name from the live chain. Returns false
// when no live bone matched (caller should draw undeformed)
bool evaluate(const SkinBindData& skin,
              Bone* const chainRoot,
              const qreal relFrame,
              const QTransform& boxTotal,
              QVector<SkPoint>& outPos);

} // namespace SkinMeshGen

#endif // SKINMESH_H

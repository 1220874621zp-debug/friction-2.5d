#ifndef SKINMESH_H
#define SKINMESH_H

#include "core_global.h"
#include "skia/skiaincludes.h"
#include "include/core/SkPixmap.h"

#include <QVector>
#include <QTransform>
#include <QString>

// Driver-agnostic skin deformation core: mesh + weights + math.
// Bones are ONE possible driver (resolved by the caller into
// SkinDriverPose entries); direct mesh manipulation can drive the
// same data without any bone involved.
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

// resolved CURRENT pose of one palette slot, parallel to fDefs; the
// caller maps whatever driver it uses onto these (a Bone fills them
// from its frame transform, other drivers could fill them directly)
struct SkinDriverPose {
    bool fValid = false;
    QPointF fHead;
    qreal fAngle = 0.;
    qreal fLen = 100.;
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
CORE_EXPORT bool generate(const SkPixmap& pm, const int cellPx, SkinMesh& mesh);

// fallback when no pixels are available at bind time: a uniform grid
// over the whole image rectangle
CORE_EXPORT void generateUniform(const int w, const int h, SkinMesh& mesh);

// compute per-vertex bone weights from the palette (capsule falloff
// with the AnimeEffects root/tail angular twist suppression); call
// after generate() with the palette captured at bind time
CORE_EXPORT void computeWeights(SkinBindData& skin);

// evaluate the deformed vertex positions (image/rel space) for the
// given driver poses (parallel to skin.fDefs). Returns false when no
// slot is valid (caller should draw undeformed)
CORE_EXPORT bool evaluate(const SkinBindData& skin,
                          const QVector<SkinDriverPose>& poses,
                          const QTransform& boxTotal,
                          QVector<SkPoint>& outPos);

// puppet-pin falloff weight: softness 0..1 shapes the curve
// (0 = tight grip with a hard hold core, 1 = widest/softest linear
// spread); 0.5 is the validated default (20% hold + cubic falloff)
CORE_EXPORT float pinWeight(const qreal dist, const qreal radius,
                            const qreal softness = 0.5);

// offline self-test of the whole core (mesh generation, weights,
// deformation math AND the drawVertices render call path used by
// ImageRenderData::drawSk); returns the number of failed checks
CORE_EXPORT int selfTest();

// load an image from disk and report what generate() does with it
// (color type, alpha samples, per-cellPx results) - diagnoses why
// the alpha lattice falls back to the uniform grid on real files
CORE_EXPORT void diagPng(const QString& path);
// narrow-char variant for external diagnostic runners
CORE_EXPORT void diagPngA(const char* path);

} // namespace SkinMeshGen

#endif // SKINMESH_H

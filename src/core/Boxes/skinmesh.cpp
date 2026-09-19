#include "Boxes/skinmesh.h"

#include "include/core/SkVertices.h"

#include <QtMath>
#include <QVector2D>
#include <QPointF>
#include <QLineF>
#include <QDebug>
#include <algorithm>

// ---------------------------------------------------------------------------
// mesh generation (port of AnimeEffects GridMeshCreator, GPLv3,
// https://github.com/AnimeEffectsDevs/AnimeEffects - simplified: the
// iterative rasterized burr refinement is replaced by the cheaper
// per-vertex opacity-probe shortening pass)
namespace {

const float kHalfSqrt3 = 0.8660254f;

struct Vtx {
    bool fExist = false;
    float fX = 0.f;
    float fY = 0.f;
    int fIndex = -1;
    // adjacent existing cells, clockwise from the right side (-1 = none)
    int fCell[6] = {-1, -1, -1, -1, -1, -1};
    QVector2D fReduceVec;
    float fMaxReduce = 0.f;
    float fReduceRate = 0.f;

    QVector2D pos() const { return QVector2D(fX, fY); }
    QVector2D posReduced() const
    {
        return pos() + fReduceVec * fMaxReduce * fReduceRate;
    }
};

struct Cell {
    bool fExist = false;
    bool fInverted = false;
    bool fNonReducing = false;
    float fX = 0.f;
    float fY = 0.f;
    int fV[3] = {-1, -1, -1};
};

class AlphaMap {
public:
    AlphaMap(const SkPixmap& pm)
        : mW(pm.width()), mH(pm.height())
        , mRaw(mW * mH, 0), mDilated(mW * mH, 0)
    {
        const auto ct = pm.colorType();
        if (ct == kN32_SkColorType && pm.addr32()) {
            for (int y = 0; y < mH; ++y) {
                for (int x = 0; x < mW; ++x) {
                    // alpha is the high byte of a premul N32 color for
                    // both the RGBA and the BGRA byte orders
                    mRaw[x + y * mW] =
                            quint8(*pm.addr32(x, y) >> 24);
                }
            }
        } else {
            for (int y = 0; y < mH; ++y) {
                for (int x = 0; x < mW; ++x) {
                    mRaw[x + y * mW] = SkColorGetA(pm.getColor(x, y));
                }
            }
        }
        // dilate by one pixel (AnimeEffects expandAlpha1Pixel): the
        // existence test then keeps cells that only touch the outline,
        // which avoids transparent seams along the mesh border
        mDilated = mRaw;
        for (int y = 0; y < mH; ++y) {
            const int row = y * mW;
            for (int x = 0; x < mW; ++x) {
                if (mRaw[row + x] <= 10) continue;
                for (int k = 0; k < 4; ++k) {
                    const int nx = x + (k == 0) - (k == 1);
                    const int ny = y + (k == 2) - (k == 3);
                    if (nx < 0 || mW <= nx || ny < 0 || mH <= ny) continue;
                    mDilated[ny * mW + nx] = 255;
                }
            }
        }
    }

    int width() const { return mW; }
    int height() const { return mH; }

    bool hasRawAlpha(const int x, const int y) const
    {
        return mRaw[x + y * mW] > 10;
    }

    bool hasAlpha(const int x, const int y) const
    {
        if (x < 0 || mW <= x || y < 0 || mH <= y) return false;
        return mDilated[x + y * mW] > 10;
    }

    bool hasSomeAlphaIn3x3(const int x, const int y) const
    {
        for (int i = -1; i < 2; ++i) {
            for (int k = -1; k < 2; ++k) {
                if (hasAlpha(x + i, y + k)) return true;
            }
        }
        return false;
    }

private:
    const int mW;
    const int mH;
    QVector<quint8> mRaw;
    QVector<quint8> mDilated;
};

struct Lattice {
    Lattice(const int cellPx)
        : fCellW(float(cellPx))
        , fCellH(float(cellPx) * kHalfSqrt3)
        , fHalfW(float(cellPx) * 0.5f) {}

    const float fCellW;
    const float fCellH;
    const float fHalfW;

    int tableW = 0;
    int tableH = 0;
    QVector<Cell> fCells;
    int fVtxW = 0;
    int fVtxH = 0;
    QVector<Vtx> fVertices;

    Cell& cell(const int x, const int y) { return fCells[x + tableW * y]; }
    const Cell& cell(const int x, const int y) const { return fCells[x + tableW * y]; }
    Vtx& vtx(const int x, const int y) { return fVertices[x + fVtxW * y]; }
    const Vtx& vtx(const int x, const int y) const { return fVertices[x + fVtxW * y]; }
};

bool cellHasOpa(const AlphaMap& img, const Cell& c, const Lattice& lat)
{
    // exact port of AnimeEffects GridMeshCreator::Image::
    // getOpaExistence - scan the pixels covered by the triangle half
    // of the staggered cell rectangle
    const int t = int(c.fY);
    const int b = int(c.fY + lat.fCellH);
    const int h = b - t;
    if (h <= 0) return false;

    for (int y = t; y <= b; ++y) {
        if (y < 0 || img.height() <= y) continue;
        const float offs = c.fInverted
                ? lat.fHalfW * float(y - t) / float(h)
                : lat.fHalfW * float(h - y + t) / float(h);
        const int l = int(c.fX + offs);
        const int r = int(c.fX + lat.fCellW - offs);
        for (int x = l; x <= r; ++x) {
            if (x < 0 || img.width() <= x) continue;
            if (img.hasRawAlpha(x, y)) return true;
        }
    }
    return false;
}

void buildLattice(const AlphaMap& img, const int cellPx, Lattice& lat)
{
    lat.tableW = int(img.width() / lat.fHalfW) + 2;
    lat.tableH = int(img.height() / lat.fCellH) + 1;
    lat.fCells.resize(lat.tableW * lat.tableH);

    for (int y = 0; y < lat.tableH; ++y) {
        const bool zalign = (y % 2 == 0);
        for (int x = 0; x < lat.tableW; ++x) {
            auto& c = lat.cell(x, y);
            c.fInverted = zalign != (x % 2 == 1);
            c.fX = lat.fHalfW * float(x - 1);
            c.fY = lat.fCellH * float(y);
            c.fExist = cellHasOpa(img, c, lat);
        }
    }

    lat.fVtxW = (lat.tableW + 1) / 2 + 1;
    lat.fVtxH = lat.tableH + 1;
    lat.fVertices.resize(lat.fVtxW * lat.fVtxH);
    for (int y = 0; y < lat.fVtxH; ++y) {
        const bool zalign = (y % 2 == 0);
        const float zoffs = zalign ? -lat.fHalfW : 0.f;
        for (int x = 0; x < lat.fVtxW; ++x) {
            auto& v = lat.vtx(x, y);
            v.fX = lat.fCellW * float(x) + zoffs;
            v.fY = lat.fCellH * float(y);
        }
    }

    // connect existing cells to their three lattice vertices (exact
    // port of AnimeEffects GridMeshCreator::CellTable::
    // connectCellsToVertices - the staggered hexagon wiring)
    for (int y = 0; y < lat.tableH; ++y) {
        const bool zalign = (y % 2 == 0);
        for (int x = 0; x < lat.tableW; ++x) {
            const int index = x + lat.tableW * y;
            auto& c = lat.cell(x, y);
            if (!c.fExist) continue;

            int v[3] = {-1, -1, -1};
            if (c.fInverted) {
                const int hx = x / 2;
                if (zalign) {
                    v[0] = hx + y * lat.fVtxW;
                    v[1] = (hx + 1) + y * lat.fVtxW;
                    v[2] = hx + (y + 1) * lat.fVtxW;
                } else {
                    v[0] = hx + y * lat.fVtxW;
                    v[1] = (hx + 1) + y * lat.fVtxW;
                    v[2] = (hx + 1) + (y + 1) * lat.fVtxW;
                }
                lat.fVertices[v[0]].fCell[0] = index;
                lat.fVertices[v[1]].fCell[2] = index;
                lat.fVertices[v[2]].fCell[4] = index;
            } else {
                if (zalign) {
                    const int hx = (x + 1) / 2;
                    v[0] = hx + y * lat.fVtxW;
                    v[1] = hx + (y + 1) * lat.fVtxW;
                    v[2] = (hx - 1) + (y + 1) * lat.fVtxW;
                } else {
                    const int hx = x / 2;
                    v[0] = hx + y * lat.fVtxW;
                    v[1] = (hx + 1) + (y + 1) * lat.fVtxW;
                    v[2] = hx + (y + 1) * lat.fVtxW;
                }
                lat.fVertices[v[0]].fCell[1] = index;
                lat.fVertices[v[1]].fCell[3] = index;
                lat.fVertices[v[2]].fCell[5] = index;
            }
            for (int i = 0; i < 3; ++i) {
                lat.fVertices[v[i]].fExist = true;
                c.fV[i] = v[i];
            }
        }
    }
}

bool segmentsFacing(const QVector2D& aStart, const QVector2D& aDir,
                    const QVector2D& bStart, const QVector2D& bDir)
{
    if (QVector2D::dotProduct(aDir, bDir) >= 0.f) return false;
    return QVector2D::dotProduct(bStart - aStart, aDir) > 0.f;
}

void setReducingVectors(Lattice& lat)
{
    // pull direction of a boundary vertex: the average of the unit
    // directions towards its adjacent existing cells, scaled to the
    // cell height (AnimeEffects VertexTable::setReducingVectors)
    const float cellH = lat.fCellH;
    const float pairTriH = cellH * kHalfSqrt3;
    const float cellW = lat.fCellW;
    QVector2D vecs[6];
    for (int i = 0; i < 6; ++i) {
        const float angle = float(M_PI / 6.0 + i * M_PI / 3.0);
        vecs[i] = QVector2D(std::cos(angle) * cellH, std::sin(angle) * cellH);
    }

    for (auto& v : lat.fVertices) {
        QVector2D vec;
        int count = 0;
        for (int k = 0; k < 6; ++k) {
            if (v.fCell[k] != -1) {
                vec += vecs[k];
                ++count;
            }
        }
        if (count == 0) continue;
        v.fReduceVec = vec / float(count);

        const float veclen = v.fReduceVec.length();
        if (count == 2 && veclen >= pairTriH * 0.95f) {
            v.fReduceVec *= cellW / veclen;
        } else if (count == 3 && veclen >= cellW * 0.57f) {
            v.fReduceVec *= cellH / veclen;
        } else if (count == 4 && veclen >= pairTriH * 0.45f) {
            v.fReduceVec *= cellW / veclen;
        }

        v.fMaxReduce = v.fReduceVec.length();
        if (v.fMaxReduce >= 1.f) {
            v.fReduceVec /= v.fMaxReduce;
        } else {
            v.fReduceVec = QVector2D();
            v.fMaxReduce = 0.f;
        }
    }
}

void reduceBurrs(Lattice& lat, const AlphaMap& img)
{
    setReducingVectors(lat);

    // clamp opposing reduce vectors so a triangle cannot flip
    const float halfReduce = lat.fCellW * 0.5f;
    for (int y = 0; y < lat.tableH; ++y) {
        for (int x = 0; x < lat.tableW; ++x) {
            auto& c = lat.cell(x, y);
            if (!c.fExist) continue;
            Vtx* tv[3] = {&lat.fVertices[c.fV[0]],
                          &lat.fVertices[c.fV[1]],
                          &lat.fVertices[c.fV[2]]};
            if (tv[0]->fMaxReduce <= 0.f && tv[1]->fMaxReduce <= 0.f &&
                tv[2]->fMaxReduce <= 0.f) {
                c.fNonReducing = true;
                continue;
            }
            for (int i = 0; i < 3; ++i) {
                for (int j = i + 1; j < 3; ++j) {
                    if (!segmentsFacing(tv[i]->pos(), tv[i]->fReduceVec,
                                        tv[j]->pos(), tv[j]->fReduceVec)) {
                        continue;
                    }
                    if (tv[i]->fMaxReduce > 0.f) tv[i]->fMaxReduce = halfReduce;
                    if (tv[j]->fMaxReduce > 0.f) tv[j]->fMaxReduce = halfReduce;
                }
            }
        }
    }

    // shorten the reduce vector while the pulled-in vertex would land
    // on fully transparent ground (keeps opaque pixels covered)
    for (auto& v : lat.fVertices) {
        if (!v.fExist || v.fMaxReduce <= 0.f) continue;
        v.fReduceRate = 0.95f;
        const float maxReduce = v.fMaxReduce;
        for (int div = 0; div < 9; ++div) {
            const auto pos = v.posReduced();
            if (!img.hasSomeAlphaIn3x3(int(pos.x()), int(pos.y()))) break;
            v.fMaxReduce = (1.f - (div + 1) * 0.125f) * maxReduce;
        }
    }
}

} // namespace

namespace SkinMeshGen {

bool generate(const SkPixmap& pm, const int cellPx, SkinMesh& mesh)
{
    if (pm.width() <= 0 || pm.height() <= 0) return false;

    AlphaMap img(pm);
    Lattice lat(cellPx);
    buildLattice(img, cellPx, lat);
    reduceBurrs(lat, img);

    // flatten: assign indices to existing vertices in table order
    int index = 0;
    for (auto& v : lat.fVertices) {
        if (v.fExist) {
            v.fIndex = index;
            ++index;
        }
    }
    const int vtxCount = index;
    if (vtxCount < 3 || vtxCount >= 65536) return false;

    mesh.fPos.clear();
    mesh.fIndices.clear();
    mesh.fPos.reserve(vtxCount);
    for (const auto& v : lat.fVertices) {
        if (!v.fExist) continue;
        const auto pos = v.posReduced();
        mesh.fPos.append(SkPoint::Make(pos.x(), pos.y()));
    }
    for (int y = 0; y < lat.tableH; ++y) {
        for (int x = 0; x < lat.tableW; ++x) {
            const auto& c = lat.cell(x, y);
            if (!c.fExist) continue;
            for (int i = 0; i < 3; ++i) {
                mesh.fIndices.append(
                            uint16_t(lat.fVertices[c.fV[i]].fIndex));
            }
        }
    }
    mesh.fW.clear();
    mesh.fW.resize(vtxCount);
    mesh.fCellPx = cellPx;
    mesh.fImgW = pm.width();
    mesh.fImgH = pm.height();
    return mesh.isValid();
}

void generateUniform(const int w, const int h, SkinMesh& mesh)
{
    mesh.fPos.clear();
    mesh.fIndices.clear();
    mesh.fW.clear();
    const int nx = qMax(2, qMin(24, w / 32 + 2));
    const int ny = qMax(2, qMin(24, h / 32 + 2));
    for (int y = 0; y <= ny; ++y) {
        for (int x = 0; x <= nx; ++x) {
            mesh.fPos.append(SkPoint::Make(
                        float(w) * float(x) / float(nx),
                        float(h) * float(y) / float(ny)));
        }
    }
    const auto id = [nx](const int x, const int y) { return x + y * (nx + 1); };
    for (int y = 0; y < ny; ++y) {
        for (int x = 0; x < nx; ++x) {
            mesh.fIndices.append(uint16_t(id(x, y)));
            mesh.fIndices.append(uint16_t(id(x + 1, y)));
            mesh.fIndices.append(uint16_t(id(x + 1, y + 1)));
            mesh.fIndices.append(uint16_t(id(x, y)));
            mesh.fIndices.append(uint16_t(id(x + 1, y + 1)));
            mesh.fIndices.append(uint16_t(id(x, y + 1)));
        }
    }
    mesh.fW.resize(mesh.fPos.count());
    mesh.fCellPx = -1;
    mesh.fImgW = w;
    mesh.fImgH = h;
}

namespace {

// angular falloff away from the bone axis (AnimeEffects BoneShape
// twist weight, fixed PI/4 wing variant): full within +-45 degrees of
// the bone direction, linearly fading to zero directly behind it - a
// joint area pixel then prefers the bone it points along
float wingOutness(const float angleDiff)
{
    const float kWing = float(M_PI * 0.25);
    const float kWingInv = float(M_PI * 0.75);
    const float outress = 1.f - std::max(std::abs(angleDiff) - kWing, 0.f) / kWingInv;
    return outress;
}

float normalizeAngle(float a)
{
    while (a > M_PI) a -= float(2. * M_PI);
    while (a < -M_PI) a += float(2. * M_PI);
    return a;
}

struct RelBone {
    QPointF fStart;
    QPointF fDir;     // unit
    float fLen;
    float fAngle;     // radians
    float fRadius;
};

float boneWeightAt(const RelBone& b, const QPointF& p)
{
    const QVector2D d(float(p.x() - b.fStart.x()),
                      float(p.y() - b.fStart.y()));
    if (d.isNull()) return 0.f;

    // twist suppression at the root and the tail (AnimeEffects
    // BoneShape: BOTH vectors point FROM the joint TO the point -
    // reversing the tail one suppresses the whole bone interior,
    // which blends overlapping bones 50/50 and dilutes deformation)
    float twist = 1.f;
    const float rootDiff = normalizeAngle(
                float(std::atan2(d.y(), d.x())) - b.fAngle);
    twist *= wingOutness(rootDiff) * wingOutness(rootDiff);
    const QPointF tailPt(b.fStart.x() + b.fDir.x() * b.fLen,
                         b.fStart.y() + b.fDir.y() * b.fLen);
    const QVector2D e(float(p.x() - tailPt.x()),
                      float(p.y() - tailPt.y()));
    if (!e.isNull()) {
        const float tailDiff = normalizeAngle(
                    float(std::atan2(e.y(), e.x())) - (b.fAngle + float(M_PI)));
        twist *= wingOutness(tailDiff) * wingOutness(tailDiff);
    }

    // capsule falloff
    float nearness = 0.f;
    if (b.fRadius >= 1.f) {
        const float t = QVector2D::dotProduct(d, QVector2D(b.fDir)) / b.fLen;
        float dist;
        if (t < 0.f) {
            dist = d.length();
        } else if (t <= 1.f) {
            const QVector2D proj = QVector2D(b.fDir) * (t * b.fLen);
            dist = (d - proj).length();
        } else {
            dist = e.length();
        }
        nearness = std::max(1.f - dist / b.fRadius, 0.f);
    }

    const float ratio = 0.3f * nearness * nearness;
    twist = ratio + (1.f - ratio) * twist;
    return twist * nearness;
}

float boneDistAt(const RelBone& b, const QPointF& p)
{
    const QPointF d = p - b.fStart;
    const float t = float(QPointF::dotProduct(d, QPointF(b.fDir))) / b.fLen;
    const float tc = qBound(0.f, t, 1.f);
    const QPointF proj(b.fStart.x() + b.fDir.x() * tc * b.fLen,
                       b.fStart.y() + b.fDir.y() * tc * b.fLen);
    return float(QLineF(p, proj).length());
}

} // namespace

void computeWeights(SkinBindData& skin)
{
    const auto& defs = skin.fDefs;
    auto& mesh = skin.fMesh;
    if (!mesh.isValid() || defs.isEmpty()) return;

    // map the bind-time bones into image/rel space
    const QTransform invL = skin.fBindBoxTotal.inverted();
    const qreal det = skin.fBindBoxTotal.determinant();
    const qreal s = det != 0. ? std::sqrt(std::abs(det)) : 1.;
    QVector<RelBone> rels;
    rels.reserve(defs.count());
    for (const auto& def : defs) {
        RelBone rb;
        rb.fStart = invL.map(def.fBindHead);
        const QPointF tail = invL.map(def.fBindTail);
        const QPointF dir = tail - rb.fStart;
        rb.fLen = float(QLineF(rb.fStart, tail).length());
        if (rb.fLen < 1.f) {
            // degenerate bone: treat as a disc
            rb.fDir = QPointF(1., 0.);
            rb.fLen = 1.f;
            rb.fAngle = 0.f;
        } else {
            rb.fDir = dir / rb.fLen;
            rb.fAngle = float(std::atan2(dir.y(), dir.x()));
        }
        rb.fRadius = float(def.fRadius / (s > 1e-9 ? s : 1.));
        rels.append(rb);
    }

    const bool single = rels.count() == 1;
    for (int vi = 0; vi < mesh.fPos.count(); ++vi) {
        const QPointF p(mesh.fPos[vi].x(), mesh.fPos[vi].y());
        auto& vw = mesh.fW[vi];

        int bestIdx[4] = {-1, -1, -1, -1};
        float bestW[4] = {0.f, 0.f, 0.f, 0.f};
        int bestCount = 0;

        if (single) {
            bestIdx[0] = 0;
            bestW[0] = 1.f;
            bestCount = 1;
        } else {
            for (int bi = 0; bi < rels.count(); ++bi) {
                const float w = boneWeightAt(rels[bi], p);
                if (w <= 0.0001f) continue;
                // insert into the top-4 list
                int slot = bestCount < 4 ? bestCount : 3;
                if (bestCount < 4) ++bestCount;
                else if (w <= bestW[3]) continue;
                else slot = 3;
                while (slot > 0 && bestW[slot - 1] < w) {
                    bestIdx[slot] = bestIdx[slot - 1];
                    bestW[slot] = bestW[slot - 1];
                    --slot;
                }
                bestIdx[slot] = bi;
                bestW[slot] = w;
            }
            if (bestCount == 0) {
                // outside every influence: follow the nearest bone so
                // stray vertices tear off instead of staying behind
                int nearest = 0;
                float nd = boneDistAt(rels[0], p);
                for (int bi = 1; bi < rels.count(); ++bi) {
                    const float d = boneDistAt(rels[bi], p);
                    if (d < nd) { nd = d; nearest = bi; }
                }
                bestIdx[0] = nearest;
                bestW[0] = 1.f;
                bestCount = 1;
            }
        }

        float sum = 0.f;
        for (int k = 0; k < bestCount; ++k) sum += bestW[k];
        if (sum <= 0.f) sum = 1.f;
        for (int k = 0; k < bestCount; ++k) {
            vw.fIdx[k] = bestIdx[k];
            vw.fW[k] = bestW[k] / sum;
        }
        vw.fCount = bestCount;
    }
}

bool evaluate(const SkinBindData& skin,
              const QVector<SkinDriverPose>& poses,
              const QTransform& boxTotal,
              QVector<SkPoint>& outPos)
{
    outPos = skin.fMesh.fPos;
    if (!skin.fMesh.isValid()) return false;
    if (poses.count() != skin.fDefs.count()) return false;
    int liveCount = 0;
    for (const auto& pose : poses) {
        if (pose.fValid) ++liveCount;
    }
    if (liveCount == 0) return false;

    // per-slot rel-space skin matrix
    // R_b = L_cur^-1 * M_b * L_bind, where M_b (scene space) is the
    // AnimeEffects PosePalette 2D formula: rotate the slot's delta
    // angle around its bind head, then move to the current head
    const QTransform invCur = boxTotal.inverted();
    QVector<QTransform> mats(skin.fDefs.count());
    for (int i = 0; i < skin.fDefs.count(); ++i) {
        const auto& pose = poses[i];
        if (!pose.fValid) continue;
        const auto& def = skin.fDefs[i];

        const qreal dAng = pose.fAngle - def.fBindAngle;
        const qreal c = std::cos(dAng);
        const qreal s = std::sin(dAng);
        const QPointF bh = def.fBindHead;
        const QTransform m(c, s, -s, c,
                           pose.fHead.x() - (c * bh.x() - s * bh.y()),
                           pose.fHead.y() - (s * bh.x() + c * bh.y()));
        mats[i] = invCur * m * skin.fBindBoxTotal;
    }

    // linear blend skinning per vertex
    for (int vi = 0; vi < skin.fMesh.fPos.count(); ++vi) {
        const auto& vw = skin.fMesh.fW[vi];
        if (vw.fCount <= 0) continue;
        const QPointF p(skin.fMesh.fPos[vi].x(),
                        skin.fMesh.fPos[vi].y());
        qreal x = 0.;
        qreal y = 0.;
        qreal wsum = 0.;
        for (int k = 0; k < vw.fCount; ++k) {
            if (!poses[vw.fIdx[k]].fValid) continue;
            const QPointF mp = mats[vw.fIdx[k]].map(p);
            x += mp.x() * vw.fW[k];
            y += mp.y() * vw.fW[k];
            wsum += vw.fW[k];
        }
        if (wsum > 1e-6) {
            outPos[vi] = SkPoint::Make(float(x / wsum), float(y / wsum));
        }
    }
    return true;
}

int selfTest()
{
    int fails = 0;
    const auto check = [&fails](const bool cond, const char* what) {
        if (!cond) {
            ++fails;
            qWarning() << "[SKINTEST] FAIL:" << what;
        } else {
            qDebug() << "[SKINTEST] ok:" << what;
        }
    };

    // ---- 1. mesh generation from a synthetic alpha shape ----
    const int W = 256, H = 256;
    SkBitmap bmp;
    bmp.allocN32Pixels(W, H);
    bmp.eraseColor(SK_ColorTRANSPARENT);
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const int dx = x - 128, dy = y - 128;
            if (dx * dx + dy * dy <= 80 * 80) {
                *bmp.getAddr32(x, y) = SkColorSetARGB(255, 200, 40, 40);
            }
        }
    }
    SkPixmap pm;
    bmp.peekPixels(&pm);

    SkinBindData skin;
    check(generate(pm, 20, skin.fMesh), "generate ok");
    check(skin.fMesh.fPos.count() > 50, "vertex count plausible");
    check(skin.fMesh.fIndices.count() % 3 == 0, "index count multiple of 3");
    bool inRange = true;
    bool idxOk = true;
    for (const auto& p : skin.fMesh.fPos) {
        if (p.x() < -40.f || p.x() > 300.f ||
            p.y() < -40.f || p.y() > 300.f) inRange = false;
    }
    for (const auto i : skin.fMesh.fIndices) {
        if (i >= skin.fMesh.fPos.count()) idxOk = false;
    }
    check(inRange, "vertices within image bounds + margin");
    check(idxOk, "indices in range");
    float maxR = 0.f;
    for (const auto& p : skin.fMesh.fPos) {
        maxR = std::max(maxR,
                        std::hypot(p.x() - 128.f, p.y() - 128.f));
    }
    check(maxR < 95.f, "mesh hugs alpha edge (burr reduction)");

    // ---- 2. weights: two slots across the circle ----
    skin.fBindBoxTotal = QTransform();
    SkinBoneDef defA;
    defA.fName = QStringLiteral("A");
    defA.fBindHead = QPointF(48, 128);
    defA.fBindTail = QPointF(128, 128);
    defA.fBindAngle = 0.;
    defA.fRadius = 60.;
    SkinBoneDef defB;
    defB.fName = QStringLiteral("B");
    defB.fBindHead = QPointF(128, 128);
    defB.fBindTail = QPointF(208, 128);
    defB.fBindAngle = 0.;
    defB.fRadius = 60.;
    skin.fDefs = { defA, defB };
    computeWeights(skin);

    const auto nearestVtx = [&skin](const QPointF& p) {
        int best = 0;
        float bd = 1e9f;
        for (int i = 0; i < skin.fMesh.fPos.count(); ++i) {
            const float d = QLineF(p, QPointF(skin.fMesh.fPos[i].x(),
                                              skin.fMesh.fPos[i].y())).length();
            if (d < bd) { bd = d; best = i; }
        }
        return best;
    };
    const int iL = nearestVtx(QPointF(70, 128));
    const int iR = nearestVtx(QPointF(186, 128));
    const auto& wL = skin.fMesh.fW[iL];
    const auto& wR = skin.fMesh.fW[iR];
    check(wL.fIdx[0] == 0 && wL.fW[0] > 0.7f,
          "left-half vertex dominated by slot A");
    check(wR.fIdx[0] == 1 && wR.fW[0] > 0.7f,
          "right-half vertex dominated by slot B");
    bool sumsOk = true;
    bool countsOk = true;
    for (const auto& vw : skin.fMesh.fW) {
        float sum = 0.f;
        for (int k = 0; k < vw.fCount; ++k) {
            sum += vw.fW[k];
            if (vw.fIdx[k] < 0 || vw.fIdx[k] > 1) countsOk = false;
        }
        if (qAbs(sum - 1.f) > 0.02f) sumsOk = false;
    }
    check(sumsOk, "all vertex weights sum to 1");
    check(countsOk, "all palette indices valid");

    // ---- 3. deformation math: rotate slot B +90 deg around its head
    QVector<SkinDriverPose> poses(2);
    poses[0].fValid = true;
    poses[0].fHead = QPointF(48, 128);
    poses[0].fAngle = 0.;
    poses[0].fLen = 80.;
    poses[1].fValid = true;
    poses[1].fHead = QPointF(128, 128);
    poses[1].fAngle = M_PI / 2.;
    poses[1].fLen = 80.;

    QVector<SkPoint> out;
    check(evaluate(skin, poses, QTransform(), out),
          "evaluate ok (2 valid poses)");
    const SkPoint bR = skin.fMesh.fPos[iR];
    const SkPoint aR = out[iR];
    // expected: rotation of (v - head) by +90 deg around head (128,128)
    const float exR = 128.f - (bR.y() - 128.f);
    const float eyR = 128.f + (bR.x() - 128.f);
    check(qAbs(aR.x() - exR) < 6.f && qAbs(aR.y() - eyR) < 6.f,
          "right vertex rotated 90deg to expected position");
    const SkPoint bL = skin.fMesh.fPos[iL];
    const SkPoint aL = out[iL];
    check(QLineF(QPointF(aL.x(), aL.y()),
                 QPointF(bL.x(), bL.y())).length() < 3.f,
          "left vertex stays put (slot A unchanged)");
    bool noNaN = true;
    for (const auto& p : out) {
        if (std::isnan(p.x()) || std::isnan(p.y())) noNaN = false;
    }
    check(noNaN, "no NaN in deformed positions");

    // ---- 4. render path: the exact drawSk call sequence ----
    const sk_sp<SkImage> img = SkImage::MakeFromBitmap(bmp);
    check(img != nullptr, "source image from bitmap");
    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setFilterQuality(kMedium_SkFilterQuality);
    paint.setShader(img->makeShader(SkTileMode::kClamp,
                                    SkTileMode::kClamp,
                                    nullptr));
    const sk_sp<SkVertices> vertices = SkVertices::MakeCopy(
                SkVertices::kTriangles_VertexMode,
                out.count(), out.constData(),
                skin.fMesh.fPos.constData(), nullptr,
                skin.fMesh.fIndices.count(),
                skin.fMesh.fIndices.constData());
    const sk_sp<SkSurface> surf = SkSurface::MakeRasterN32Premul(W, H);
    surf->getCanvas()->drawVertices(vertices.get(),
                                    SkBlendMode::kModulate, paint);
    SkPixmap dst;
    surf->peekPixels(&dst);
    const SkColor cMoved = dst.getColor(128, 186);
    // the rotated half lands BELOW the pivot and spans x 48..208, so
    // the old center row (186,128) is legitimately covered by content
    // swept in from the top of the disc; the upper-right quadrant
    // (e.g. 180,90) is outside every covered region
    const SkColor cGone = dst.getColor(180, 90);
    const SkColor cStay = dst.getColor(70, 128);
    check(SkColorGetA(cMoved) > 200 && SkColorGetR(cMoved) > 150,
          "rotated half renders opaque red at its new position");
    if (SkColorGetA(cGone) >= 120) {
        QString row;
        for (int x = 140; x <= 210; x += 10) {
            row += QStringLiteral(" %1:%2").arg(x)
                    .arg(SkColorGetA(dst.getColor(x, 90)));
        }
        qWarning() << "[SKINTEST]   residue row y=90 (x:alpha):" << row;
    }
    check(SkColorGetA(cGone) < 120,
          "upper-right quadrant empty after rotation");
    check(SkColorGetA(cStay) > 200,
          "unchanged left half still renders");

    qDebug() << "[SKINTEST] ===" << (fails == 0 ? "ALL PASS" :
          QString("%1 FAILED").arg(fails)) << "===";
    return fails;
}

} // namespace SkinMeshGen

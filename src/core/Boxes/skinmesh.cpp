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
    mesh.fCellPx = cellPx;
    mesh.fImgW = pm.width();
    mesh.fImgH = pm.height();
    return mesh.isValid();
}

void generateUniform(const int w, const int h, SkinMesh& mesh)
{
    mesh.fPos.clear();
    mesh.fIndices.clear();
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
    mesh.fCellPx = -1;
    mesh.fImgW = w;
    mesh.fImgH = h;
}

float pinWeight(const qreal dist, const qreal radius,
                const qreal softness)
{
    if (radius <= 1. || dist >= radius) return 0.f;
    // softness shapes BOTH the hold zone and the falloff exponent,
    // anchored so 0.5 reproduces the validated default exactly:
    //   hold = 0.4R*s    : 0     .. 0.2R  .. 0.4R
    //   exp  = 1 + 4*s   : 1     .. 3     .. 5
    // hard (0) = no hold + steep quintic (tight grip, quick release),
    // soft (1) = wide 40% hold + linear spread (gradual, widest blend)
    const qreal s = qBound(0., softness, 1.);
    const qreal hold = 0.4 * radius * s;
    if (dist <= hold) return 1.f;
    const qreal t = (dist - hold) / (radius - hold);
    return float(std::pow(1. - t, 5. - 4. * s));
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

    // ---- 2. puppet-pin falloff curve ----
    check(pinWeight(0., 800.) == 1.f, "pinWeight full at the pin");
    check(pinWeight(160., 800.) == 1.f, "pinWeight hold zone (20%)");
    check(pinWeight(800., 800.) <= 0.f, "pinWeight zero at the radius");
    check(pinWeight(900., 800.) == 0.f, "pinWeight zero beyond radius");
    check(qAbs(pinWeight(400., 800., 0.5) -
               std::pow(1. - 0.375, 3.)) < 1e-4,
          "pinWeight softness 0.5 = cubic default");
    check(pinWeight(0.3 * 800., 800., 1.) == 1.f,
          "softness 1 keeps its wide 40% hold core");
    check(pinWeight(1., 800., 0.) < 1.f,
          "softness 0 has no hold zone");
    check(pinWeight(400., 800., 1.) > pinWeight(400., 800., 0.5) &&
          pinWeight(400., 800., 0.5) > pinWeight(400., 800., 0.),
          "higher softness = softer/wider influence");

    // ---- 3. pin deformation math + render (the ImageBox loop) ----
    {
        // pin at the circle center, dragged by (60, 40), radius 150:
        // content at the pin tracks exactly, far corners stay put
        const QPointF bind(128, 128);
        const QPointF delta(60, 40);
        const double radius = 150.;

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
        const int vAt = nearestVtx(bind);
        check(QLineF(bind, QPointF(skin.fMesh.fPos[vAt].x(),
                                   skin.fMesh.fPos[vAt].y())).length()
              < 12.f, "a lattice vertex sits near the pin");

        QVector<SkPoint> pos = skin.fMesh.fPos;
        for (int vi = 0; vi < pos.count(); ++vi) {
            const auto& rest = skin.fMesh.fPos[vi];
            const float w = pinWeight(
                        QLineF(bind, QPointF(rest.x(), rest.y())).length(),
                        radius, 0.5);
            if (w <= 0.f) continue;
            pos[vi] += SkPoint::Make(float(delta.x()) * w,
                                     float(delta.y()) * w);
        }
        const SkPoint bAt = skin.fMesh.fPos[vAt];
        const SkPoint aAt = pos[vAt];
        check(qAbs(aAt.x() - bAt.x() - 60.f) < 6.f &&
              qAbs(aAt.y() - bAt.y() - 40.f) < 6.f,
              "pin vertex tracks the drag exactly (hold zone)");
        bool noNaN = true;
        for (const auto& p : pos) {
            if (std::isnan(p.x()) || std::isnan(p.y())) noNaN = false;
        }
        check(noNaN, "no NaN in deformed positions");

        // render: the exact drawSk call sequence
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
                    pos.count(), pos.constData(),
                    skin.fMesh.fPos.constData(), nullptr,
                    skin.fMesh.fIndices.count(),
                    skin.fMesh.fIndices.constData());
        const sk_sp<SkSurface> surf = SkSurface::MakeRasterN32Premul(W, H);
        surf->getCanvas()->drawVertices(vertices.get(),
                                        SkBlendMode::kModulate, paint);
        SkPixmap dst;
        surf->peekPixels(&dst);
        // center content dragged to (188, 168); the right edge of the
        // canvas lies outside the pin radius and stays empty
        const SkColor cMoved = dst.getColor(188, 168);
        const SkColor cEmpty = dst.getColor(250, 128);
        check(SkColorGetA(cMoved) > 200 && SkColorGetR(cMoved) > 150,
              "pin-dragged content renders at its new position");
        check(SkColorGetA(cEmpty) < 10,
              "beyond-radius corner stays empty");
    }

    qDebug() << "[SKINTEST] ===" << (fails == 0 ? "ALL PASS" :
          QString("%1 FAILED").arg(fails)) << "===";
    return fails;
}

void diagPng(const QString& path)
{
    const auto data = SkData::MakeFromFileName(path.toStdString().c_str());
    if (!data) {
        qWarning() << "[SKINDIAG] cannot read" << path;
        return;
    }
    const sk_sp<SkImage> encoded = SkImage::MakeFromEncoded(data);
    if (!encoded) {
        qWarning() << "[SKINDIAG] decode failed" << path;
        return;
    }
    // MakeFromEncoded returns a lazy codec image - materialize like
    // ImageBox::skinGenerateMesh does
    const sk_sp<SkImage> img = encoded->makeRasterImage();
    qDebug() << "[SKINDIAG]" << path
             << "size" << img->width() << "x" << img->height();
    SkPixmap pm;
    if (!img->peekPixels(&pm)) {
        qWarning() << "[SKINDIAG] peekPixels FAILED (texture-backed?)"
                   << "colorType" << int(img->colorType());
        return;
    }
    qDebug() << "[SKINDIAG] colorType" << int(pm.colorType())
             << "alphaType" << int(pm.alphaType());
    for (const auto& pt : {QPoint(1, 1),
                           QPoint(pm.width() / 2, pm.height() / 2),
                           QPoint(pm.width() - 2, pm.height() - 2)}) {
        qDebug() << "[SKINDIAG]   alpha at" << pt << "="
                 << SkColorGetA(pm.getColor(pt.x(), pt.y()));
    }
    for (int cellPx = 20; cellPx <= 320; cellPx *= 2) {
        SkinMesh mesh;
        const bool ok = generate(pm, cellPx, mesh);
        qDebug() << "[SKINDIAG]   cellPx" << cellPx
                 << (ok ? "OK" : "FAIL")
                 << "verts" << mesh.fPos.count()
                 << "tris" << mesh.fIndices.count() / 3;
        if (ok) break;
    }
}

void diagPngA(const char* path)
{
    diagPng(QString::fromLocal8Bit(path));
}

} // namespace SkinMeshGen

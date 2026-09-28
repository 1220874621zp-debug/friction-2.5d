#include "lyricmotioncanvas.h"

#include <QJSEngine>
#include <QQmlEngine>

#include <QBrush>
#include <QColor>
#include <QConicalGradient>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QLinearGradient>
#include <QPen>
#include <QRadialGradient>
#include <QRegularExpression>
#include <QVariantMap>
#include <QtMath>
#include <cmath>
#include <cstdio>

namespace {

QPainter::CompositionMode compositeMode(const QString &name) {
    if (name == QStringLiteral("copy")) { return QPainter::CompositionMode_Source; }
    if (name == QStringLiteral("source-in")) { return QPainter::CompositionMode_SourceIn; }
    if (name == QStringLiteral("source-out")) { return QPainter::CompositionMode_SourceOut; }
    if (name == QStringLiteral("source-atop")) { return QPainter::CompositionMode_SourceAtop; }
    if (name == QStringLiteral("destination-over")) { return QPainter::CompositionMode_DestinationOver; }
    if (name == QStringLiteral("destination-in")) { return QPainter::CompositionMode_DestinationIn; }
    if (name == QStringLiteral("destination-out")) { return QPainter::CompositionMode_DestinationOut; }
    if (name == QStringLiteral("destination-atop")) { return QPainter::CompositionMode_DestinationAtop; }
    if (name == QStringLiteral("lighter")) { return QPainter::CompositionMode_Plus; }
    if (name == QStringLiteral("multiply")) { return QPainter::CompositionMode_Multiply; }
    if (name == QStringLiteral("screen")) { return QPainter::CompositionMode_Screen; }
    if (name == QStringLiteral("overlay")) { return QPainter::CompositionMode_Overlay; }
    if (name == QStringLiteral("darken")) { return QPainter::CompositionMode_Darken; }
    if (name == QStringLiteral("lighten")) { return QPainter::CompositionMode_Lighten; }
    if (name == QStringLiteral("color-dodge")) { return QPainter::CompositionMode_ColorDodge; }
    if (name == QStringLiteral("color-burn")) { return QPainter::CompositionMode_ColorBurn; }
    if (name == QStringLiteral("hard-light")) { return QPainter::CompositionMode_HardLight; }
    if (name == QStringLiteral("soft-light")) { return QPainter::CompositionMode_SoftLight; }
    if (name == QStringLiteral("difference")) { return QPainter::CompositionMode_Difference; }
    if (name == QStringLiteral("exclusion")) { return QPainter::CompositionMode_Exclusion; }
    // HSL blend modes have no QPainter equivalent; callers (keyFinish,
    // grain) are not part of the preview path
    return QPainter::CompositionMode_SourceOver;
}

bool compositeIgnored(const QString &name) {
    return name == QStringLiteral("saturation")
        || name == QStringLiteral("hue")
        || name == QStringLiteral("color")
        || name == QStringLiteral("luminosity");
}

QColor parseColor(const QVariant &v) {
    if (!v.canConvert<QString>()) { return QColor(Qt::black); }
    const QString s = v.toString().trimmed();
    if (s.isEmpty() || s == QStringLiteral("transparent")
        || s == QStringLiteral("none")) { return Qt::transparent; }
    QColor c(s);
    if (c.isValid()) { return c; }
    static const QRegularExpression rgba(
        QStringLiteral("^rgba?\\(([^)]+)\\)$"),
        QRegularExpression::CaseInsensitiveOption);
    const auto m = rgba.match(s);
    if (m.hasMatch()) {
        const QStringList parts = m.captured(1).split(QLatin1Char(','));
        if (parts.size() >= 3) {
            return QColor(qBound(0, qRound(parts.at(0).toDouble()), 255),
                          qBound(0, qRound(parts.at(1).toDouble()), 255),
                          qBound(0, qRound(parts.at(2).toDouble()), 255),
                          parts.size() > 3
                              ? qBound(0, qRound(parts.at(3).toDouble() * 255), 255)
                              : 255);
        }
    }
    // canvas color keywords the render path may emit
    static const QHash<QString, QRgb> kNamed = {
        {QStringLiteral("white"), qRgb(255, 255, 255)},
        {QStringLiteral("black"), qRgb(0, 0, 0)},
        {QStringLiteral("red"), qRgb(255, 0, 0)},
        {QStringLiteral("green"), qRgb(0, 128, 0)},
        {QStringLiteral("blue"), qRgb(0, 0, 255)},
        {QStringLiteral("yellow"), qRgb(255, 255, 0)},
    };
    const auto it = kNamed.find(s.toLower());
    if (it != kNamed.end()) { return QColor(*it); }
    return QColor(Qt::black);
}

// parse `900 100.00px "Noto Sans JP","Hiragino Sans",sans-serif`
bool parseFontSpec(const QString &spec, int *weight, qreal *px,
                   QStringList *families) {
    static const QRegularExpression re(
        QStringLiteral("^(?:\\s*(normal|bold|italic|oblique|[1-9]00)\\s+)?"
                       "\\s*([0-9.]+)\\s*(px|pt|em)\\s+(.*)$"));
    const auto m = re.match(spec.trimmed());
    if (!m.hasMatch()) { return false; }
    QString w = m.captured(1);
    *weight = w == QStringLiteral("bold") ? 700
            : w == QStringLiteral("normal") || w.isEmpty() ? 400
            : w.toInt();
    *px = m.captured(2).toDouble();
    if (m.captured(3) == QStringLiteral("pt")) { *px *= 96.0 / 72.0; }
    else if (m.captured(3) == QStringLiteral("em")) { *px *= 16.0; }
    families->clear();
    for (QString fam : m.captured(4).split(QLatin1Char(','))) {
        fam = fam.trimmed();
        fam.remove(QLatin1Char('"')).remove(QLatin1Char('\''));
        if (!fam.isEmpty()) { families->append(fam); }
    }
    return !families->isEmpty();
}

// resolve a JIZURA font stack against locally installed families
QFont resolveFont(const QString &spec, const int fallbackWeight = 700) {
    int weight = fallbackWeight;
    qreal px = 64;
    QStringList fams;
    if (!parseFontSpec(spec, &weight, &px, &fams)) {
        QFont f(QStringLiteral("Noto Sans CJK JP"));
        f.setPixelSize(qMax(1, qRound(px)));
        f.setWeight(static_cast<QFont::Weight>(fallbackWeight));
        return f;
    }
    static QStringList installed;
    if (installed.isEmpty()) { installed = QFontDatabase::families(); }
    QString family;
    for (const QString &fam : std::as_const(fams)) {
        if (installed.contains(fam)) { family = fam; break; }
        // generic JP web-font stacks → this machine's CJK equivalents
        if ((fam.contains(QLatin1String("Sans JP"))
             || fam.contains(QLatin1String("Gothic")))
            && installed.contains(QStringLiteral("Noto Sans CJK JP"))) {
            family = QStringLiteral("Noto Sans CJK JP");
            break;
        }
        if ((fam.contains(QLatin1String("Serif JP"))
             || fam.contains(QLatin1String("Mincho")))
            && installed.contains(QStringLiteral("Noto Serif CJK JP"))) {
            family = QStringLiteral("Noto Serif CJK JP");
            break;
        }
    }
    if (family.isEmpty()) { family = QStringLiteral("Noto Sans CJK JP"); }
    QFont f(family);
    f.setPixelSize(qMax(1, qRound(px)));
    f.setWeight(static_cast<QFont::Weight>(qBound(100, weight, 900)));
    return f;
}

Qt::PenCapStyle capStyle(const QString &s) {
    return s == QStringLiteral("round") ? Qt::RoundCap
         : s == QStringLiteral("square") ? Qt::SquareCap : Qt::FlatCap;
}

Qt::PenJoinStyle joinStyle(const QString &s) {
    return s == QStringLiteral("round") ? Qt::RoundJoin
         : s == QStringLiteral("bevel") ? Qt::BevelJoin : Qt::MiterJoin;
}

// normalize a canvas arc (radians, y-down, cw-positive) into a Qt
// arcTo(rect, startDeg, sweepDeg) call; Qt's positive sweep is
// counter-clockwise on screen
void canvasArcToQt(const qreal start, const qreal end, const bool ccw,
                   qreal *startDeg, qreal *sweepDeg) {
    constexpr qreal kRad = 180.0 / M_PI;
    qreal d = end - start;
    if (!ccw) {
        while (d <= 0) { d += 2 * M_PI; }
        *startDeg = -start * kRad;
        *sweepDeg = -d * kRad;
    } else {
        while (d >= 0) { d -= 2 * M_PI; }
        *startDeg = -start * kRad;
        *sweepDeg = -d * kRad;
    }
}

} // namespace

// ------------------------------------------------------------------

void LyricCanvasFactory::ownForSession(QObject * const o) {
    QQmlEngine::setObjectOwnership(o, QQmlEngine::CppOwnership);
    mOwned.append(o);
}

LyricCanvasFactory::~LyricCanvasFactory() {
    for (const auto &o : mOwned) { delete o.data(); }
}

LyricCanvasFactory::LyricCanvasFactory(QObject * const parent) :
    QObject(parent) {}

void LyricCanvasFactory::install(QJSEngine * const engine) {
    mEngine = engine;
    // CppOwnership + parented: JS may cache canvases for long periods
    // (glyph sprite maps); JS-owned wrappers would be GC candidates
    engine->globalObject().setProperty(QStringLiteral("document"),
                                       engine->newQObject(this));
    // ImageData helpers: ImageData objects are plain JS records whose
    // .data is a typed array; C++ delivers bytes via QByteArray
    // (readable as an ArrayBuffer view) and receives JS-written bytes
    // as Latin-1 strings (no direct typed-array access from QJSValue)
    mMakeImageData = engine->evaluate(QStringLiteral(
        "(function(w, h, bytes) {"
        "  return { width: w, height: h, data: new Uint8ClampedArray(bytes) };"
        "})"));
    mBytesOfImageData = engine->evaluate(QStringLiteral(
        "(function(id) {"
        "  var u8 = id.data, s = '', CH = 8192;"
        "  for (var i = 0; i < u8.length; i += CH) {"
        "    s += String.fromCharCode.apply(null,"
        "          u8.subarray(i, Math.min(i + CH, u8.length)));"
        "  }"
        "  return s;"
        "})"));
}

QJSValue LyricCanvasFactory::makeImageData(const int width, const int height,
                                           const QByteArray &rgba) const {
    return mMakeImageData.call(QJSValueList{}
            << width << height << mEngine->toScriptValue(rgba));
}

QByteArray LyricCanvasFactory::bytesOfImageData(
        const QJSValue &imageData) const {
    const QString latin1 = mBytesOfImageData.call(
                QJSValueList{imageData}).toString();
    QByteArray bytes(latin1.size(), Qt::Uninitialized);
    for (int i = 0; i < latin1.size(); i++) {
        bytes[i] = char(latin1.at(i).unicode() & 0xFF);
    }
    return bytes;
}

QJSValue LyricCanvasFactory::debugObj() const {
    return mEngine ? mEngine->evaluate(QStringLiteral("({a: 1, b: 'x'})"))
                   : QJSValue();
}

QObject *LyricCanvasFactory::createElement(const QString &tag) {
    if (mEngine && tag.compare(QStringLiteral("canvas"),
                               Qt::CaseInsensitive) == 0) {
        const auto canvas = new JsCanvas2D(this);
        // the JIZURA renderer caches offscreen canvases (font decompose
        // tiles) inside JS globals; with the default JavaScriptOwnership
        // the GC may collect them mid-plan and every later drawImage of
        // the cache is a use-after-free — keep them alive for the
        // session and drop them when the factory dies with the engine
        ownForSession(canvas);
        return canvas;
    }
    return nullptr;
}

QObject *LyricCanvasFactory::createElementNS(const QString &ns,
                                             const QString &tag) {
    Q_UNUSED(ns)
    return createElement(tag);
}

// ------------------------------------------------------------------

JsCanvas2D::JsCanvas2D(LyricCanvasFactory *factory) :
    QObject(factory), mFactory(factory) {}

JsCanvas2D::~JsCanvas2D()
{
    // members (mImage) destruct BEFORE ~QObject deletes the children,
    // so a still-active painter inside the context would touch a dead
    // QPaintDevice ("Cannot destroy paint device that is being
    // painted" + heap corruption later) — end it while the image is
    // still alive
    if (mContext) {
        mContext->endPainting();
        delete mContext.data();
    }
}

void JsCanvas2D::setWidth(const int w) {
    if (mContext) { mContext->endPainting(); }
    mImage = QImage(qMax(1, w), qMax(1, mImage.height()),
                    QImage::Format_ARGB32_Premultiplied);
    mImage.fill(Qt::transparent);
}

void JsCanvas2D::setHeight(const int h) {
    if (mContext) { mContext->endPainting(); }
    mImage = QImage(qMax(1, mImage.width()), qMax(1, h),
                    QImage::Format_ARGB32_Premultiplied);
    mImage.fill(Qt::transparent);
}

QObject *JsCanvas2D::getContext(const QString &type) {
    if (!type.startsWith(QStringLiteral("2d"))) { return nullptr; }
    if (!mContext) { mContext = new JsContext2D(this); }
    return mContext.data();
}

QVariant JsCanvas2D::getBoundingClientRect() const {
    return QVariantMap{{QStringLiteral("left"), 0},
                       {QStringLiteral("top"), 0},
                       {QStringLiteral("width"), mImage.width()},
                       {QStringLiteral("height"), mImage.height()}};
}

// ------------------------------------------------------------------

JsGradient::JsGradient(const Kind kind, const QPointF &p0, const qreal r0,
                       const QPointF &p1, const qreal r1, QObject *parent) :
    QObject(parent), mKind(kind), mP0(p0), mP1(p1), mR0(r0), mR1(r1) {}

void JsGradient::addColorStop(const qreal offset, const QString &color) {
    mStops.append({qBound(0.0, offset, 1.0), parseColor(color)});
}

QGradient JsGradient::gradient() const {
    QGradient g;
    switch (mKind) {
        case Kind::Linear: {
            QLinearGradient lg(mP0, mP1);
            g = lg;
        } break;
        case Kind::Radial: {
            QRadialGradient rg(mP1, mR1, mP0); // focal p0(r0), center p1(r1)
            g = rg;
        } break;
        case Kind::Conic: {
            QConicalGradient cg(mP1, qRadiansToDegrees(mR0) - 90);
            g = cg;
        } break;
    }
    for (const auto &stop : mStops) {
        g.setColorAt(stop.first, stop.second);
    }
    return g;
}

JsPattern::JsPattern(const QImage &image, const QString &repetition,
                     QObject *parent) :
    QObject(parent), mImage(image) {
    Q_UNUSED(repetition)
}

JsImageData::JsImageData(const int width, const int height, QObject *parent) :
    QObject(parent), mImage(qMax(1, width), qMax(1, height),
                            QImage::Format_ARGB32) {
    mImage.fill(0);
}

QByteArray JsImageData::bytes() const {
    // canvas ImageData is straight RGBA byte order; QImage ARGB32 on
    // little-endian is BGRA in memory → reorder
    const int n = mImage.width() * mImage.height();
    QByteArray out;
    out.resize(n * 4);
    auto *dst = reinterpret_cast<uchar *>(out.data());
    for (int y = 0; y < mImage.height(); y++) {
        const auto *line = reinterpret_cast<const QRgb *>(mImage.constScanLine(y));
        for (int x = 0; x < mImage.width(); x++) {
            const QRgb px = line[x];
            *dst++ = uchar(qRed(px));
            *dst++ = uchar(qGreen(px));
            *dst++ = uchar(qBlue(px));
            *dst++ = uchar(qAlpha(px));
        }
    }
    return out;
}

// ------------------------------------------------------------------

JsContext2D::JsContext2D(JsCanvas2D *canvas) : mCanvas(canvas) {}

void JsContext2D::ensurePainter() {
    if (!mPainting || !mPainter.isActive()) {
        mPainter.begin(&mCanvas->mImage);
        mPainter.setRenderHint(QPainter::Antialiasing);
        mPainter.setRenderHint(QPainter::TextAntialiasing);
        mPainting = true;
    }
}

void JsContext2D::endPainting() {
    if (mPainting || mPainter.isActive()) {
        while (mSaveDepth > 0) {
            mPainter.restore();
            mSaveDepth--;
        }
        mPainter.end();
    }
    mPainting = false;
}

void JsContext2D::setGlobalAlpha(const qreal a) {
    mAlpha = qBound(0.0, a, 1.0);
    ensurePainter();
    mPainter.setOpacity(mAlpha);
}

void JsContext2D::setComposite(const QString &m) {
    mComposite = m.toLower();
    ensurePainter();
    mIgnoreDraw = compositeIgnored(mComposite);
    mPainter.setCompositionMode(compositeMode(mComposite));
}

qreal JsContext2D::lineWidth() const {
    return const_cast<JsContext2D *>(this)->mPainter.pen().widthF();
}

void JsContext2D::setLineWidth(const qreal w) {
    ensurePainter();
    QPen pen = mPainter.pen();
    pen.setWidthF(qMax(0.0, w));
    pen.setStyle(w > 0 ? Qt::SolidLine : Qt::NoPen);
    mPainter.setPen(pen);
}

void JsContext2D::setLineCap(const QString &c) {
    mExtra.lineCap = c;
    ensurePainter();
    QPen pen = mPainter.pen();
    pen.setCapStyle(capStyle(c));
    mPainter.setPen(pen);
}

void JsContext2D::setLineJoin(const QString &j) {
    mExtra.lineJoin = j;
    ensurePainter();
    QPen pen = mPainter.pen();
    pen.setJoinStyle(joinStyle(j));
    mPainter.setPen(pen);
}

void JsContext2D::setMiterLimit(const qreal m) {
    ensurePainter();
    QPen pen = mPainter.pen();
    pen.setMiterLimit(m);
    mPainter.setPen(pen);
}

void JsContext2D::setLineDashOffset(const qreal o) {
    mLineDashOffset = o;
    ensurePainter();
    QPen pen = mPainter.pen();
    pen.setDashOffset(o);
    mPainter.setPen(pen);
}

void JsContext2D::setFont(const QString &spec) {
    mExtra.fontSpec = spec;
    ensurePainter();
    mPainter.setFont(resolveFont(spec));
}

QVariant JsContext2D::canvasProp() const {
    return QVariant::fromValue(static_cast<QObject *>(mCanvas));
}

QVariant JsContext2D::fillStyle() const { return mFillStyle; }

QVariant JsContext2D::strokeStyle() const { return mStrokeStyle; }

qreal JsContext2D::miterLimit() const { return mMiterLimit; }

void JsContext2D::setFillStyle(const QVariant &v) {
    mFillStyle = v;
    ensurePainter();
    mPainter.setBrush(styleToBrush(v));
}

void JsContext2D::setStrokeStyle(const QVariant &v) {
    mStrokeStyle = v;
    ensurePainter();
    QPen pen = mPainter.pen();
    const QBrush brush = styleToBrush(v);
    pen.setBrush(brush);
    pen.setStyle(brush.style() == Qt::NoBrush ? Qt::NoPen : Qt::SolidLine);
    mPainter.setPen(pen);
}

QBrush JsContext2D::styleToBrush(const QVariant &v) const {
    if (v.canConvert<QString>()) {
        const QColor c = parseColor(v);
        return c.alpha() == 0 ? QBrush(Qt::NoBrush) : QBrush(c);
    }
    const auto obj = v.value<QObject *>();
    if (obj) {
        if (const auto g = qobject_cast<JsGradient *>(obj)) {
            return QBrush(g->gradient());
        }
        if (const auto p = qobject_cast<JsPattern *>(obj)) {
            return p->brush();
        }
    }
    return QBrush(Qt::black);
}

void JsContext2D::save() {
    ensurePainter();
    mPainter.save();
    mSaveDepth++;
    mExtraStack.push(mExtra);
}

void JsContext2D::restore() {
    if (!mPainting || mSaveDepth <= 0) { return; }
    mPainter.restore();
    mSaveDepth--;
    if (!mExtraStack.isEmpty()) { mExtra = mExtraStack.pop(); }
}

void JsContext2D::scale(const qreal x, const qreal y) {
    ensurePainter();
    mPainter.scale(x, y);
}

void JsContext2D::rotate(const qreal radians) {
    ensurePainter();
    mPainter.rotate(qRadiansToDegrees(radians));
}

void JsContext2D::translate(const qreal x, const qreal y) {
    ensurePainter();
    mPainter.translate(x, y);
}

void JsContext2D::transform(const qreal a, const qreal b, const qreal c,
                            const qreal d, const qreal e, const qreal f) {
    ensurePainter();
    mPainter.setTransform(mPainter.transform()
                          * QTransform(a, b, c, d, e, f));
}

void JsContext2D::setTransform(const qreal a, const qreal b, const qreal c,
                               const qreal d, const qreal e, const qreal f) {
    ensurePainter();
    mPainter.setTransform(QTransform(a, b, c, d, e, f));
}

void JsContext2D::resetTransform() {
    ensurePainter();
    mPainter.resetTransform();
}

QVariant JsContext2D::getTransform() {
    ensurePainter();
    const QTransform t = mPainter.transform();
    return QVariantMap{{QStringLiteral("a"), t.m11()},
                       {QStringLiteral("b"), t.m12()},
                       {QStringLiteral("c"), t.m21()},
                       {QStringLiteral("d"), t.m22()},
                       {QStringLiteral("e"), t.dx()},
                       {QStringLiteral("f"), t.dy()}};
}

void JsContext2D::beginPath() { mPath = QPainterPath(); }

void JsContext2D::closePath() { mPath.closeSubpath(); }

void JsContext2D::moveTo(const qreal x, const qreal y) {
    mPath.moveTo(x, y);
}

void JsContext2D::lineTo(const qreal x, const qreal y) {
    if (mPath.isEmpty()) { mPath.moveTo(x, y); }
    else { mPath.lineTo(x, y); }
}

void JsContext2D::rect(const qreal x, const qreal y, const qreal w,
                       const qreal h) {
    mPath.addRect(x, y, w, h);
}

void JsContext2D::arc(const qreal x, const qreal y, const qreal r,
                      const qreal start, const qreal end, const bool ccw) {
    qreal startDeg = 0, sweepDeg = 0;
    canvasArcToQt(start, end, ccw, &startDeg, &sweepDeg);
    if (mPath.isEmpty()) {
        const QPointF p0(x + r * std::cos(start), y + r * std::sin(start));
        mPath.moveTo(p0);
    }
    mPath.arcTo(QRectF(x - r, y - r, 2 * r, 2 * r), startDeg, sweepDeg);
}

void JsContext2D::arcTo(const qreal x1, const qreal y1, const qreal x2,
                        const qreal y2, const qreal r) {
    // canvas tangent arc: line to the tangent point then arc around the
    // corner; QPainterPath lacks the (point,point,r) overload here, so
    // build it from the tangent geometry directly
    if (mPath.isEmpty()) { mPath.moveTo(x1, y1); }
    const QPointF p0 = mPath.currentPosition();
    const QPointF p1(x1, y1), p2(x2, y2);
    const QPointF v1 = p0 - p1, v2 = p2 - p1;
    const double l1 = std::hypot(v1.x(), v1.y());
    const double l2 = std::hypot(v2.x(), v2.y());
    if (l1 < 1e-9 || l2 < 1e-9 || r <= 0) {
        mPath.lineTo(p1);
        return;
    }
    const double angle = std::acos(qBound(-1.0,
            QPointF::dotProduct(v1, v2) / (l1 * l2), 1.0));
    const double tanLen = r / std::tan(angle / 2);
    const QPointF t1 = p1 + v1 * (tanLen / l1);
    const QPointF t2 = p1 + v2 * (tanLen / l2);
    mPath.lineTo(t1);
    // tangent-circle center: offset from p1 along the angle bisector
    const QPointF bis = (v1 / l1 + v2 / l2);
    const double bl = std::hypot(bis.x(), bis.y());
    if (bl < 1e-9) { mPath.lineTo(t2); return; }
    const double cdist = r / std::sin(angle / 2);
    const QPointF center = p1 + bis * (cdist / bl);
    // draw the arc from t1 to t2 around center
    const double a1 = std::atan2(t1.y() - center.y(), t1.x() - center.x());
    const double a2 = std::atan2(t2.y() - center.y(), t2.x() - center.x());
    // pick the short sweep through the corner side
    double d = a2 - a1;
    while (d > M_PI) { d -= 2 * M_PI; }
    while (d < -M_PI) { d += 2 * M_PI; }
    qreal startDeg = 0, sweepDeg = 0;
    canvasArcToQt(a1, a1 + d, d < 0, &startDeg, &sweepDeg);
    mPath.arcTo(QRectF(center.x() - r, center.y() - r, 2 * r, 2 * r),
                startDeg, sweepDeg);
}

void JsContext2D::ellipse(const qreal x, const qreal y, const qreal rx,
                          const qreal ry, const qreal rot,
                          const qreal start, const qreal end,
                          const bool ccw) {
    Q_UNUSED(start) Q_UNUSED(end) Q_UNUSED(ccw)
    if (mPath.isEmpty()) { mPath.moveTo(x + rx, y); }
    mPath.addEllipse(QPointF(x, y), rx, ry);
}

void JsContext2D::bezierCurveTo(const qreal c1x, const qreal c1y,
                                const qreal c2x, const qreal c2y,
                                const qreal x, const qreal y) {
    if (mPath.isEmpty()) { mPath.moveTo(c1x, c1y); }
    mPath.cubicTo(c1x, c1y, c2x, c2y, x, y);
}

void JsContext2D::quadraticCurveTo(const qreal cx, const qreal cy,
                                   const qreal x, const qreal y) {
    if (mPath.isEmpty()) { mPath.moveTo(cx, cy); }
    mPath.quadTo(cx, cy, x, y);
}

void JsContext2D::roundRect(const qreal x, const qreal y, const qreal w,
                            const qreal h, const qreal r) {
    mPath.addRoundedRect(QRectF(x, y, w, h), r, r);
}

void JsContext2D::fill(const QString &rule) {
    if (mIgnoreDraw) { return; }
    ensurePainter();
    if (rule == QStringLiteral("evenodd")) { mPath.setFillRule(Qt::OddEvenFill); }
    else { mPath.setFillRule(Qt::WindingFill); }
    if (!mPath.isEmpty()) { mPainter.fillPath(mPath, mPainter.brush()); }
    recPath(false);
}

void JsContext2D::stroke() {
    if (mIgnoreDraw) { return; }
    ensurePainter();
    if (!mPath.isEmpty()) { mPainter.strokePath(mPath, mPainter.pen()); }
    recPath(true);
}

void JsContext2D::clip(const QString &rule) {
    ensurePainter();
    if (rule == QStringLiteral("evenodd")) { mPath.setFillRule(Qt::OddEvenFill); }
    else { mPath.setFillRule(Qt::WindingFill); }
    if (!mPath.isEmpty()) {
        mPainter.setClipPath(mPath, Qt::IntersectClip);
    }
}

void JsContext2D::fillRect(const qreal x, const qreal y, const qreal w,
                           const qreal h) {
    if (mIgnoreDraw) { return; }
    ensurePainter();
    mPainter.fillRect(QRectF(x, y, w, h), mPainter.brush());
    recRect(false, QRectF(x, y, w, h));
}

void JsContext2D::strokeRect(const qreal x, const qreal y, const qreal w,
                             const qreal h) {
    if (mIgnoreDraw) { return; }
    ensurePainter();
    QPainterPath path;
    path.addRect(x, y, w, h);
    mPainter.strokePath(path, mPainter.pen());
    recRect(true, QRectF(x, y, w, h));
}

void JsContext2D::clearRect(const qreal x, const qreal y, const qreal w,
                            const qreal h) {
    ensurePainter();
    mPainter.save();
    mPainter.setCompositionMode(QPainter::CompositionMode_Clear);
    mPainter.fillRect(QRectF(x, y, w, h), Qt::black);
    mPainter.restore();
}

QPointF JsContext2D::alignedTextPos(const QString &text, const qreal x,
                                    const qreal y) const {
    const QFontMetricsF fm(mPainter.font());
    const qreal w = fm.horizontalAdvance(text);
    qreal px = x;
    const QString &a = mExtra.textAlign;
    if (a == QStringLiteral("center")) { px = x - w * 0.5; }
    else if (a == QStringLiteral("right") || a == QStringLiteral("end")) {
        px = x - w;
    }
    qreal py = y;
    const QString &b = mExtra.textBaseline;
    if (b == QStringLiteral("middle")) { py = y + (fm.ascent() - fm.descent()) * 0.5; }
    else if (b == QStringLiteral("top") || b == QStringLiteral("hanging")) {
        py = y + fm.ascent();
    } else if (b == QStringLiteral("bottom")) {
        py = y - fm.descent();
    }
    return {px, py};
}

// ---------------------------------------------------------------------------
// recording: world-space primitive capture for the native builder

namespace {
void recPaintFrom(const QBrush &brush, const QPen &pen,
                  LyricDrawRec &r, const bool stroke) {
    const QGradient *g = brush.gradient();
    if (g && g->type() != QGradient::NoGradient) {
        r.gradient = true;
        r.gradType = int(g->type()) - 1; // LinearGradient=1→0, Radial=2→1, Conic=3→2
        r.gradStops.clear();
        const auto stops = g->stops();
        for (const QGradientStop &s : stops) {
            r.gradStops.append({s.first, s.second});
        }
        if (g->type() == QGradient::LinearGradient) {
            const auto *lg = static_cast<const QLinearGradient*>(g);
            r.gradP0 = lg->start();
            r.gradP1 = lg->finalStop();
        }
        // average color for the flattened fallback: weight each
        // stop's RGB by its alpha and carry the mean alpha. The old
        // straight RGB average forced alpha=1, turning the web's
        // near-invisible vignette/center-lift (alpha ≤ 0.045) into an
        // opaque #808080 fullscreen blanket that washed the whole
        // background gray
        if (!stops.isEmpty()) {
            qreal rr = 0, gg = 0, bb = 0, aa = 0;
            for (const QGradientStop &s : stops) {
                const qreal a = s.second.alphaF();
                rr += s.second.redF() * a;
                gg += s.second.greenF() * a;
                bb += s.second.blueF() * a;
                aa += a;
            }
            const qreal meanA = aa / stops.size();
            QColor flat;
            if (meanA > 0.003) {
                flat = QColor::fromRgbF(qBound(0.0, rr / aa, 1.0),
                                        qBound(0.0, gg / aa, 1.0),
                                        qBound(0.0, bb / aa, 1.0));
            } else {
                flat = QColor(0, 0, 0);
            }
            flat.setAlphaF(meanA);
            r.fillColor = flat;
            r.hasFill = true;
        }
    } else {
        r.fillColor = brush.color();
        r.hasFill = brush.style() != Qt::NoBrush;
    }
    if (stroke) {
        r.strokeColor = pen.color();
        r.strokeWidth = pen.widthF();
        r.hasStroke = pen.style() != Qt::NoPen && pen.widthF() > 0.05;
    }
}
}

void JsContext2D::recText(const bool stroke, const QString &text,
                          const QPointF &baselinePos) {
    if (!mCanvas->mRecording || text.trimmed().isEmpty()) { return; }
    const QTransform t = mPainter.transform();
    LyricDrawRec r;
    r.kind = LyricDrawRec::Kind::Text;
    r.text = text;
    r.pos = t.map(baselinePos);
    const QFont f = mPainter.font();
    r.family = f.family();
    const qreal sy = qSqrt(t.m22() * t.m22() + t.m21() * t.m21());
    const qreal sx = qSqrt(t.m11() * t.m11() + t.m12() * t.m12());
    // QFont keeps pixelSize and pointSize mutually exclusive: the
    // canvas-2D specs this shim resolves are px-based, so pointSizeF()
    // returns -1 for them. Reading it anyway collapsed every recorded
    // text to the qMax floor (4-6 px) and the materialized layers
    // came out invisible specks. Derive the size in px instead (the
    // materializer's setFontSize is px-based, like SkFont).
    const qreal basePx = f.pointSizeF() > 0
            ? f.pointSizeF() * 96.0 / 72.0 : qreal(f.pixelSize());
    r.pointSize = qMax(4.0, basePx * qMax(0.01, sy));
    r.weight = f.weight();
    r.stretchX = sy > 0.01 ? sx / sy : 1.0;
    r.rotation = qRadiansToDegrees(qAtan2(t.m21(), t.m11()));
    r.alpha = mAlpha;
    const QFontMetricsF fm(f);
    r.rect = QRectF(r.pos.x(), r.pos.y() - r.pointSize * 0.8,
                    fm.horizontalAdvance(text) * qMax(0.01, sx),
                    r.pointSize * 1.2);
    recPaintFrom(mPainter.brush(), mPainter.pen(), r, stroke);
    mCanvas->mRecs.append(r);
}

void JsContext2D::recRect(const bool stroke, const QRectF &localRect) {
    if (!mCanvas->mRecording) { return; }
    const QTransform t = mPainter.transform();
    LyricDrawRec r;
    r.kind = LyricDrawRec::Kind::Rect;
    r.rect = t.mapRect(localRect);
    r.alpha = mAlpha;
    const qreal rot = qRadiansToDegrees(qAtan2(t.m21(), t.m11()));
    if (qAbs(rot) > 0.1 || qAbs(t.m12()) > 0.001) {
        // sheared/rotated rect → path with the four mapped corners
        r.kind = LyricDrawRec::Kind::Path;
        QPainterPath pp;
        pp.addPolygon(t.map(QPolygonF(localRect)));
        pp.closeSubpath();
        r.path = pp;
    }
    recPaintFrom(mPainter.brush(), mPainter.pen(), r, stroke);
    mCanvas->mRecs.append(r);
}

void JsContext2D::recPath(const bool stroke) {
    if (!mCanvas->mRecording || mPath.isEmpty()) { return; }
    const QTransform t = mPainter.transform();
    LyricDrawRec r;
    r.kind = LyricDrawRec::Kind::Path;
    r.path = t.map(mPath);
    r.alpha = mAlpha;
    recPaintFrom(mPainter.brush(), mPainter.pen(), r, stroke);
    mCanvas->mRecs.append(r);
}

void JsContext2D::fillText(const QString &text, const qreal x,
                           const qreal y) {
    if (mIgnoreDraw) { return; }
    ensurePainter();
    const QPointF pos = alignedTextPos(text, x, y);
    QPen pen(mPainter.brush(), 0);
    pen.setStyle(mPainter.brush().style() == Qt::NoBrush
                     ? Qt::NoPen : Qt::SolidLine);
    const QPen old = mPainter.pen();
    mPainter.setPen(pen);
    mPainter.drawText(pos, text);
    mPainter.setPen(old);
    recText(false, text, pos);
}

void JsContext2D::strokeText(const QString &text, const qreal x,
                             const qreal y) {
    if (mIgnoreDraw) { return; }
    ensurePainter();
    const QPointF pos = alignedTextPos(text, x, y);
    // Qt lacks strokeText; approximate via a text-shaped path
    QPainterPath tp;
    tp.addText(pos, mPainter.font(), text);
    mPainter.strokePath(tp, mPainter.pen());
    recText(true, text, pos);
}

QVariant JsContext2D::measureText(const QString &text) {
    ensurePainter();
    return QVariantMap{{QStringLiteral("width"),
                        QFontMetricsF(mPainter.font())
                            .horizontalAdvance(text)}};
}

void JsContext2D::setLineDash(const QJSValue &segments) {
    ensurePainter();
    QVector<qreal> pattern;
    if (segments.isArray()) {
        const int n = segments.property(QStringLiteral("length")).toInt();
        for (int i = 0; i < n; i++) {
            pattern << segments.property(i).toNumber();
        }
    }
    QPen pen = mPainter.pen();
    if (pattern.isEmpty()) { pen.setStyle(Qt::SolidLine); }
    else { pen.setDashPattern(pattern); }
    mPainter.setPen(pen);
}

QVariant JsContext2D::lineDash() const {
    return QVariantList{};
}

QObject *JsContext2D::createLinearGradient(const qreal x0, const qreal y0,
                                           const qreal x1, const qreal y1) {
    const auto g = new JsGradient(JsGradient::Kind::Linear,
                                  {x0, y0}, 0, {x1, y1}, 0, mCanvas);
    if (mCanvas && mCanvas->mFactory) {
        mCanvas->mFactory->ownForSession(g);
    } else {
        QQmlEngine::setObjectOwnership(g, QQmlEngine::CppOwnership);
    }
    return g;
}

QObject *JsContext2D::createRadialGradient(const qreal x0, const qreal y0,
                                           const qreal r0, const qreal x1,
                                           const qreal y1, const qreal r1) {
    const auto g = new JsGradient(JsGradient::Kind::Radial,
                                  {x0, y0}, r0, {x1, y1}, r1, mCanvas);
    if (mCanvas && mCanvas->mFactory) {
        mCanvas->mFactory->ownForSession(g);
    } else {
        QQmlEngine::setObjectOwnership(g, QQmlEngine::CppOwnership);
    }
    return g;
}

QObject *JsContext2D::createConicGradient(const qreal startAngle,
                                          const qreal x, const qreal y) {
    const auto g = new JsGradient(JsGradient::Kind::Conic,
                                  {x, y}, startAngle, {x, y}, 0, mCanvas);
    if (mCanvas && mCanvas->mFactory) {
        mCanvas->mFactory->ownForSession(g);
    } else {
        QQmlEngine::setObjectOwnership(g, QQmlEngine::CppOwnership);
    }
    return g;
}

QObject *JsContext2D::createPattern(const QJSValue &source,
                                    const QString &repetition) {
    const auto canvas = qobject_cast<JsCanvas2D *>(source.toQObject());
    if (!canvas) { return nullptr; }
    const auto g = new JsPattern(canvas->image(), repetition, mCanvas);
    if (mCanvas && mCanvas->mFactory) {
        mCanvas->mFactory->ownForSession(g);
    } else {
        QQmlEngine::setObjectOwnership(g, QQmlEngine::CppOwnership);
    }
    return g;
}

void JsContext2D::drawImage(const QJSValue &source, const qreal a,
                            const qreal b, const qreal c, const qreal d,
                            const qreal e, const qreal f, const qreal g,
                            const qreal h) {
    const auto canvas = qobject_cast<JsCanvas2D *>(source.toQObject());
    if (!canvas || canvas == mCanvas) { return; }
    // DEEP copy: the renderer routinely re-bases cached source canvases
    // (font decompose tiles) right after drawing them; a shallow
    // QImage shares those bits and QPainter then reads freed memory
    const QImage src = canvas->image().copy();
    if (src.isNull()) { return; }
    if (mIgnoreDraw) { return; }
    ensurePainter();
    mPainter.setRenderHint(QPainter::SmoothPixmapTransform, mSmoothing);
    const bool hasD = !qIsNaN(d);
    if (qIsNaN(c)) {
        // (img, dx, dy)
        mPainter.drawImage(QPointF(a, b), src);
    } else if (!hasD) {
        // (img, dx, dy, dw, dh)
        mPainter.drawImage(QRectF(a, b, c, d), src);
    } else {
        // (img, sx, sy, sw, sh, dx, dy, dw, dh)
        QRectF srcR(a, b, c, d);
        srcR = srcR.intersected(QRectF(0, 0, src.width(), src.height()));
        if (srcR.isEmpty()) { return; }
        mPainter.drawImage(QRectF(e, f, g, h), src, srcR);
    }
}

QJSValue JsContext2D::getImageData(const qreal sx, const qreal sy,
                                   const qreal sw, const qreal sh) {
    ensurePainter();
    const int x = qBound(0, qFloor(sx), mCanvas->mImage.width() - 1);
    const int y = qBound(0, qFloor(sy), mCanvas->mImage.height() - 1);
    const int w = qBound(1, qCeil(sw), mCanvas->mImage.width() - x);
    const int h = qBound(1, qCeil(sh), mCanvas->mImage.height() - y);
    const QImage region = mCanvas->mImage.copy(x, y, w, h)
            .convertToFormat(QImage::Format_ARGB32);
    const JsImageData img(region.width(), region.height(), mCanvas);
    const auto *factory = mCanvas->mFactory;
    if (factory && factory->hasHelpers()) {
        return factory->makeImageData(region.width(), region.height(),
                                      img.bytes());
    }
    return QJSValue();
}

QJSValue JsContext2D::createImageData(const qreal sw, const qreal sh) {
    const auto *factory = mCanvas->mFactory;
    if (factory && factory->hasHelpers()) {
        // a zero-filled buffer: an EMPTY QByteArray would convert to an
        // invalid ArrayBuffer and throw inside the JS helper
        return factory->makeImageData(qCeil(sw), qCeil(sh),
                                      QByteArray(int(sw) * int(sh) * 4, 0));
    }
    return QJSValue();
}

void JsContext2D::putImageData(const QJSValue &imageData, const qreal dx,
                               const qreal dy, const QString &rawBytes) {
    QImage img;
    if (const auto imgData = qobject_cast<JsImageData *>(
                imageData.toQObject())) {
        img = imgData->image();
    } else {
        QByteArray bytes;
        if (!rawBytes.isEmpty()) {
            bytes.resize(rawBytes.size());
            for (int ci = 0; ci < rawBytes.size(); ci++) {
                bytes[ci] = char(rawBytes.at(ci).unicode() & 0xFF);
            }
        } else if (const auto *factory = mCanvas->mFactory;
                   factory && factory->hasHelpers()) {
            bytes = factory->bytesOfImageData(imageData);
        }
        if (bytes.isEmpty()) { return; }
        const int w = imageData.property(QStringLiteral("width")).toInt();
        const int h = imageData.property(QStringLiteral("height")).toInt();
        if (w <= 0 || h <= 0) { return; }
        img = QImage(reinterpret_cast<const uchar *>(bytes.constData()),
                     w, h, w * 4, QImage::Format_ARGB32)
              .rgbSwapped(); // RGBA byte order → ARGB32
        img.detach();
    }
    if (img.isNull()) { return; }
    ensurePainter();
    mPainter.save();
    mPainter.setCompositionMode(QPainter::CompositionMode_Source);
    mPainter.resetTransform();
    mPainter.setOpacity(1);
    mPainter.drawImage(QPointF(dx, dy), img);
    mPainter.restore();
}

bool JsContext2D::isPointInPath(const qreal x, const qreal y) {
    return mPath.contains(QPointF(x, y));
}

QString LyricCanvasDriver::source() { return QStringLiteral(
    "(function(){"
    "  var st = { canvas: null, ctx: null, renderer: null, plan: null };"
    "  globalThis.__jz = st;"
    "  globalThis.__jzInit = function(w, h){"
    "    st.canvas = document.createElement('canvas');"
    "    st.canvas.width = w; st.canvas.height = h;"
    "    st.ctx = st.canvas.getContext('2d');"
    "  };"
    "  globalThis.__jzPlan = function(styleKey, seed, density, lyrics,"
    "                                  beatsJson, audioDuration, fps){"
    "    var pr = J.defaultProject();"
    "    pr.lyrics = (lyrics && lyrics.length) ? lyrics"
    "      : \"\\u591c\\u660e\\u3051\\u306e\\u8272\\n*\\u6587\\u5b57* Motion\\n\\u6b4c\\u8a5e\\u30a2\\u30cb\\u30e1\\u3067\\u3059\";"
    "    pr.style = styleKey; pr.seed = seed;"
    "    pr.fx.density = (density == null ? 0.55 : density);"
    "    pr.fps = (fps || 24);"
    "    pr.timing.bpm = 0;"
    "    var audio = null;"
    "    if (beatsJson && beatsJson.length) {"
    "      audio = { beats: beatsJson, duration: audioDuration || 600 };"
    "    }"
    "    st.plan = J.plan(pr, audio);"
    "    st.renderer = new J.Renderer();"
    "    return JSON.stringify({ duration: st.plan.duration, cuts: st.plan.cuts.length });"
    "  };"
    "  globalThis.__jzFrame = function(i, n){"
    "    var plan = st.plan;"
    "    var t = plan.duration * (i + 0.5) / n;"
    "    st.ctx.setTransform(1, 0, 0, 1, 0, 0);"
    "    st.ctx.clearRect(0, 0, st.canvas.width, st.canvas.height);"
    "    st.renderer.frame(st.ctx, plan, t, { scale: st.canvas.width / plan.W });"
    "    return JSON.stringify({ t: t });"
    "  };"
    "  globalThis.__jzFrameAt = function(t, w, h){"
    "    if (!st.canvas) __jzInit(w, h);"
    "    if (st.canvas.width !== w) st.canvas.width = w;"
    "    if (st.canvas.height !== h) st.canvas.height = h;"
    "    st.ctx.setTransform(1, 0, 0, 1, 0, 0);"
    "    st.ctx.clearRect(0, 0, w, h);"
    "    st.renderer.frame(st.ctx, st.plan, t, { scale: w / st.plan.W });"
    "    return 'ok';"
    "  };"
    "  globalThis.__jzRenderTime = function(t){"
    "    st.ctx.setTransform(1, 0, 0, 1, 0, 0);"
    "    st.ctx.clearRect(0, 0, st.canvas.width, st.canvas.height);"
    "    st.renderer.frame(st.ctx, st.plan, t, { scale: st.canvas.width / st.plan.W });"
    "    return 'ok';"
    "  };"
    "})();");
}

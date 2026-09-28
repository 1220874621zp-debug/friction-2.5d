#ifndef LYRICMOTIONCANVAS_H
#define LYRICMOTIONCANVAS_H

#include <QObject>
#include <QPainter>
#include <QPainterPath>
#include <QImage>
#include <QTransform>
#include <QStack>
#include <QPointer>
#include <QJSValue>
#include <QVariant>

class QJSEngine;
class JsContext2D;
class JsCanvas2D;

// Canvas2D-over-QPainter shim for the vendored JIZURA render path
// (09_render / 03_text / 02_fonts / packs). Covers the exact API
// surface those sources use (audited 2026-09-27): state + transforms,
// path building, fill/stroke/clip, gradients/patterns, text with
// align/baseline, drawImage of other shim canvases, and pixel access
// via ArrayBuffer-backed ImageData.
//
// Known degradations (accepted for preview tiles):
//  - ctx.filter (blur) and shadow* are parsed but not applied
//  - letterSpacing is ignored
//  - HSL blend modes (saturation/hue/color/luminosity) draw nothing
//    (QPainter has no HSL composition modes); keyBg previews skip them
//  - JS-mutated ImageData is not read back by putImageData (C++-made
//    ImageData round-trips fine); mosaic-style post FX lose that effect

class JsImageData;

// the JS driver glue the preview worker AND the native builder's
// materialization pass both evaluate after the planner sources
namespace LyricCanvasDriver { QString source(); }

// document.createElement('canvas') → JsCanvas2D; C++ document object
// installed as `document` BEFORE the JS stub evaluates (the stub only
// defines its fake document when none exists).
class LyricCanvasFactory : public QObject
{
    Q_OBJECT
public:
    explicit LyricCanvasFactory(QObject * const parent = nullptr);
    ~LyricCanvasFactory() override;
    // install as `document` on the engine's global object; also installs
    // the ImageData helper functions (there is no efficient C++<->JS
    // typed-array channel, so C++ round-trips pixel bytes through
    // engine-evaluated JS helpers)
    void install(QJSEngine * const engine);

    Q_INVOKABLE QJSValue debugPing() const { return mMakeImageData; }
    Q_INVOKABLE QJSValue debugObj() const;
    Q_INVOKABLE QObject *createElement(const QString &tag);
    Q_INVOKABLE QObject *createElementNS(const QString &ns,
                                         const QString &tag);

    QJSValue makeImageData(int width, int height,
                           const QByteArray &rgba) const;
    QByteArray bytesOfImageData(const QJSValue &imageData) const;
    bool hasHelpers() const { return mMakeImageData.isCallable(); }
    // register a CppOwnership session object for factory-lifetime
    void ownForSession(QObject * const o);

private:
    friend class LyricCanvasFactoryRegistrar;
    QJSEngine *mEngine = nullptr;
    QJSValue mMakeImageData;   // (w, h, byteArray) -> {width,height,data}
    QJSValue mBytesOfImageData; // ({width,height,data}) -> latin1 string
    // CppOwnership objects created for this session (canvases,
    // gradients, patterns) — deleted with the factory so JS GC never
    // frees them early AND C++ never leaks them
    QList<QPointer<QObject>> mOwned;
};

// ---------------------------------------------------------------------------
// draw-call recording: the native builder replays a JIZURA render pass
// and materializes every primitive as editable friction layers — this
// is how the panel reproduces the web-version compositions without
// hand-porting 860 part renderers
struct LyricDrawRec
{
    enum class Kind { Text, Rect, Path };
    Kind kind = Kind::Rect;
    // text
    QString text;
    QString family;
    qreal pointSize = 48;
    int weight = 400;
    // geometry in WORLD coordinates (ctx transform already applied)
    QPointF pos;
    QRectF rect;
    QPainterPath path;
    qreal rotation = 0; // degrees, from the ctx transform
    qreal stretchX = 1;
    // paint
    QColor fillColor;
    bool hasFill = false;
    QColor strokeColor;
    qreal strokeWidth = 0;
    bool hasStroke = false;
    qreal alpha = 1;
    // gradient fill (linear stops kept; radial/conic flattened to the
    // average color by the materializer for now)
    bool gradient = false;
    QVector<QPair<qreal, QColor>> gradStops;
    QPointF gradP0, gradP1;
    int gradType = 0; // 0 linear 1 radial 2 conic
};
// ---------------------------------------------------------------------------

class JsCanvas2D : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int width READ width WRITE setWidth)
    Q_PROPERTY(int height READ height WRITE setHeight)
public:
    explicit JsCanvas2D(LyricCanvasFactory *factory);
    ~JsCanvas2D() override;

    int width() const { return mImage.width(); }
    int height() const { return mImage.height(); }
    void setWidth(const int w);
    void setHeight(const int h);

    Q_INVOKABLE QObject *getContext(const QString &type);
    Q_INVOKABLE QVariant getBoundingClientRect() const;

    const QImage &image() const { return mImage; }
    QImage takeImage() { return mImage; } // shallow; callers must copy

    // recording: while on, every draw call appends a LyricDrawRec
    // (world-space) instead of only painting pixels
    void setRecording(const bool on) { mRecording = on; }
    bool recording() const { return mRecording; }
    const QVector<LyricDrawRec> &recordingItems() const { return mRecs; }
    void clearRecording() { mRecs.clear(); }

private:
    friend class JsContext2D;
    QImage mImage{1, 1, QImage::Format_ARGB32_Premultiplied};
    QPointer<JsContext2D> mContext;
    LyricCanvasFactory *mFactory = nullptr;
    bool mRecording = false;
    QVector<LyricDrawRec> mRecs;
};

class JsGradient : public QObject
{
    Q_OBJECT
public:
    enum class Kind { Linear, Radial, Conic };
    JsGradient(const Kind kind, const QPointF &p0, const qreal r0,
               const QPointF &p1, const qreal r1, QObject *parent);
    Q_INVOKABLE void addColorStop(const qreal offset, const QString &color);
    QGradient gradient() const;
private:
    Kind mKind;
    QPointF mP0, mP1;
    qreal mR0 = 0, mR1 = 1;
    QVector<QPair<qreal, QColor>> mStops;
};

class JsPattern : public QObject
{
    Q_OBJECT
public:
    JsPattern(const QImage &image, const QString &repetition,
              QObject *parent);
    QBrush brush() const { return QBrush(mImage); }
private:
    QImage mImage;
};

class JsImageData : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int width READ width CONSTANT)
    Q_PROPERTY(int height READ height CONSTANT)
    Q_PROPERTY(QByteArray bytes READ bytes CONSTANT)
public:
    JsImageData(const int width, const int height, QObject *parent);

    int width() const { return mImage.width(); }
    int height() const { return mImage.height(); }
    QByteArray bytes() const;

    const QImage &image() const { return mImage; }

private:
    QImage mImage; // Format_ARGB32 (straight alpha, matches ImageData)
};

class JsContext2D : public QObject
{
    Q_OBJECT
public:
    friend class JsCanvas2D;
    // canvas-level state shared with the owning JsCanvas2D
    explicit JsContext2D(JsCanvas2D *canvas);

    // ---- attributes ----
    Q_PROPERTY(QVariant canvas READ canvasProp CONSTANT)
    Q_PROPERTY(QVariant fillStyle READ fillStyle WRITE setFillStyle)
    Q_PROPERTY(QVariant strokeStyle READ strokeStyle WRITE setStrokeStyle)
    Q_PROPERTY(qreal globalAlpha READ globalAlpha WRITE setGlobalAlpha)
    Q_PROPERTY(QString globalCompositeOperation READ composite WRITE setComposite)
    Q_PROPERTY(qreal lineWidth READ lineWidth WRITE setLineWidth)
    Q_PROPERTY(QString lineCap READ lineCap WRITE setLineCap)
    Q_PROPERTY(QString lineJoin READ lineJoin WRITE setLineJoin)
    Q_PROPERTY(qreal miterLimit READ miterLimit WRITE setMiterLimit)
    Q_PROPERTY(QString font READ font WRITE setFont)
    Q_PROPERTY(QString textAlign READ textAlign WRITE setTextAlign)
    Q_PROPERTY(QString textBaseline READ textBaseline WRITE setTextBaseline)
    Q_PROPERTY(QVariant filter READ filter WRITE setFilter)
    Q_PROPERTY(QVariant shadowColor READ shadowColor WRITE setShadowColor)
    Q_PROPERTY(qreal shadowBlur READ shadowBlur WRITE setShadowBlur)
    Q_PROPERTY(qreal shadowOffsetX READ shadowOffsetX WRITE setShadowOffsetX)
    Q_PROPERTY(qreal shadowOffsetY READ shadowOffsetY WRITE setShadowOffsetY)
    Q_PROPERTY(bool imageSmoothingEnabled READ smoothing WRITE setSmoothing)
    Q_PROPERTY(QString imageSmoothingQuality READ smoothingQuality WRITE setSmoothingQuality)
    Q_PROPERTY(qreal lineDashOffset READ lineDashOffset WRITE setLineDashOffset)

    QVariant canvasProp() const;
    QVariant fillStyle() const;
    void setFillStyle(const QVariant &v);
    QVariant strokeStyle() const;
    void setStrokeStyle(const QVariant &v);
    qreal globalAlpha() const { return mAlpha; }
    void setGlobalAlpha(const qreal a);
    QString composite() const { return mComposite; }
    void setComposite(const QString &m);
    qreal lineWidth() const;
    void setLineWidth(const qreal w);
    QString lineCap() const { return mExtra.lineCap; }
    void setLineCap(const QString &c);
    QString lineJoin() const { return mExtra.lineJoin; }
    void setLineJoin(const QString &j);
    qreal miterLimit() const;
    void setMiterLimit(const qreal m);
    QString font() const { return mFontSpec; }
    void setFont(const QString &spec);
    QString textAlign() const { return mExtra.textAlign; }
    void setTextAlign(const QString &a) { mExtra.textAlign = a; }
    QString textBaseline() const { return mExtra.textBaseline; }
    void setTextBaseline(const QString &b) { mExtra.textBaseline = b; }
    QVariant filter() const { return mExtra.filter; }
    void setFilter(const QVariant &f) { mExtra.filter = f; }
    QVariant shadowColor() const { return mExtra.shadowColor; }
    void setShadowColor(const QVariant &c) { mExtra.shadowColor = c; }
    qreal shadowBlur() const { return mExtra.shadowBlur; }
    void setShadowBlur(const qreal b) { mExtra.shadowBlur = b; }
    qreal shadowOffsetX() const { return mExtra.shadowOffsetX; }
    void setShadowOffsetX(const qreal o) { mExtra.shadowOffsetX = o; }
    qreal shadowOffsetY() const { return mExtra.shadowOffsetY; }
    void setShadowOffsetY(const qreal o) { mExtra.shadowOffsetY = o; }
    bool smoothing() const { return mSmoothing; }
    void setSmoothing(const bool s) { mSmoothing = s; }
    QString smoothingQuality() const { return mExtra.smoothingQuality; }
    void setSmoothingQuality(const QString &q) { mExtra.smoothingQuality = q; }
    qreal lineDashOffset() const { return mLineDashOffset; }
    void setLineDashOffset(const qreal o);

    // ---- state ----
    Q_INVOKABLE void save();
    Q_INVOKABLE void restore();
    Q_INVOKABLE void scale(const qreal x, const qreal y);
    Q_INVOKABLE void rotate(const qreal radians);
    Q_INVOKABLE void translate(const qreal x, const qreal y);
    Q_INVOKABLE void transform(const qreal a, const qreal b,
                               const qreal c, const qreal d,
                               const qreal e, const qreal f);
    Q_INVOKABLE void setTransform(const qreal a, const qreal b,
                                  const qreal c, const qreal d,
                                  const qreal e, const qreal f);
    Q_INVOKABLE void resetTransform();
    Q_INVOKABLE QVariant getTransform();

    // ---- paths ----
    Q_INVOKABLE void beginPath();
    Q_INVOKABLE void closePath();
    Q_INVOKABLE void moveTo(const qreal x, const qreal y);
    Q_INVOKABLE void lineTo(const qreal x, const qreal y);
    Q_INVOKABLE void rect(const qreal x, const qreal y,
                          const qreal w, const qreal h);
    Q_INVOKABLE void arc(const qreal x, const qreal y, const qreal r,
                         const qreal start, const qreal end,
                         const bool ccw = false);
    Q_INVOKABLE void arcTo(const qreal x1, const qreal y1,
                           const qreal x2, const qreal y2, const qreal r);
    Q_INVOKABLE void ellipse(const qreal x, const qreal y, const qreal rx,
                             const qreal ry, const qreal rot,
                             const qreal start, const qreal end,
                             const bool ccw = false);
    Q_INVOKABLE void bezierCurveTo(const qreal c1x, const qreal c1y,
                                   const qreal c2x, const qreal c2y,
                                   const qreal x, const qreal y);
    Q_INVOKABLE void quadraticCurveTo(const qreal cx, const qreal cy,
                                      const qreal x, const qreal y);
    Q_INVOKABLE void roundRect(const qreal x, const qreal y,
                               const qreal w, const qreal h, const qreal r);

    // ---- paint ----
    Q_INVOKABLE void fill(const QString &rule = QString());
    Q_INVOKABLE void stroke();
    Q_INVOKABLE void clip(const QString &rule = QString());
    Q_INVOKABLE void fillRect(const qreal x, const qreal y,
                              const qreal w, const qreal h);
    Q_INVOKABLE void strokeRect(const qreal x, const qreal y,
                                const qreal w, const qreal h);
    Q_INVOKABLE void clearRect(const qreal x, const qreal y,
                               const qreal w, const qreal h);
    Q_INVOKABLE void fillText(const QString &text, const qreal x,
                              const qreal y);
    Q_INVOKABLE void strokeText(const QString &text, const qreal x,
                                const qreal y);
    Q_INVOKABLE QVariant measureText(const QString &text);
    Q_INVOKABLE void setLineDash(const QJSValue &segments);
    Q_INVOKABLE QVariant lineDash() const;

    // ---- resources ----
    Q_INVOKABLE QObject *createLinearGradient(const qreal x0, const qreal y0,
                                              const qreal x1, const qreal y1);
    Q_INVOKABLE QObject *createRadialGradient(const qreal x0, const qreal y0,
                                              const qreal r0, const qreal x1,
                                              const qreal y1, const qreal r1);
    Q_INVOKABLE QObject *createConicGradient(const qreal startAngle,
                                             const qreal x, const qreal y);
    Q_INVOKABLE QObject *createPattern(const QJSValue &source,
                                       const QString &repetition);

    // ---- images / pixels ----
    Q_INVOKABLE void drawImage(const QJSValue &source,
                               const qreal a = qQNaN(), const qreal b = qQNaN(),
                               const qreal c = qQNaN(), const qreal d = qQNaN(),
                               const qreal e = qQNaN(), const qreal f = qQNaN(),
                               const qreal g = qQNaN(), const qreal h = qQNaN());
    Q_INVOKABLE QJSValue getImageData(const qreal sx, const qreal sy,
                                      const qreal sw, const qreal sh);
    Q_INVOKABLE QJSValue createImageData(const qreal sw, const qreal sh);
    // rawBytes: Latin-1 encoded RGBA for pure-JS ImageData objects
    // (no efficient C++<-JS typed-array channel exists)
    Q_INVOKABLE void putImageData(const QJSValue &imageData,
                                  const qreal dx, const qreal dy,
                                  const QString &rawBytes = QString());
    Q_INVOKABLE bool isPointInPath(const qreal x, const qreal y);

private:
    struct Extra {
        QString textAlign = QStringLiteral("start");
        QString textBaseline = QStringLiteral("alphabetic");
        QString fontSpec;
        QVariant filter, shadowColor;
        qreal shadowBlur = 0, shadowOffsetX = 0, shadowOffsetY = 0;
        QString smoothingQuality;
        QString lineCap = QStringLiteral("butt");
        QString lineJoin = QStringLiteral("miter");
    };

    void ensurePainter();
    // balance any JS-side save() calls left open and end the painter
    // (QPainter::end with saved states warns and corrupts state)
    void endPainting();
    void applyFillBrush();   // fillStyle → painter brush
    void applyStrokePen();   // strokeStyle/width/cap/join → painter pen
    QBrush styleToBrush(const QVariant &v) const;
    QTransform baseTransform() const;
    void drawGlyphText(const QString &text, const qreal x, const qreal y,
                       const bool fillMode);
    QPointF alignedTextPos(const QString &text, const qreal x,
                           const qreal y) const;
    // recording hooks (append world-space primitives while the owning
    // canvas has recording on)
    void recText(const bool stroke, const QString &text,
                 const QPointF &baselinePos);
    void recRect(const bool stroke, const QRectF &localRect);
    void recPath(const bool stroke);

    JsCanvas2D *mCanvas = nullptr;
    QPainter mPainter;
    bool mPainting = false;
    int mSaveDepth = 0; // unbalanced JS save() calls to restore at end

    QPainterPath mPath;
    bool mIgnoreDraw = false; // HSL blend modes (no QPainter equivalent)
    QVariant mFillStyle{QStringLiteral("#000000")};
    QVariant mStrokeStyle{QStringLiteral("#000000")};
    qreal mAlpha = 1;
    QString mComposite = QStringLiteral("source-over");
    qreal mLineWidth = 1;
    qreal mMiterLimit = 10;
    qreal mLineDashOffset = 0;
    bool mSmoothing = true;
    QString mFontSpec;
    Extra mExtra;
    QStack<Extra> mExtraStack;
};

#endif // LYRICMOTIONCANVAS_H

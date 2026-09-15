/*
#
# Friction - https://friction.graphics
#
# Copyright (c) Ole-André Rodlie and contributors
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
# See 'README.md' for more information.
#
*/

#include "textanimpresetpanel.h"

#include "textanimpresets.h"
#include "layeranimpresets.h"
#include "Boxes/textbox.h"
#include "Boxes/rectangle.h"
#include "Animators/transformanimator.h"
#include "Animators/qpointfanimator.h"
#include "Animators/qrealanimator.h"
#include "Animators/texteffectcollection.h"

#include "Private/document.h"
#include "canvas.h"

#include "themesupport.h"

#include <QFontDatabase>
#include <QFutureWatcher>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QButtonGroup>
#include <QPainter>
#include <QPaintEvent>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QSharedPointer>
#include <QSlider>
#include <QSpinBox>
#include <QSvgRenderer>
#include <QtConcurrent/QtConcurrentMap>
#include "widgets/flowlayout.h"
#include <QTimer>
#include <QVBoxLayout>

// preview sampling parameters
namespace {
constexpr qreal gPreviewFps = 24.;
constexpr int gMaxFrames = 48;
constexpr int gMinFrames = 8;

// preview frame numbers for one loop of a preset
QList<qreal> framesForPreset(const TextAnimPreset& p,
                             const qreal durationScale)
{
    qreal start = 0;
    qreal total = 1;
    if (p.category == 2) {
        total = p.kind == TextAnim::wave ? 2*p.waveTime :
                                          4*p.duration*durationScale;
    } else {
        total = p.duration*durationScale + 0.6;
    }
    const int n = qBound(gMinFrames,
                         qRound(total*gPreviewFps),
                         gMaxFrames);
    QList<qreal> frames;
    frames.reserve(n);
    for (int i = 0; i < n; i++) {
        frames << (start + total*i/n)*gPreviewFps;
    }
    return frames;
}

QList<qreal> framesForLayerPreset(const LayerAnimPreset& p,
                                  const qreal durationScale)
{
    qreal start = 0;
    qreal total = 1;
    if (p.category == 2) {
        total = 2*p.duration*durationScale;
    } else {
        total = p.duration*durationScale + 0.6;
    }
    const int n = qBound(gMinFrames,
                         qRound(total*gPreviewFps),
                         gMaxFrames);
    QList<qreal> frames;
    frames.reserve(n);
    for (int i = 0; i < n; i++) {
        frames << (start + total*i/n)*gPreviewFps;
    }
    return frames;
}

// configures a sceneless preview text box
void configureSampleBox(TextBox* const box,
                        const QString& text,
                        const QString& family,
                        const SkFontStyle& style,
                        const qreal fontSize)
{
    box->setCurrentValue(text);
    if (!family.isEmpty()) {
        box->setFontFamilyAndStyle(family, style);
    }
    box->setFontSize(fontSize);
    box->setTextHAlignment(Qt::AlignCenter);
    const auto fill = box->getFillSettings();
    if (fill) {
        fill->setPaintType(PaintType::FLATPAINT);
        fill->setCurrentColor(QColor(235, 235, 235), false);
    }
    const auto stroke = box->getStrokeSettings();
    if (stroke) { stroke->setPaintType(PaintType::NOPAINT); }
}

void configureSampleRect(RectangleBox* const box,
                         const qreal halfW, const qreal halfH)
{
    box->setTopLeftPos(QPointF(-halfW, -halfH));
    box->setBottomRightPos(QPointF(halfW, halfH));
    const auto fill = box->getFillSettings();
    if (fill) {
        fill->setPaintType(PaintType::FLATPAINT);
        fill->setCurrentColor(QColor(235, 235, 235), false);
    }
}

// worker-thread render input: pure data, no widget pointers
struct RenderParams {
    const TextAnimPreset* tp = nullptr;
    const LayerAnimPreset* lp = nullptr;
    qreal scale = 1.0;
    QString cjkFamily;
    QImage mascot;
};

// runs on a QtConcurrent worker thread: sceneless sample box +
// effect evaluation + skia rasterization only, no gui objects
QList<QImage> renderPresetPreview(const RenderParams& rp)
{
    try {
        if (rp.tp) {
            const auto box = enve::make_shared<TextBox>();
            configureSampleBox(box.get(), QStringLiteral("friction"),
                               rp.cjkFamily, SkFontStyle(), 64);
            TextAnimPresets::apply(box.get(), *rp.tp, 0, gPreviewFps,
                                   rp.scale);
            return TextAnimPresets::renderPreviewSequence(
                        box.get(), framesForPreset(*rp.tp, rp.scale),
                        QSize(160, 160));
        }
        if (rp.lp) {
            // presets without an entrance recipe (sink) preview their
            // exit direction instead; plain attach (no undo)
            const bool previewOut = rp.lp->gen == nullptr;
            const auto box = enve::make_shared<RectangleBox>();
            configureSampleRect(box.get(), 55, 55);
            if (previewOut) {
                LayerAnimPresets::apply(box.get(), *rp.lp, -1, 0,
                                        gPreviewFps, rp.scale,
                                        400, 400, false);
            } else {
                LayerAnimPresets::apply(box.get(), *rp.lp, 0, -1,
                                        gPreviewFps, rp.scale,
                                        400, 400, false);
            }
            return LayerAnimPresets::renderPreviewSequence(
                        box.get(), framesForLayerPreset(*rp.lp, rp.scale),
                        QSize(160, 160), rp.mascot);
        }
    } catch (const std::exception& e) {
        qWarning() << "textanim preview render failed:" << e.what();
    } catch (...) {
        qWarning() << "textanim preview render failed: unknown error";
    }
    return {};
}
}

// ---------------------------------------------------------------- preview

TextAnimPreview::TextAnimPreview(QWidget* const parent)
    : QWidget(parent)
{
    setMinimumHeight(60);
}

void TextAnimPreview::setFrames(const QList<QImage>& frames)
{
    mFrames = frames;
    mFrame = 0;
    update();
}

void TextAnimPreview::advance()
{
    if (mFrames.count() > 1) {
        mFrame = (mFrame + 1) % mFrames.count();
        update();
    }
}

void TextAnimPreview::setPlaceholder(const QString& text)
{
    mPlaceholder = text;
    mFrames.clear();
    update();
}

void TextAnimPreview::paintEvent(QPaintEvent* const e)
{
    Q_UNUSED(e)
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    // rounded dark frame; content is clipped to it
    const int rad = ThemeSupport::borderRadius();
    QPainterPath frame;
    frame.addRoundedRect(rect().adjusted(0, 0, -1, -1), rad, rad);
    p.fillPath(frame, ThemeSupport::getThemeBaseDarkerColor());
    p.save();
    p.setClipPath(frame);
    if (!mFrames.isEmpty()) {
        const auto& img = mFrames.at(mFrame % mFrames.count());
        const qreal s = qMin(static_cast<qreal>(width() - 8)/img.width(),
                             static_cast<qreal>(height() - 8)/img.height());
        const int w = qRound(img.width()*s);
        const int h = qRound(img.height()*s);
        p.drawImage(QRect((width() - w)/2, (height() - h)/2, w, h), img);
    } else if (!mPlaceholder.isEmpty()) {
        QFont f = font();
        f.setPixelSize(12);
        p.setFont(f);
        p.setPen(QColor(140, 140, 140));
        p.drawText(rect(), Qt::AlignCenter, mPlaceholder);
    }
    p.restore();
}

// ---------------------------------------------------------------- tile

TextAnimTile::TextAnimTile(const TextAnimPreset* const textPreset,
                           const LayerAnimPreset* const layerPreset,
                           QWidget* const parent)
    : QWidget(parent)
    , mTextPreset(textPreset)
    , mLayerPreset(layerPreset)
{
    const auto lay = new QVBoxLayout(this);
    lay->setContentsMargins(5, 5, 5, 5);
    lay->setSpacing(3);

    // 1. Clean animated preview thumbnail
    mPreviewArea = new TextAnimPreview(this);
    mPreviewArea->setFixedSize(130, 130);
    lay->addWidget(mPreviewArea, 0, Qt::AlignHCenter);

    // 2. Preset Name Label (bold, clean)
    mNameLabel = new QLabel(this);
    QFont nf = mNameLabel->font();
    nf.setPixelSize(11);
    nf.setBold(true);
    mNameLabel->setFont(nf);
    mNameLabel->setAlignment(Qt::AlignCenter);
    mNameLabel->setText(mTextPreset ? mTextPreset->name :
                          mLayerPreset ? mLayerPreset->name : QString());
    mNameLabel->setStyleSheet(QStringLiteral("color: %1;")
                              .arg(palette().color(QPalette::WindowText).name()));
    lay->addWidget(mNameLabel);

    // 3. Subtitle / Tag Pill Label
    mTagLabel = new QLabel(this);
    QFont tf = mTagLabel->font();
    tf.setPixelSize(10);
    mTagLabel->setFont(tf);
    mTagLabel->setAlignment(Qt::AlignCenter);
    mTagLabel->setStyleSheet(QStringLiteral("color: %1;")
                             .arg(ThemeSupport::getThemeColorTextDisabled().name()));
    QString subText;
    if (mTextPreset) {
        const QString durStr = QString::number(mTextPreset->duration, 'f', 1) + QLatin1String("s");
        if (mTextPreset->category == 2) {
            subText = QString::fromUtf8("循环 · ") + durStr;
        } else {
            const QString fStr = (mTextPreset->fragment == 0) ? QString::fromUtf8("逐字") :
                                 (mTextPreset->fragment == 1) ? QString::fromUtf8("逐词") : QString::fromUtf8("逐行");
            subText = fStr + QString::fromUtf8(" · ") + durStr;
        }
    } else if (mLayerPreset) {
        const QString durStr = QString::number(mLayerPreset->duration, 'f', 1) + QLatin1String("s");
        if (mLayerPreset->category == 2) {
            subText = QString::fromUtf8("循环 · ") + durStr;
        } else {
            subText = (mLayerPreset->tag == QLatin1String("3d") ? QString::fromUtf8("3D空间 · ") : QString::fromUtf8("图层 · ")) + durStr;
        }
    }
    mTagLabel->setText(subText);
    lay->addWidget(mTagLabel);

    // 4. Action Buttons Container (Never obscuring the preview!)
    const auto btnContainer = new QWidget(this);
    const auto btnLay = new QHBoxLayout(btnContainer);
    btnLay->setContentsMargins(0, 0, 0, 0);
    btnLay->setSpacing(3);

    const auto addBtn = [&](const QString& label, const int dir) {
        auto btn = new QPushButton(label, btnContainer);
        QFont bf = btn->font();
        bf.setPixelSize(10);
        bf.setBold(true);
        btn->setFont(bf);
        btn->setFixedHeight(22);
        btn->setCursor(Qt::PointingHandCursor);
        const QColor btnBg = ThemeSupport::getThemeButtonBaseColor(240);
        const QColor btnBorder = ThemeSupport::getThemeButtonBorderColor(180);
        const QColor btnHoverBg = ThemeSupport::getThemeHighlightColor();
        const int rad = ThemeSupport::borderRadius() > 3 ? 3 : ThemeSupport::borderRadius();
        btn->setStyleSheet(QStringLiteral(
            "QPushButton { background: %1; color: %2; border: 1px solid %3; border-radius: %4px; padding: 0px 4px; font-weight: bold; }"
            "QPushButton:hover { background: %5; border-color: %5; color: #ffffff; }"
        ).arg(btnBg.name(QColor::HexArgb),
              palette().color(QPalette::WindowText).name(),
              btnBorder.name(QColor::HexArgb),
              QString::number(rad),
              btnHoverBg.name()));
        if (label == QString::fromUtf8("入")) {
            btn->setToolTip(QString::fromUtf8("应用为入场动画（在图层入点生效）"));
        } else if (label == QString::fromUtf8("全")) {
            btn->setToolTip(QString::fromUtf8("同时应用入场与出场完整动画"));
        } else if (label == QString::fromUtf8("出")) {
            btn->setToolTip(QString::fromUtf8("应用为出场动画（在图层出点生效）"));
        } else {
            btn->setToolTip(QString::fromUtf8("应用循环动画（持续运行）"));
        }
        mApplyButtons << btn;
        btnLay->addWidget(btn);
        connect(btn, &QPushButton::clicked, this, [this, dir]() {
            emit applyRequested(this, dir);
        });
    };

    const bool isLoop = mTextPreset ? mTextPreset->category == 2 :
                        mLayerPreset ? mLayerPreset->category == 2 : false;
    if (isLoop) {
        addBtn(QString::fromUtf8("应用循环"), 0);
    } else {
        const bool hasIn = !mLayerPreset || mLayerPreset->gen;
        const bool hasOut = mTextPreset ? mTextPreset->supportsOut :
                            mLayerPreset ? mLayerPreset->outGen != nullptr
                                         : false;
        const bool hasAll = hasIn && hasOut;
        if (hasIn) { addBtn(QString::fromUtf8("入"), 0); }
        if (hasAll) { addBtn(QString::fromUtf8("全"), 2); }
        if (hasOut) { addBtn(QString::fromUtf8("出"), 1); }
    }
    lay->addWidget(btnContainer);

    const QString name = mTextPreset ? mTextPreset->name : (mLayerPreset ? mLayerPreset->name : QString());
    const QString id = mTextPreset ? mTextPreset->id : (mLayerPreset ? mLayerPreset->id : QString());
    const QString desc = mTextPreset ? mTextPreset->desc : (mLayerPreset ? mLayerPreset->desc : QString());
    const qreal dur = mTextPreset ? mTextPreset->duration : (mLayerPreset ? mLayerPreset->duration : 1.0);
    const QString catStr = mTextPreset ? (mTextPreset->category == 2 ? QString::fromUtf8("文字循环") :
                                         (mTextPreset->fragment == 0 ? QString::fromUtf8("逐字动画") :
                                         (mTextPreset->fragment == 1 ? QString::fromUtf8("逐词动画") : QString::fromUtf8("逐行动画"))))
                                       : (mLayerPreset->category == 2 ? QString::fromUtf8("图层循环") : QString::fromUtf8("图层入出场"));

    const QString tip = QStringLiteral(
                "<div style='font-family:sans-serif; min-width:170px;'>"
                "<b style='font-size:12px; color:#60a5fa;'>%1</b> <span style='color:#9ca3af;'>[%2]</span><br/>"
                "<span style='color:#d1d5db; font-size:11px;'>类别：%3 · 时长：%4s</span>"
                "<p style='color:#e5e7eb; font-size:11px; margin-top:4px; line-height:1.3;'>%5</p>"
                "</div>")
            .arg(name, id, catStr, QString::number(dur, 'f', 1), desc);
    setToolTip(tip);

    // clicking preview, name, or tag selects the preset for details
    mPreviewArea->installEventFilter(this);
    mNameLabel->installEventFilter(this);
    mTagLabel->installEventFilter(this);
}

bool TextAnimTile::matches(const QString& query, const QString& filterTag) const
{
    if (filterTag != QLatin1String("all")) {
        if (filterTag == QLatin1String("sharp")) {
            if (!mTextPreset || mTextPreset->tag != QLatin1String("sharp")) return false;
        } else if (filterTag == QLatin1String("smooth")) {
            if (!mTextPreset || mTextPreset->tag != QLatin1String("smooth")) return false;
        } else if (filterTag == QLatin1String("prop")) {
            if (!mTextPreset || mTextPreset->tag != QLatin1String("prop")) return false;
        } else if (filterTag == QLatin1String("3d")) {
            const bool textMatch = mTextPreset && mTextPreset->tag == QLatin1String("3d");
            const bool layerMatch = mLayerPreset && (mLayerPreset->tag == QLatin1String("3d") || mLayerPreset->id.contains(QLatin1String("3d")));
            if (!textMatch && !layerMatch) return false;
        } else if (filterTag == QLatin1String("tech")) {
            if (!mTextPreset || mTextPreset->tag != QLatin1String("tech")) return false;
        } else if (filterTag == QLatin1String("layer")) {
            if (!mLayerPreset) return false;
        } else if (filterTag == QLatin1String("loop")) {
            const bool isLoop = (mTextPreset && (mTextPreset->category == 2 || mTextPreset->tag == QLatin1String("loop"))) ||
                                (mLayerPreset && mLayerPreset->category == 2);
            if (!isLoop) return false;
        }
    }

    if (!query.isEmpty()) {
        const QString name = mTextPreset ? mTextPreset->name : (mLayerPreset ? mLayerPreset->name : QString());
        const QString id = mTextPreset ? mTextPreset->id : (mLayerPreset ? mLayerPreset->id : QString());
        const QString desc = mTextPreset ? mTextPreset->desc : (mLayerPreset ? mLayerPreset->desc : QString());

        if (!name.contains(query, Qt::CaseInsensitive) &&
            !id.contains(query, Qt::CaseInsensitive) &&
            !desc.contains(query, Qt::CaseInsensitive)) {
            return false;
        }
    }

    return true;
}

bool TextAnimTile::eventFilter(QObject* const obj, QEvent* const e)
{
    if (e->type() == QEvent::MouseButtonRelease) {
        emit previewClicked(this);
    }
    return QWidget::eventFilter(obj, e);
}

QSize TextAnimTile::sizeHint() const
{
    const int w = mPreviewArea ? mPreviewArea->width() + 10 : 140;
    const int h = mPreviewArea ? mPreviewArea->height() + 72 : 202;
    return QSize(w, h);
}

void TextAnimTile::paintEvent(QPaintEvent* const e)
{
    Q_UNUSED(e)
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const int rad = ThemeSupport::borderRadius();
    const QRectF cardRect = QRectF(rect()).adjusted(1.0, 1.0, -1.0, -1.0);
    QPainterPath cardPath;
    cardPath.addRoundedRect(cardRect, rad, rad);

    // Morandi card background
    p.fillPath(cardPath, ThemeSupport::getThemeBaseDarkerColor(150));

    // Morandi card border with state styling
    QColor border = ThemeSupport::getThemeButtonBorderColor(100);
    qreal penWidth = 1.0;
    if (mChecked) {
        border = ThemeSupport::getThemeHighlightColor();
        penWidth = 2.0;
    } else if (mHover) {
        border = ThemeSupport::getThemeButtonHoverColor();
    }
    p.setPen(QPen(border, penWidth));
    p.setBrush(Qt::NoBrush);
    p.drawPath(cardPath);
}

void TextAnimTile::enterEvent(QtEnterEvent* const e)
{
    Q_UNUSED(e)
    mHover = true;
    update();
}

void TextAnimTile::leaveEvent(QEvent* const e)
{
    Q_UNUSED(e)
    mHover = false;
    update();
}

void TextAnimTile::setPreviewSize(const int size)
{
    if (mPreviewArea) {
        mPreviewArea->setFixedSize(size, size);
    }
    updateGeometry();
}

void TextAnimTile::setFrames(const QList<QImage>& frames)
{
    // the preview widget owns the frame list; the tile keeps no copy
    if (mPreviewArea) { mPreviewArea->setFrames(frames); }
}

void TextAnimTile::setLoading()
{
    if (mPreviewArea) {
        mPreviewArea->setPlaceholder(QString::fromUtf8("渲染中…"));
    }
}

void TextAnimTile::advance()
{
    // zero per-tick allocation: the preview widget just advances
    // its frame index and repaints
    if (mPreviewArea) { mPreviewArea->advance(); }
}

// ---------------------------------------------------------------- panel

TextAnimPresetPanel::TextAnimPresetPanel(Document& doc,
                                         QWidget* const parent)
    : QWidget(parent)
    , mDocument(doc)
{
    setMinimumSize(360, 440);
    setAutoFillBackground(true);
    setPalette(ThemeSupport::getDarkPalette());

    const auto rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(6, 6, 6, 6);
    rootLayout->setSpacing(6);

    // 1. Search Bar
    mSearchEdit = new QLineEdit(this);
    mSearchEdit->setPlaceholderText(QString::fromUtf8("搜索预设（名称、拼音、效果描述）..."));
    mSearchEdit->setClearButtonEnabled(true);
    mSearchEdit->setFixedHeight(28);
    mSearchEdit->setStyleSheet(QStringLiteral(
        "QLineEdit {"
        "  background: %1;"
        "  border: 1px solid %2;"
        "  border-radius: %3px;"
        "  padding: 0 8px;"
        "  color: %4;"
        "  selection-background-color: %5;"
        "}"
        "QLineEdit:focus {"
        "  border-color: %5;"
        "}"
    ).arg(ThemeSupport::getThemeBaseDarkColor().name(),
          ThemeSupport::getThemeButtonBorderColor().name(),
          QString::number(ThemeSupport::borderRadius() > 5 ? 5 : ThemeSupport::borderRadius()),
          palette().color(QPalette::WindowText).name(),
          ThemeSupport::getThemeHighlightColor().name()));
    rootLayout->addWidget(mSearchEdit);

    // 2. Category Navigation Tab Bar (with pill styling & counts)
    const auto catScroll = new QScrollArea(this);
    catScroll->setWidgetResizable(true);
    catScroll->setFixedHeight(32);
    catScroll->setFrameShape(QFrame::NoFrame);
    catScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    catScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    catScroll->setAutoFillBackground(false);
    catScroll->viewport()->setAutoFillBackground(false);

    const auto catHost = new QWidget(catScroll);
    const auto catLayout = new QHBoxLayout(catHost);
    catLayout->setContentsMargins(0, 1, 0, 1);
    catLayout->setSpacing(5);

    mCategoryGroup = new QButtonGroup(this);
    mCategoryGroup->setExclusive(true);

    const struct FilterBtnDef {
        QString label;
        QString tag;
    } filterDefs[] = {
        { QString::fromUtf8("图层动效"), QStringLiteral("layer") },
        { QString::fromUtf8("极客代码"), QStringLiteral("tech") },
        { QString::fromUtf8("锐利动力"), QStringLiteral("sharp") },
        { QString::fromUtf8("丝滑流体"), QStringLiteral("smooth") },
        { QString::fromUtf8("属性通道"), QStringLiteral("prop") },
        { QString::fromUtf8("3D空间"), QStringLiteral("3d") },
        { QString::fromUtf8("持续循环"), QStringLiteral("loop") },
        { QString::fromUtf8("全部"), QStringLiteral("all") }
    };

    const QString pillStyle = QStringLiteral(
        "QPushButton {"
        "  background: %1;"
        "  color: %2;"
        "  border: 1px solid %3;"
        "  border-radius: 11px;"
        "  padding: 2px 10px;"
        "  font-size: 11px;"
        "  font-weight: 500;"
        "}"
        "QPushButton:hover {"
        "  background: %4;"
        "  color: %5;"
        "  border-color: %6;"
        "}"
        "QPushButton:checked {"
        "  background: %6;"
        "  border-color: %6;"
        "  color: #ffffff;"
        "  font-weight: bold;"
        "}"
    ).arg(ThemeSupport::getThemeButtonBaseColor().name(),
          ThemeSupport::getThemeColorTextDisabled().name(),
          ThemeSupport::getThemeButtonBorderColor().name(),
          ThemeSupport::getThemeButtonHoverColor().name(),
          palette().color(QPalette::WindowText).name(),
          ThemeSupport::getThemeHighlightColor().name());

    for (const auto& def : filterDefs) {
        auto btn = new QPushButton(def.label, catHost);
        btn->setCheckable(true);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(pillStyle);
        btn->setProperty("filterTag", def.tag);
        btn->setProperty("baseLabel", def.label);
        if (def.tag == QLatin1String("all")) {
            btn->setChecked(true);
        }
        mCategoryGroup->addButton(btn);
        catLayout->addWidget(btn);
    }
    catLayout->addStretch(1);
    catScroll->setWidget(catHost);
    rootLayout->addWidget(catScroll);

    connect(mSearchEdit, &QLineEdit::textChanged, this, [this]() {
        filterPresets();
    });
    connect(mCategoryGroup, static_cast<void(QButtonGroup::*)(QAbstractButton*)>(&QButtonGroup::buttonClicked),
            this, [this](QAbstractButton* btn) {
        if (btn) {
            mActiveFilterTag = btn->property("filterTag").toString();
            filterPresets();
        }
    });

    // 3. Preset Cards Gallery Flow Layout
    mScroll = new QScrollArea(this);
    mScroll->setWidgetResizable(true);
    mScroll->setFrameShape(QFrame::NoFrame);
    mScroll->setAutoFillBackground(true);
    mScroll->viewport()->setAutoFillBackground(true);

    mScrollHost = new QWidget(mScroll);
    mScrollHost->setAutoFillBackground(true);
    mFlow = new FlowLayout(mScrollHost);
    mFlow->setContentsMargins(4, 4, 4, 4);
    mFlow->setSpacing(8);

    mScroll->setWidget(mScrollHost);
    rootLayout->addWidget(mScroll, 1);

    // 4. Structured Footer Controls Toolbar
    const auto footerWidget = new QWidget(this);
    footerWidget->setAutoFillBackground(true);
    const auto footerLayout = new QVBoxLayout(footerWidget);
    footerLayout->setContentsMargins(2, 2, 2, 2);
    footerLayout->setSpacing(4);

    // Row 1: Duration & Zoom Sliders
    const auto ctrlRow = new QHBoxLayout();
    ctrlRow->setSpacing(6);
    ctrlRow->setContentsMargins(0, 0, 0, 0);

    const auto durLabel = new QLabel(QString::fromUtf8("时长:"), this);
    {
        QFont lf = durLabel->font();
        lf.setPixelSize(11);
        durLabel->setFont(lf);
        durLabel->setStyleSheet(QStringLiteral("color: %1;")
                                .arg(ThemeSupport::getThemeColorTextDisabled().name()));
    }
    ctrlRow->addWidget(durLabel);

    mDurationSlider = new QSlider(Qt::Horizontal, this);
    mDurationSlider->setRange(50, 300);
    mDurationSlider->setSingleStep(10);
    mDurationSlider->setValue(100);
    mDurationSlider->setMaximumHeight(18);
    mDurationSlider->setToolTip(QString::fromUtf8("动画时长缩放（%1%）"));
    ctrlRow->addWidget(mDurationSlider, 1);

    mDurationSpin = new QSpinBox(this);
    mDurationSpin->setRange(50, 300);
    mDurationSpin->setSuffix(QStringLiteral("%"));
    mDurationSpin->setValue(100);
    mDurationSpin->setFixedWidth(54);
    mDurationSpin->setToolTip(QString::fromUtf8("动画时长缩放百分比"));
    ctrlRow->addWidget(mDurationSpin);

    mDurationResetBtn = new QPushButton(QString::fromUtf8("↺"), this);
    mDurationResetBtn->setFixedSize(22, 22);
    mDurationResetBtn->setCursor(Qt::PointingHandCursor);
    mDurationResetBtn->setToolTip(QString::fromUtf8("重置时长缩放到 100%"));
    mDurationResetBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: %1; color: %2; border: 1px solid %3; border-radius: %4px; font-weight: bold; }"
        "QPushButton:hover { background: %5; color: #ffffff; border-color: %6; }"
    ).arg(ThemeSupport::getThemeButtonBaseColor().name(),
          ThemeSupport::getThemeColorTextDisabled().name(),
          ThemeSupport::getThemeButtonBorderColor().name(),
          QString::number(ThemeSupport::borderRadius() > 4 ? 4 : ThemeSupport::borderRadius()),
          ThemeSupport::getThemeButtonHoverColor().name(),
          ThemeSupport::getThemeHighlightColor().name()));
    connect(mDurationResetBtn, &QPushButton::clicked, this, [this]() {
        mDurationSlider->setValue(100);
    });
    ctrlRow->addWidget(mDurationResetBtn);

    const auto zoomLabel = new QLabel(QString::fromUtf8("视图:"), this);
    {
        QFont zf = zoomLabel->font();
        zf.setPixelSize(11);
        zoomLabel->setFont(zf);
        zoomLabel->setStyleSheet(QStringLiteral("color: %1;")
                                .arg(ThemeSupport::getThemeColorTextDisabled().name()));
    }
    ctrlRow->addWidget(zoomLabel);

    mTileSizeSlider = new QSlider(Qt::Horizontal, this);
    mTileSizeSlider->setRange(90, 200);
    mTileSizeSlider->setSingleStep(10);
    mTileSizeSlider->setValue(mTileSize);
    mTileSizeSlider->setMaximumHeight(18);
    mTileSizeSlider->setToolTip(QString::fromUtf8("预览卡片尺寸"));
    ctrlRow->addWidget(mTileSizeSlider, 1);
    footerLayout->addLayout(ctrlRow);

    // Row 2: Status Indicator & Clear Button
    const auto statusRow = new QHBoxLayout();
    statusRow->setContentsMargins(0, 0, 0, 0);
    statusRow->setSpacing(6);

    mStatusDot = new QLabel(this);
    mStatusDot->setFixedSize(8, 8);
    mStatusDot->setStyleSheet(QStringLiteral("background-color: %1; border-radius: 4px;")
                              .arg(ThemeSupport::getThemeHighlightColor().name()));
    statusRow->addWidget(mStatusDot, 0, Qt::AlignVCenter);

    mStatusLabel = new QLabel(this);
    {
        QFont f = mStatusLabel->font();
        f.setPixelSize(11);
        mStatusLabel->setFont(f);
    }
    mStatusLabel->setWordWrap(false);
    mStatusLabel->setStyleSheet(QStringLiteral("color: %1;")
                                .arg(palette().color(QPalette::WindowText).name()));
    statusRow->addWidget(mStatusLabel, 1, Qt::AlignVCenter);

    mClearButton = new QPushButton(QString::fromUtf8("清除预设"), this);
    {
        QFont bf = mClearButton->font();
        bf.setPixelSize(11);
        mClearButton->setFont(bf);
    }
    mClearButton->setCursor(Qt::PointingHandCursor);
    const QColor redCol = ThemeSupport::getThemeColorRed();
    const int clearRad = ThemeSupport::borderRadius() > 4 ? 4 : ThemeSupport::borderRadius();
    mClearButton->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  background: %1;"
        "  color: %2;"
        "  border: 1px solid %3;"
        "  border-radius: %4px;"
        "  padding: 3px 8px;"
        "}"
        "QPushButton:hover {"
        "  background: %5;"
        "  color: #ffffff;"
        "  border-color: %2;"
        "}"
    ).arg(QColor(redCol.red(), redCol.green(), redCol.blue(), 35).name(QColor::HexArgb),
          redCol.name(),
          QColor(redCol.red(), redCol.green(), redCol.blue(), 90).name(QColor::HexArgb),
          QString::number(clearRad),
          QColor(redCol.red(), redCol.green(), redCol.blue(), 180).name(QColor::HexArgb)));
    mClearButton->setToolTip(QString::fromUtf8("移除选中图层上已添加的预设表达式与文字特效"));
    connect(mClearButton, &QPushButton::clicked, this, &TextAnimPresetPanel::clearSelectedPresets);
    statusRow->addWidget(mClearButton, 0, Qt::AlignVCenter);

    footerLayout->addLayout(statusRow);
    rootLayout->addWidget(footerWidget);

    connect(mTileSizeSlider, &QSlider::valueChanged, this, [this](const int v) {
        mTileSize = v;
        for (const auto tile : mTiles) {
            tile->setPreviewSize(v);
        }
        if (mFlow) {
            mFlow->invalidate();
            mScrollHost->adjustSize();
        }
    });

    mDurationReRenderTimer = new QTimer(this);
    mDurationReRenderTimer->setSingleShot(true);
    mDurationReRenderTimer->setInterval(300);
    connect(mDurationReRenderTimer, &QTimer::timeout, this, [this]() {
        queueRender(mTiles);
    });

    connect(mDurationSlider, &QSlider::valueChanged, this, [this](const int v) {
        mDurationScale = v/100.;
        mScaleGeneration++;
        mDurationSpin->blockSignals(true);
        mDurationSpin->setValue(v);
        mDurationSpin->blockSignals(false);
        mDurationSlider->setToolTip(
                    QString::fromUtf8("动画时长缩放（%1%）").arg(v));
        mDurationReRenderTimer->start();
    });
    connect(mDurationSpin, static_cast<void(QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            this, [this](const int v) {
        mDurationScale = v/100.;
        mScaleGeneration++;
        mDurationSlider->blockSignals(true);
        mDurationSlider->setValue(v);
        mDurationSlider->blockSignals(false);
        mDurationReRenderTimer->start();
    });

    mPlayTimer = new QTimer(this);
    mPlayTimer->setInterval(40);
    connect(mPlayTimer, &QTimer::timeout, this, [this]() {
        for (const auto tile : mTiles) {
            if (!tile->visibleRegion().isEmpty()) { tile->advance(); }
        }
    });
}

void TextAnimPresetPanel::buildAllTiles()
{
    if (mBuilt) { return; }
    mBuilt = true;

    QList<TextAnimTile*> added;
    const auto addTile = [&](const TextAnimPreset* const tp,
                             const LayerAnimPreset* const lp) {
        const auto tile = new TextAnimTile(tp, lp, mScrollHost);
        connect(tile, &TextAnimTile::previewClicked,
                this, [this](TextAnimTile* t) { selectTile(t); });
        connect(tile, &TextAnimTile::applyRequested,
                this, [this](TextAnimTile* t, const int dir) {
            applyPreset(t, dir);
        });
        tile->setPreviewSize(mTileSize);
        mFlow->addWidget(tile);
        mTiles << tile;
        added << tile;
    };

    for (const auto& p : TextAnimPresets::all()) {
        addTile(&p, nullptr);
    }
    for (const auto& p : LayerAnimPresets::all()) {
        addTile(nullptr, &p);
    }

    updateCategoryCounts();
    queueRender(added);
}

void TextAnimPresetPanel::updateCategoryCounts()
{
    if (!mCategoryGroup) { return; }
    for (const auto btn : mCategoryGroup->buttons()) {
        const QString tag = btn->property("filterTag").toString();
        const QString baseLabel = btn->property("baseLabel").toString();
        int count = 0;
        for (const auto tile : mTiles) {
            if (tile->matches(QString(), tag)) {
                count++;
            }
        }
        btn->setText(QStringLiteral("%1 (%2)").arg(baseLabel).arg(count));
    }
}

void TextAnimPresetPanel::filterPresets()
{
    if (!mBuilt) {
        buildAllTiles();
    }
    const QString query = mSearchEdit ? mSearchEdit->text().trimmed() : QString();
    int visibleCount = 0;
    for (const auto tile : mTiles) {
        const bool match = tile->matches(query, mActiveFilterTag);
        tile->setVisible(match);
        if (match) {
            visibleCount++;
        }
    }
    if (mFlow) {
        mFlow->invalidate();
        mScrollHost->adjustSize();
    }
    if (query.isEmpty() && mActiveFilterTag == QLatin1String("all")) {
        setStatusText(QString::fromUtf8("全库共 %1 个动效预设 · 点击 [入] 或 [出] 快速应用").arg(mTiles.count()), StatusType::Normal);
    } else {
        setStatusText(QString::fromUtf8("筛选显示 %1 个预设（总库 %2 个）").arg(visibleCount).arg(mTiles.count()), StatusType::Normal);
    }
}

void TextAnimPresetPanel::setStatusText(const QString& text, const StatusType type)
{
    if (mStatusLabel) {
        mStatusLabel->setText(text);
    }
    if (mStatusDot) {
        QString color;
        switch (type) {
        case StatusType::Success:
            color = ThemeSupport::getThemeColorGreen().name();
            break;
        case StatusType::Warning:
            color = ThemeSupport::getThemeColorOrange().name();
            break;
        case StatusType::Normal:
        default:
            color = ThemeSupport::getThemeHighlightColor().name();
            break;
        }
        mStatusDot->setStyleSheet(QStringLiteral("background-color: %1; border-radius: 4px;").arg(color));
    }
}

void TextAnimPresetPanel::clearSelectedPresets()
{
    const auto scene = mDocument.fActiveScene.data();
    if (!scene) {
        setStatusText(QString::fromUtf8("没有活动场景。"), StatusType::Warning);
        return;
    }
    const auto selected = scene->getSelectedBoxesList();
    if (selected.isEmpty()) {
        setStatusText(QString::fromUtf8("请先选择图层。"), StatusType::Warning);
        return;
    }

    selected.first()->prp_pushUndoRedoName(QString::fromUtf8("清除预设"));
    int clearedCount = 0;
    for (const auto& box : selected) {
        bool changed = false;
        if (const auto tb = dynamic_cast<TextBox*>(box)) {
            if (const auto effects = tb->getTextEffects()) {
                if (effects->hasEffects()) {
                    effects->clear();
                    changed = true;
                }
            }
        }
        if (const auto t = dynamic_cast<AdvancedTransformAnimator*>(box->getTransformAnimator())) {
            const auto pos = t->getPosAnimator();
            const auto scale = t->getScaleAnimator();
            const auto rotA = t->getRotAnimator();
            const auto opA = t->getOpacityAnimator();
            const auto shear = t->getShearAnimator();
            const auto rotX = t->getRotXAnimator();
            const auto rotY = t->getRotYAnimator();
            const auto zPos = t->getZPosAnimator();

            const QList<QrealAnimator*> animators = {
                pos ? pos->getXAnimator() : nullptr,
                pos ? pos->getYAnimator() : nullptr,
                scale ? scale->getXAnimator() : nullptr,
                scale ? scale->getYAnimator() : nullptr,
                rotA, opA,
                shear ? shear->getXAnimator() : nullptr,
                shear ? shear->getYAnimator() : nullptr,
                rotX, rotY, zPos
            };
            for (const auto anim : animators) {
                if (anim && anim->hasExpression()) {
                    anim->setExpressionAction(nullptr);
                    changed = true;
                }
            }
        }
        if (changed) {
            clearedCount++;
        }
    }
    Document::sInstance->actionFinished();
    scene->updateAllBoxes(UpdateReason::userChange);

    if (clearedCount > 0) {
        setStatusText(QString::fromUtf8("已清除 %1 个图层上的动画预设。").arg(clearedCount), StatusType::Success);
    } else {
        setStatusText(QString::fromUtf8("所选图层未应用任何预设。"), StatusType::Normal);
    }
}

void TextAnimPresetPanel::queueRender(const QList<TextAnimTile*>& tiles)
{
    if (tiles.isEmpty()) { return; }
    // hidden panel: defer all rendering to the next showEvent
    if (!isVisible()) { mRenderAllOnShow = true; return; }

    QVector<RenderParams> params;
    QVector<QPointer<TextAnimTile>> targets;
    params.reserve(tiles.size());
    targets.reserve(tiles.size());
    for (const auto tile : tiles) {
        if (!tile) { continue; }
        RenderParams rp;
        rp.tp = tile->textPreset();
        rp.lp = tile->layerPreset();
        rp.scale = mDurationScale;
        if (rp.lp) { rp.mascot = mascotImage(); }
        else { rp.cjkFamily = defaultCjkFamily(); }
        params << rp;
        targets << tile;
        tile->setLoading();
    }
    if (params.isEmpty()) { return; }

    // plain function pointer: this Qt5's mapped() result deduction
    // needs result_type, which lambdas do not provide
    const int gen = mScaleGeneration;
    const auto targetsPtr =
            QSharedPointer<QVector<QPointer<TextAnimTile>>>::create(targets);
    const auto watcher = new QFutureWatcher<QList<QImage>>(this);
    connect(watcher, &QFutureWatcher<QList<QImage>>::finished,
            this, [this, watcher, targetsPtr, gen]() {
        watcher->deleteLater();
        // the duration scale changed while rendering: the frames
        // no longer match what the next Apply would bake - drop them
        if (gen != mScaleGeneration) { return; }
        const auto results = watcher->future().results();
        const int n = qMin(results.size(), targetsPtr->size());
        for (int i = 0; i < n; i++) {
            if (const auto tile = targetsPtr->at(i)) {
                tile->setFrames(results.at(i));
            }
        }
    });
    watcher->setFuture(QtConcurrent::mapped(params, &renderPresetPreview));
}

void TextAnimPresetPanel::selectTile(TextAnimTile* const tile)
{
    if (!tile) { return; }
    for (const auto t : mTiles) {
        t->setChecked(t == tile);
    }
}

void TextAnimPresetPanel::applyPreset(TextAnimTile* const tile,
                                      const int dir)
{
    if (!tile) { return; }
    const auto scene = mDocument.fActiveScene.data();
    if (!scene) {
        setStatusText(QString::fromUtf8("没有活动场景。"), StatusType::Warning);
        return;
    }
    const qreal fps = scene->getFps();

    if (const auto preset = tile->layerPreset()) {
        const auto selected = scene->getSelectedBoxesList();
        if (selected.isEmpty()) {
            setStatusText(QString::fromUtf8("请先选择图层。"), StatusType::Warning);
            return;
        }
        const qreal cw = scene->getCanvasWidth();
        const qreal ch = scene->getCanvasHeight();
        if (dir == 1 && !preset->outGen) {
            setStatusText(QString::fromUtf8("该预设不支持出点。"), StatusType::Warning);
            return;
        }
        if (dir == 0 && !preset->gen) {
            setStatusText(QString::fromUtf8("该预设不支持入点。"), StatusType::Warning);
            return;
        }
        // one undo block for the whole batch: the stack closes the
        // set at actionFinished, extra pushes mid-loop are no-ops
        selected.first()->prp_pushUndoRedoName(
                    QString::fromUtf8("应用预设「%1」").arg(preset->name));
        int applied = 0;
        bool exitClamped = false;
        for (const auto& box : selected) {
            // anchor to the layer's own in/out points instead of the
            // playhead; full-length layers fall back to the scene range
            const auto durRect = box->getDurationRectangle();
            const int inF = durRect ? durRect->getMinAbsFrame()
                                    : scene->getMinFrame();
            const int outEndF = durRect ? durRect->getMaxAbsFrame()
                                        : scene->getMaxFrame();
            const int durF = qMax(2, qRound(preset->duration*
                                            mDurationScale*fps));
            // one combined expression per animator: entrance owns
            // [inF, outStart), the exit window takes over from there
            const int inStart = (dir == 0 || dir == 2) ? inF : -1;
            int outStart = -1;
            if ((dir == 1 || dir == 2) && preset->outGen) {
                // exit finishes exactly at the layer's end; never let
                // it start before the in-point on very short layers
                // (a negative start would anchor expressions to
                // invalid frames)
                outStart = qMax(inF, outEndF - durF);
                if (outEndF - durF < inF) { exitClamped = true; }
            }
            LayerAnimPresets::apply(box, *preset, inStart, outStart,
                                    fps, mDurationScale, cw, ch);
            applied++;
        }
        const QString dirText = dir == 0 ? QString::fromUtf8("（入点）") :
                dir == 1 ? QString::fromUtf8("（出点）") :
                           QString::fromUtf8("（入+出）");
        setStatusText(
                    QString::fromUtf8("已应用「%1%2」到 %3 个图层。")
                    .arg(preset->name).arg(dirText).arg(applied)
                + (exitClamped ? QString::fromUtf8(
                       "有图层短于预设时长，出点已前移到入点。")
                               : QString()), StatusType::Success);
        Document::sInstance->actionFinished();
        scene->updateAllBoxes(UpdateReason::userChange);
        return;
    }

    const auto preset = tile->textPreset();
    if (!preset) { return; }
    if (dir == 1 && !preset->supportsOut) {
        setStatusText(QString::fromUtf8("该预设仅支持入点。"), StatusType::Warning);
        return;
    }
    const auto targets = selectedTextBoxes();
    if (targets.isEmpty()) {
        setStatusText(QString::fromUtf8("请先选择文字图层。"), StatusType::Warning);
        return;
    }
    // one undo block for the whole batch (see the layer path above)
    targets.first()->prp_pushUndoRedoName(
                QString::fromUtf8("应用文字预设「%1」").arg(preset->name));
    int applied = 0;
    bool exitClamped = false;
    for (const auto tb : targets) {
        const auto durRect = tb->getDurationRectangle();
        const int inF = durRect ? durRect->getMinAbsFrame()
                                : scene->getMinFrame();
        const int outEndF = durRect ? durRect->getMaxAbsFrame()
                                    : scene->getMaxFrame();
        const int durF = qMax(2, qRound(preset->duration*
                                        mDurationScale*fps));
        bool ok = true;
        if (dir == 0 || dir == 2) {
            ok = TextAnimPresets::apply(tb, *preset, inF,
                                        fps, mDurationScale, false);
        }
        if (ok && (dir == 1 || dir == 2)) {
            // exit finishes exactly at the layer's end; never let it
            // start before the in-point on very short layers
            const int outStart = qMax(inF, outEndF - durF);
            if (outEndF - durF < inF) { exitClamped = true; }
            ok = TextAnimPresets::apply(tb, *preset, outStart,
                                        fps, mDurationScale, true);
        }
        if (ok) { applied++; }
    }
    if (applied == 0) {
        setStatusText(preset->id == QLatin1String("number-roll")
            ? QString::fromUtf8("数字滚动需要文本包含数字（如“进度 42%”）。")
            : QString::fromUtf8("预设应用失败。"), StatusType::Warning);
    } else {
        const QString dirText = dir == 0 ? QString() :
                dir == 1 ? QString::fromUtf8("（出点）") :
                           QString::fromUtf8("（入+出）");
        setStatusText(
                    QString::fromUtf8("已应用「%1%2」到 %3 个文字层。")
                    .arg(preset->name).arg(dirText).arg(applied)
                + (exitClamped ? QString::fromUtf8(
                       "有图层短于预设时长，出点已前移到入点。")
                               : QString()), StatusType::Success);
    }
    Document::sInstance->actionFinished();
    scene->updateAllBoxes(UpdateReason::userChange);
}

QList<BoundingBox*> TextAnimPresetPanel::selectedBoxes() const
{
    const auto scene = mDocument.fActiveScene.data();
    if (!scene) { return {}; }
    return scene->getSelectedBoxesList();
}

QList<TextBox*> TextAnimPresetPanel::selectedTextBoxes() const
{
    QList<TextBox*> result;
    for (const auto& box : selectedBoxes()) {
        const auto tb = dynamic_cast<TextBox*>(box);
        if (tb) { result << tb; }
    }
    return result;
}

const QImage& TextAnimPresetPanel::mascotImage()
{
    if (!mMascotTried) {
        mMascotTried = true;
        QSvgRenderer renderer(QStringLiteral(":/assets/mascot.svg"));
        if (renderer.isValid()) {
            mMascot = QImage(256, 256,
                             QImage::Format_ARGB32_Premultiplied);
            mMascot.fill(Qt::transparent);
            QPainter p(&mMascot);
            renderer.render(&p);
        }
    }
    return mMascot;
}

const QString& TextAnimPresetPanel::defaultCjkFamily()
{
    if (mDefaultCjkFamily.isEmpty()) {
        QFontDatabase db;
        const QStringList preferred = {
            QString::fromUtf8("微软雅黑"), "Microsoft YaHei",
            QString::fromUtf8("思源黑体 CN"), "Source Han Sans CN",
            QString::fromUtf8("黑体"), "SimHei"
        };
        for (const auto& family : preferred) {
            if (db.families().contains(family)) {
                mDefaultCjkFamily = family;
                return mDefaultCjkFamily;
            }
        }
        for (const auto& family : db.families()) {
            const auto systems = db.writingSystems(family);
            if (systems.contains(QFontDatabase::SimplifiedChinese)) {
                mDefaultCjkFamily = family;
                return mDefaultCjkFamily;
            }
        }
        mDefaultCjkFamily = QString();
    }
    return mDefaultCjkFamily;
}

void TextAnimPresetPanel::showEvent(QShowEvent* const e)
{
    QWidget::showEvent(e);
    if (!mBuilt) {
        buildAllTiles();
        filterPresets();
        if (!mTiles.isEmpty()) { selectTile(mTiles.first()); }
    } else if (mRenderAllOnShow) {
        mRenderAllOnShow = false;
        queueRender(mTiles);
    }
    mPlayTimer->start();
}

void TextAnimPresetPanel::hideEvent(QHideEvent* const e)
{
    QWidget::hideEvent(e);
    mPlayTimer->stop();
}

void TextAnimPresetPanel::setGalleryPaused(const bool paused)
{
    if (paused) {
        mPlayTimer->stop();
    } else if (isVisible()) {
        mPlayTimer->start();
    }
}

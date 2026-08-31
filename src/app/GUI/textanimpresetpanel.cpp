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

#include "Private/document.h"
#include "canvas.h"

#include "themesupport.h"

#include <QCoreApplication>
#include <QFontDatabase>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPaintEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSvgRenderer>
#include "widgets/flowlayout.h"
#include <QTimer>
#include <QVBoxLayout>

// preview bake parameters
namespace {
constexpr qreal gPreviewFps = 24.;
constexpr int gMaxFrames = 72;
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
    QPainterPath frame;
    frame.addRoundedRect(rect().adjusted(0, 0, -1, -1), 8, 8);
    p.fillPath(frame, QColor(12, 12, 14));
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
    lay->setContentsMargins(4, 4, 4, 4);
    lay->setSpacing(2);

    // square preview area (size driven by the panel zoom slider)
    mPreviewArea = new TextAnimPreview(this);
    mPreviewArea->setFixedSize(150, 150);
    lay->addWidget(mPreviewArea, 0, Qt::AlignHCenter);

    mNameLabel = new QLabel(this);
    QFont nf = mNameLabel->font();
    nf.setPixelSize(11);
    mNameLabel->setFont(nf);
    mNameLabel->setAlignment(Qt::AlignCenter);
    mNameLabel->setText(mTextPreset ? mTextPreset->name :
                          mLayerPreset ? mLayerPreset->name : QString());
    lay->addWidget(mNameLabel);
    lay->addStretch(1);

    // small square apply buttons overlaid on the preview, centered
    // near its bottom edge: in / all / out (loops get one apply)
    const auto addBtn = [&](const QString& label, const int dir) {
        auto btn = new QPushButton(label, mPreviewArea);
        QFont bf = btn->font();
        bf.setPixelSize(11);
        bf.setBold(true);
        btn->setFont(bf);
        btn->setFixedSize(40, 26);
        btn->setCursor(Qt::PointingHandCursor);
        // nearly opaque pill so the animated content behind does
        // not wash the label out (user screens run 144dpi)
        btn->setStyleSheet(
                    "QPushButton { background: rgba(16,16,20,240);"
                    " color: #f0f0f0; border: 1px solid #71717a;"
                    " border-radius: 4px; padding: 0px; }"
                    "QPushButton:hover { background: rgba(60,90,150,240); }");
        mApplyButtons << btn;
        connect(btn, &QPushButton::clicked, this, [this, dir]() {
            emit applyRequested(this, dir);
        });
    };
    const bool isLoop = mTextPreset ? mTextPreset->category == 2 :
                        mLayerPreset ? mLayerPreset->category == 2 : false;
    if (isLoop) {
        addBtn(QString::fromUtf8("应用"), 0);
    } else {
        const bool hasIn = !mLayerPreset || mLayerPreset->bake;
        const bool hasOut = mTextPreset ? mTextPreset->supportsOut :
                            mLayerPreset ? mLayerPreset->outBake != nullptr
                                         : false;
        const bool hasAll = hasIn && hasOut;
        if (hasIn) { addBtn(QStringLiteral("in"), 0); }
        if (hasAll) { addBtn(QStringLiteral("all"), 2); }
        if (hasOut) { addBtn(QStringLiteral("out"), 1); }
    }

    // clicking the preview or the name selects the preset for the
    // big preview
    mPreviewArea->installEventFilter(this);
    mNameLabel->installEventFilter(this);
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
    if (!mPreviewArea) { return QSize(180, 230); }
    return QSize(mPreviewArea->width() + 8,
                 mPreviewArea->height() + 30);
}

void TextAnimTile::paintEvent(QPaintEvent* const e)
{
    Q_UNUSED(e)
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    if (!mPreviewArea) { return; }
    // selection highlight / hover hint around the preview frame
    QPainterPath frame;
    frame.addRoundedRect(QRectF(mPreviewArea->geometry())
                         .adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);
    QColor border(70, 70, 76);
    if (mHover) { border = QColor(110, 110, 120); }
    if (mChecked) { border = ThemeSupport::getThemeColorBlue(); }
    QPen pen(border, mChecked ? 2.0 : 1.2);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawPath(frame);
}

void TextAnimTile::enterEvent(QEvent* const e)
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

void TextAnimTile::resizeEvent(QResizeEvent* const e)
{
    QWidget::resizeEvent(e);
    repositionButtons();
}

void TextAnimTile::repositionButtons()
{
    if (mApplyButtons.isEmpty() || !mPreviewArea) { return; }
    const int btnW = 40;
    const int btnH = 26;
    const int gap = 4;
    const int n = mApplyButtons.count();
    const int totalW = n*btnW + (n - 1)*gap;
    // the buttons are children of the preview area: coordinates
    // are relative to IT, not to the tile
    const int x0 = (mPreviewArea->width() - totalW)/2;
    const int y = mPreviewArea->height() - btnH - 5;
    for (int i = 0; i < n; i++) {
        mApplyButtons.at(i)->setGeometry(
                    x0 + i*(btnW + gap), y, btnW, btnH);
    }
    for (const auto btn : mApplyButtons) { btn->raise(); }
}

void TextAnimTile::setPreviewSize(const int size)
{
    if (mPreviewArea) {
        mPreviewArea->setFixedSize(size, size);
    }
    repositionButtons();
}

void TextAnimTile::setFrames(const QList<QImage>& frames)
{
    mFrames = frames;
    mFrame = 0;
    paintFrame();
}

void TextAnimTile::advance()
{
    if (mFrames.count() > 1) {
        mFrame = (mFrame + 1) % mFrames.count();
        paintFrame();
    }
}

void TextAnimTile::paintFrame()
{
    if (!mPreviewArea) { return; }
    mPreviewArea->setFrames(
                QList<QImage>() << mFrames.value(
                    mFrames.isEmpty() ? 0 : mFrame % mFrames.count()));
}

// ---------------------------------------------------------------- panel

TextAnimPresetPanel::TextAnimPresetPanel(Document& doc,
                                         QWidget* const parent)
    : QWidget(parent)
    , mDocument(doc)
{
    setMinimumSize(360, 440);
    // opaque panel background (no see-through)
    setAutoFillBackground(true);
    setPalette(ThemeSupport::getDarkPalette());

    const auto rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(6, 6, 6, 6);
    rootLayout->setSpacing(4);

    // collapsible preset sections
    mScroll = new QScrollArea(this);
    mScroll->setWidgetResizable(true);
    mScroll->setFrameShape(QFrame::NoFrame);
    mScroll->setAutoFillBackground(true);
    mScroll->viewport()->setAutoFillBackground(true);
    mScrollHost = new QWidget(mScroll);
    mScrollHost->setAutoFillBackground(true);
    const auto hostLayout = new QVBoxLayout(mScrollHost);
    hostLayout->setContentsMargins(0, 0, 0, 0);
    hostLayout->setSpacing(4);

    const auto makeSection = [&](Section& section,
                                 const QString& title) {
        section.header = new QPushButton(
                    QString::fromUtf8("▸ ") + title, this);
        section.header->setCheckable(false);
        section.header->setFlat(true);
        QFont hf = section.header->font();
        hf.setPixelSize(13);
        hf.setBold(true);
        section.header->setFont(hf);
        section.header->setCursor(Qt::PointingHandCursor);
        hostLayout->addWidget(section.header);

        section.body = new QWidget(mScrollHost);
        section.flow = new FlowLayout(section.body);
        hostLayout->addWidget(section.body);

        connect(section.header, &QPushButton::clicked,
                this, [this, &section, title]() {
            section.expanded = !section.expanded;
            section.header->setText(QString::fromUtf8(
                        section.expanded ? "▾ " : "▸ ") + title);
            section.body->setVisible(section.expanded);
            if (section.expanded && !section.built) {
                buildSection(section,
                             &section == &mTextSection ? 0 :
                             &section == &mImageSection ? 1 : 2);
            }
        });
    };
    makeSection(mTextSection, QString::fromUtf8("文字动画"));
    makeSection(mImageSection, QString::fromUtf8("图像动画"));
    makeSection(mLoopSection, QString::fromUtf8("循环动画"));
    hostLayout->addStretch(1);
    mScroll->setWidget(mScrollHost);
    rootLayout->addWidget(mScroll, 1);

    // first section open by default
    mTextSection.expanded = true;
    mTextSection.header->setText(
                QString::fromUtf8("▾ 文字动画"));

    // big preview
    mPreview = new TextAnimPreview(this);
    mPreview->setMinimumHeight(150);
    rootLayout->addWidget(mPreview);

    mStatusLabel = new QLabel(this);
    {
        QFont f = mStatusLabel->font();
        f.setPixelSize(11);
        mStatusLabel->setFont(f);
    }
    mStatusLabel->setWordWrap(true);
    mStatusLabel->setText(QString::fromUtf8("选择文字或图层后点入点/出点应用。"));
    rootLayout->addWidget(mStatusLabel);

    // both sliders side by side at the very bottom, no labels
    // (tooltips carry their meaning)
    const auto slidersRow = new QHBoxLayout();
    slidersRow->setSpacing(6);
    mDurationSlider = new QSlider(Qt::Horizontal, this);
    mDurationSlider->setRange(50, 300);
    mDurationSlider->setSingleStep(10);
    mDurationSlider->setValue(100);
    mDurationSlider->setMaximumHeight(18);
    mDurationSlider->setToolTip(QString::fromUtf8("动画时长缩放（%1%）"));
    slidersRow->addWidget(mDurationSlider, 1);
    mTileSizeSlider = new QSlider(Qt::Horizontal, this);
    mTileSizeSlider->setRange(90, 220);
    mTileSizeSlider->setSingleStep(10);
    mTileSizeSlider->setValue(mTileSize);
    mTileSizeSlider->setMaximumHeight(18);
    mTileSizeSlider->setToolTip(QString::fromUtf8("预览缩放"));
    slidersRow->addWidget(mTileSizeSlider, 1);
    rootLayout->addLayout(slidersRow);
    connect(mTileSizeSlider, &QSlider::valueChanged, this, [this](const int v) {
        mTileSize = v;
        for (const auto tile : mTiles) {
            tile->setPreviewSize(v);
            tile->updateGeometry();
        }
        // reflow: column count adapts to the new tile size
        for (auto section : { &mTextSection, &mImageSection, &mLoopSection }) {
            if (section->flow) {
                section->flow->invalidate();
                section->body->adjustSize();
            }
        }
    });

    connect(mDurationSlider, &QSlider::valueChanged, this, [this](const int v) {
        mDurationScale = v/100.;
    });
    connect(mDurationSlider, &QSlider::sliderReleased, this, [this]() {
        if (mCurrent) { renderBigPreview(); }
    });

    mPlayTimer = new QTimer(this);
    mPlayTimer->setInterval(40);
    connect(mPlayTimer, &QTimer::timeout, this, [this]() {
        for (const auto tile : mTiles) { tile->advance(); }
        mPreview->advance();
    });

    // build the default-open section lazily on first show
}

void TextAnimPresetPanel::buildSection(Section& section, const int kind)
{
    section.built = true;
    fillGridFromKind(kind, section.flow);
}

void TextAnimPresetPanel::fillGridFromKind(const int kind,
                                           FlowLayout* flow)
{
    int rendered = 0;
    const auto addTile = [&](const TextAnimPreset* const tp,
                             const LayerAnimPreset* const lp) {
        const auto tile = new TextAnimTile(tp, lp, this);
        connect(tile, &TextAnimTile::previewClicked,
                this, [this](TextAnimTile* t) { selectTile(t); });
        connect(tile, &TextAnimTile::applyRequested,
                this, [this](TextAnimTile* t, const int dir) {
            applyPreset(t, dir);
        });
        tile->setPreviewSize(mTileSize);
        flow->addWidget(tile);
        mTiles << tile;
        ensureTileFrames(tile);
        if (++rendered % 4 == 0) { QCoreApplication::processEvents(); }
    };
    if (kind == 0) {
        for (const auto& p : TextAnimPresets::all()) {
            if (p.category == 0) { addTile(&p, nullptr); }
        }
    } else if (kind == 1) {
        for (const auto& p : LayerAnimPresets::all()) {
            if (p.category == 0) { addTile(nullptr, &p); }
        }
    } else {
        for (const auto& p : TextAnimPresets::all()) {
            if (p.category == 2) { addTile(&p, nullptr); }
        }
        for (const auto& p : LayerAnimPresets::all()) {
            if (p.category == 2) { addTile(nullptr, &p); }
        }
    }
}

void TextAnimPresetPanel::ensureTileFrames(TextAnimTile* const tile)
{
    if (!tile) { return; }
    if (const auto preset = tile->textPreset()) {
        const auto box = enve::make_shared<TextBox>();
        configureSampleBox(box.get(), QStringLiteral("friction"),
                           defaultCjkFamily(), SkFontStyle(), 64);
        TextAnimPresets::apply(box.get(), *preset, 0, gPreviewFps, 1.0);
        const auto frames = framesForPreset(*preset, 1.0);
        const auto imgs = TextAnimPresets::renderPreviewSequence(
                    box.get(), frames, QSize(160, 160));
        tile->setFrames(imgs);
    } else if (const auto preset = tile->layerPreset()) {
        // presets without an entrance recipe (sink) preview their
        // exit direction instead
        const bool previewOut = preset->bake == nullptr;
        const auto box = enve::make_shared<RectangleBox>();
        configureSampleRect(box.get(), 55, 55);
        LayerAnimPresets::apply(box.get(), *preset, 0, gPreviewFps,
                                1.0, 400, 400, previewOut);
        const auto frames = framesForLayerPreset(*preset, 1.0);
        const auto imgs = LayerAnimPresets::renderPreviewSequence(
                    box.get(), frames, QSize(160, 160), mascotImage());
        tile->setFrames(imgs);
    }
}

void TextAnimPresetPanel::selectTile(TextAnimTile* const tile)
{
    if (!tile) { return; }
    mCurrent = tile;
    for (const auto t : mTiles) {
        t->setChecked(t == tile);
    }
    renderBigPreview();
}

void TextAnimPresetPanel::renderBigPreview()
{
    if (!mCurrent) { return; }
    if (const auto preset = mCurrent->textPreset()) {
        QString text = QStringLiteral("friction");
        QString family = defaultCjkFamily();
        SkFontStyle style;
        qreal fontSize = 72;
        const auto tb = selectedTextBox();
        if (tb) {
            text = tb->getCurrentValue();
            family = tb->getFontFamily();
            style = tb->getFontStyle();
            fontSize = qBound<qreal>(24, tb->getFontSize(), 200);
        }
        const auto box = enve::make_shared<TextBox>();
        configureSampleBox(box.get(), text, family, style, fontSize);
        TextAnimPresets::apply(box.get(), *preset, 0, gPreviewFps,
                               mDurationScale);
        const auto frames = framesForPreset(*preset, mDurationScale);
        const auto imgs = TextAnimPresets::renderPreviewSequence(
                    box.get(), frames, QSize(480, 260));
        if (imgs.isEmpty()) {
            mPreview->setPlaceholder(QString::fromUtf8("（空文本）"));
        } else {
            mPreview->setFrames(imgs);
        }
    } else if (const auto preset = mCurrent->layerPreset()) {
        const auto box = enve::make_shared<RectangleBox>();
        configureSampleRect(box.get(), 130, 130);
        LayerAnimPresets::apply(box.get(), *preset, 0, gPreviewFps,
                                mDurationScale, 1280, 720);
        const auto frames = framesForLayerPreset(*preset, mDurationScale);
        const auto imgs = LayerAnimPresets::renderPreviewSequence(
                    box.get(), frames, QSize(480, 260), mascotImage());
        if (imgs.isEmpty()) {
            mPreview->setPlaceholder(QString::fromUtf8("（无内容）"));
        } else {
            mPreview->setFrames(imgs);
        }
    }
}

void TextAnimPresetPanel::applyPreset(TextAnimTile* const tile,
                                      const int dir)
{
    if (!tile) { return; }
    const auto scene = mDocument.fActiveScene.data();
    if (!scene) {
        mStatusLabel->setText(QString::fromUtf8("没有活动场景。"));
        return;
    }
    const int startFrame = scene->getCurrentFrame();
    const qreal fps = scene->getFps();
    // "all" applies the entrance at the playhead and the exit after
    // a short hold
    const int holdF = qRound(0.5*fps);

    if (const auto preset = tile->layerPreset()) {
        const auto selected = scene->getSelectedBoxesList();
        if (selected.isEmpty()) {
            mStatusLabel->setText(QString::fromUtf8("请先选择图层。"));
            return;
        }
        const qreal cw = scene->getCanvasWidth();
        const qreal ch = scene->getCanvasHeight();
        if (dir == 1 && !preset->outBake) {
            mStatusLabel->setText(QString::fromUtf8("该预设不支持出点。"));
            return;
        }
        if (dir == 0 && !preset->bake) {
            mStatusLabel->setText(QString::fromUtf8("该预设不支持入点。"));
            return;
        }
        int applied = 0;
        for (const auto& box : selected) {
            const int durF = qMax(2, qRound(preset->duration*
                                            mDurationScale*fps));
            if (dir == 0 || dir == 2) {
                LayerAnimPresets::apply(box, *preset, startFrame,
                                        fps, mDurationScale, cw, ch,
                                        false);
            }
            if (dir == 1 || dir == 2) {
                const int outStart = dir == 2 ?
                            startFrame + durF + holdF : startFrame;
                LayerAnimPresets::apply(box, *preset, outStart,
                                        fps, mDurationScale, cw, ch,
                                        true);
            }
            applied++;
        }
        const QString dirText = dir == 0 ? QString::fromUtf8("（入点）") :
                dir == 1 ? QString::fromUtf8("（出点）") :
                           QString::fromUtf8("（入+出）");
        mStatusLabel->setText(
                    QString::fromUtf8("已应用「%1%2」到 %3 个图层。")
                    .arg(preset->name).arg(dirText).arg(applied));
        return;
    }

    const auto preset = tile->textPreset();
    if (!preset) { return; }
    if (dir == 1 && !preset->supportsOut) {
        mStatusLabel->setText(QString::fromUtf8("该预设仅支持入点。"));
        return;
    }
    const auto targets = selectedTextBoxes();
    if (targets.isEmpty()) {
        mStatusLabel->setText(QString::fromUtf8("请先选择文字图层。"));
        return;
    }
    int applied = 0;
    for (const auto tb : targets) {
        const int durF = qMax(2, qRound(preset->duration*
                                        mDurationScale*fps));
        bool ok = true;
        if (dir == 0 || dir == 2) {
            ok = TextAnimPresets::apply(tb, *preset, startFrame,
                                        fps, mDurationScale, false);
        }
        if (ok && (dir == 1 || dir == 2)) {
            const int outStart = dir == 2 ?
                        startFrame + durF + holdF : startFrame;
            ok = TextAnimPresets::apply(tb, *preset, outStart,
                                        fps, mDurationScale, true);
        }
        if (ok) { applied++; }
    }
    if (applied == 0) {
        mStatusLabel->setText(QString::fromUtf8(
            "数字滚动需要文本包含数字（如“进度 42%”）。"));
    } else {
        const QString dirText = dir == 0 ? QString() :
                dir == 1 ? QString::fromUtf8("（出点）") :
                           QString::fromUtf8("（入+出）");
        mStatusLabel->setText(
                    QString::fromUtf8("已应用「%1%2」到 %3 个文字层。")
                    .arg(preset->name).arg(dirText).arg(applied));
    }
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

TextBox* TextAnimPresetPanel::selectedTextBox() const
{
    const auto list = selectedTextBoxes();
    return list.isEmpty() ? nullptr : list.first();
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
    if (mTextSection.expanded && !mTextSection.built) {
        buildSection(mTextSection, 0);
        if (!mTiles.isEmpty()) { selectTile(mTiles.first()); }
    }
    mPlayTimer->start();
}

void TextAnimPresetPanel::hideEvent(QHideEvent* const e)
{
    QWidget::hideEvent(e);
    mPlayTimer->stop();
}

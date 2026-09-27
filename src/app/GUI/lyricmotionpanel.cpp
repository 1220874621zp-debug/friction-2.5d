#include "lyricmotionpanel.h"

#include "lyricmotionengine.h"
#include "lyricmotionpreview.h"

#include "appsupport.h"
#include "canvas.h"
#include "Private/document.h"
#include "Scripting/jsapi.h"
#include "Boxes/textbox.h"
#include "Boxes/containerbox.h"
#include "Animators/qrealanimator.h"
#include "Animators/qstringanimator.h"
#include "Animators/transformanimator.h"
#include "widgets/flowlayout.h"
#include "themesupport.h"

#include "include/core/SkFont.h"
#include "include/core/SkTypeface.h"

#include <QComboBox>
#include <QFontMetricsF>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRandomGenerator>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <cmath>

// self-drawn style card: three swatch bars (bg / fg / accent of the
// style's first scheme) + display name; upgrades to a rendered
// preview in phase 2 without changing the surrounding layout
class LyricStyleCard : public QWidget {
public:
    LyricStyleCard(const LyricMotionEngine::StyleInfo &info,
                   QWidget * const parent) :
        QWidget(parent), mInfo(info) {
        setFixedSize(112, 92);
        setToolTip(info.name);
        setCursor(Qt::PointingHandCursor);
    }

    const QString &key() const { return mInfo.key; }

    void setSelected(const bool selected) {
        if (mSelected == selected) { return; }
        mSelected = selected;
        update();
    }

    void setFrames(const QVector<QImage> &frames) {
        mFrames = frames;
        mFrameIdx = 0;
        mPending = false;
        update();
    }

    bool hasFrames() const { return !mFrames.isEmpty(); }
    bool isPending() const { return mPending; }
    void setPending(const bool pending) { mPending = pending; }

    void advance() {
        if (mFrames.size() < 2) { return; }
        mFrameIdx = (mFrameIdx + 1) % mFrames.size();
        update();
    }

    std::function<void()> onClicked;

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        const QRectF r = rect().adjusted(1, 1, -1, -1);
        const QRectF preview = r.adjusted(0, 0, 0, -18);
        if (!mFrames.isEmpty()) {
            // real JIZURA-rendered frame
            const int idx = qBound(0, mFrameIdx, mFrames.size() - 1);
            p.setPen(Qt::NoPen);
            p.setBrush(Qt::black);
            p.drawRoundedRect(preview, 6, 6);
            p.save();
            QPainterPath clip;
            clip.addRoundedRect(preview, 6, 6);
            p.setClipPath(clip);
            const QImage &frame = mFrames[idx];
            const QImage scaled = frame.scaled(
                        int(preview.width()), int(preview.height()),
                        Qt::KeepAspectRatioByExpanding,
                        Qt::SmoothTransformation);
            p.drawImage(preview.topLeft(), scaled);
            p.restore();
        } else {
            // swatch fallback until the background render arrives
            p.setPen(Qt::NoPen);
            p.setBrush(mInfo.bg);
            p.drawRoundedRect(preview, 6, 6);
            const qreal barH = qMax(6.0, preview.height() * 0.14);
            p.setBrush(mInfo.fg);
            p.drawRoundedRect(QRectF(preview.left() + 10, preview.top() + 12,
                                     preview.width() * 0.72, barH), 2.5, 2.5);
            p.setBrush(mInfo.accent);
            p.drawRoundedRect(QRectF(preview.left() + 10,
                                     preview.top() + 12 + barH + 5,
                                     preview.width() * 0.5, barH), 2.5, 2.5);
        }
        // name plate
        QFont f = font();
        f.setPointSizeF(8.5);
        p.setFont(f);
        p.setPen(palette().color(QPalette::WindowText));
        const QString name = QFontMetricsF(f).elidedText(
                    mInfo.name, Qt::ElideRight, r.width() - 8);
        p.drawText(QRectF(r.left(), r.bottom() - 20, r.width(), 16),
                   Qt::AlignHCenter | Qt::AlignVCenter, name);
        if (mSelected) {
            QPen pen(ThemeSupport::getThemeColorYellow(), 2);
            p.setPen(pen);
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);
        }
    }
    void mousePressEvent(QMouseEvent *e) override {
        QWidget::mousePressEvent(e);
        if (onClicked) { onClicked(); }
    }
private:
    LyricMotionEngine::StyleInfo mInfo;
    QVector<QImage> mFrames;
    int mFrameIdx = 0;
    bool mPending = false;
    bool mSelected = false;
};


namespace {

QString timeLabel(const qreal t) {
    return QString::number(t, 'f', 2);
}

// font kind → locally available family; JIZURA's Google families are
// not vendored, so we resolve by kind and keep the requested weight
struct LocalFont { QString family; int weight; };
LocalFont localFont(const QJsonObject &fontDef) {
    const QString kind = fontDef.value(QStringLiteral("kind")).toString();
    int weight = fontDef.value(QStringLiteral("weight")).toInt(700);
    weight = qBound(300, weight, 900);
    QString family;
    if (kind == QStringLiteral("mincho") || kind == QStringLiteral("brush")) {
        family = QStringLiteral("Noto Serif CJK JP");
    } else if (kind == QStringLiteral("mono")) {
        family = QStringLiteral("Noto Sans Mono CJK JP");
    } else { // gothic / display / round / hand / pixel
        family = QStringLiteral("Noto Sans CJK JP");
        if (kind == QStringLiteral("display")) { weight = qMax(weight, 900); }
    }
    return {family, weight};
}

// largest font size whose rendered width stays within targetWidth
qreal fitTextSize(const QString &text, const QString &family,
                  const int weight, const qreal targetWidth) {
    const auto typeface = SkTypeface::MakeFromName(
                family.toUtf8().constData(),
                SkFontStyle(weight, SkFontStyle::kNormal_Width,
                            SkFontStyle::kUpright_Slant));
    SkFont font(typeface ? typeface : SkTypeface::MakeDefault(), 100);
    const QString probe = text.left(64);
    const qreal w = font.measureText(probe.utf16(),
                                     probe.size() * sizeof(char16_t),
                                     SkTextEncoding::kUTF16);
    if (w <= 1) { return 48; }
    return 100.0 * targetWidth / w;
}

// layout key → anchor point on the canvas (phase-1 mapping; unknown
// keys land centered like JIZURA's own fallback)
QPointF anchorForLayout(const QString &layout,
                        const qreal cw, const qreal ch) {
    const auto has = [&layout](const char *s) {
        return layout.contains(QLatin1String(s), Qt::CaseInsensitive);
    };
    if (has("lower") || has("tl3rd") || has("foot")) { return {cw*0.5, ch*0.78}; }
    if (has("upper") || has("top")) { return {cw*0.5, ch*0.24}; }
    if (has("bottom")) { return {cw*0.5, ch*0.80}; }
    return {cw*0.5, ch*0.5};
}

struct MovePlan {
    bool animated = false;   // false → plain cut (visibility by durRect)
    QPointF enterOffset;     // added to the anchor at the enter start
    QPointF exitOffset;      // added to the anchor at the exit end
};

// enter/exit key → motion hints; unknown names still fade
MovePlan movePlanFor(const QString &key, const qreal cw, const qreal ch) {
    MovePlan plan;
    if (key == QStringLiteral("cut") || key == QStringLiteral("none")) {
        return plan;
    }
    plan.animated = true;
    const auto has = [&key](const char *s) {
        return key.contains(QLatin1String(s), Qt::CaseInsensitive);
    };
    const qreal dx = cw * 0.07, dy = ch * 0.06;
    if (has("rise") || has("up")) { plan.enterOffset = {0, dy}; plan.exitOffset = {0, -dy}; }
    else if (has("drop") || has("fall") || has("down")) { plan.enterOffset = {0, -dy}; plan.exitOffset = {0, dy}; }
    else if (has("left")) { plan.enterOffset = {dx, 0}; plan.exitOffset = {-dx, 0}; }
    else if (has("right")) { plan.enterOffset = {-dx, 0}; plan.exitOffset = {dx, 0}; }
    else if (has("slide") || has("wipe") || has("swipe")) { plan.enterOffset = {dx, 0}; plan.exitOffset = {-dx, 0}; }
    else if (has("slam") || has("impact")) { plan.enterOffset = {0, -dy*0.6}; }
    return plan;
}

} // namespace

LyricMotionPanel::LyricMotionPanel(QWidget * const parent) :
    QWidget(parent) {
    mEngine = new LyricMotionEngine(this);
    setObjectName(QStringLiteral("LyricMotionPanel"));
    setupUi();
    loadSettings();
    rebuildStyleCards();

    connect(&mReplanTimer, &QTimer::timeout,
            this, &LyricMotionPanel::replanNow);
    mReplanTimer.setSingleShot(true);
    mReplanTimer.setInterval(350);

    // preview pipeline: dedicated thread owns the render engine
    setupPreviewWorker();

    connect(&mFrameTimer, &QTimer::timeout,
            this, &LyricMotionPanel::advancePreviews);
    mFrameTimer.setInterval(40);
}

LyricMotionPanel::~LyricMotionPanel() {
    if (mPreviewThread) {
        mPreviewThread->quit();
        mPreviewThread->wait(3000);
    }
}

void LyricMotionPanel::showEvent(QShowEvent *e) {
    QWidget::showEvent(e);
    pumpPreviewQueue();
    if (isVisible()) { mFrameTimer.start(); }
}

void LyricMotionPanel::hideEvent(QHideEvent *e) {
    QWidget::hideEvent(e);
    mFrameTimer.stop();
}

void LyricMotionPanel::setupPreviewWorker() {
    mPreviewThread = new QThread(this);
    mPreviewWorker = new LyricPreviewWorker(240, 135, 10);
    mPreviewWorker->moveToThread(mPreviewThread);
    connect(mPreviewThread, &QThread::started,
            mPreviewWorker, &LyricPreviewWorker::setup);
    connect(mPreviewWorker, &LyricPreviewWorker::framesReady, this,
            [this](const QString &key, const int generation,
                   const QVector<QImage> &frames) {
        if (generation != mPreviewGeneration) { return; } // stale batch
        for (LyricStyleCard *card : mCards) {
            if (card->key() == key) { card->setFrames(frames); break; }
        }
        pumpPreviewQueue();
    }, Qt::QueuedConnection);
    connect(mPreviewWorker, &LyricPreviewWorker::styleFailed, this,
            [this](const QString &key, const int generation,
                   const QString &) {
        if (generation != mPreviewGeneration) { return; }
        pumpPreviewQueue(); // skip the failed one, keep filling
    }, Qt::QueuedConnection);
    mPreviewThread->start();
}

void LyricMotionPanel::pumpPreviewQueue() {
    if (!isVisible() || !mPreviewWorker) { return; }
    for (LyricStyleCard *card : mCards) {
        if (!card->hasFrames() && !card->isPending()) {
            card->setPending(true);
            mPreviewWorker->renderStyle(card->key(),
                                        static_cast<quint32>(mSeedSpin->value()),
                                        mDensitySlider->value() / 100.0,
                                        mPreviewGeneration);
        }
    }
}

void LyricMotionPanel::advancePreviews() {
    for (LyricStyleCard *card : mCards) { card->advance(); }
}

void LyricMotionPanel::setupUi() {
    auto * const mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(4);

    // lyrics
    mLyricsEdit = new QPlainTextEdit(this);
    mLyricsEdit->setPlaceholderText(tr("在此粘贴歌词（支持 LRC 时间戳、*强调*、行尾 ! キメ、/ 手动切分）"));
    {
        QFont f = mLyricsEdit->font();
        f.setStyleHint(QFont::Monospace);
        mLyricsEdit->setFont(f);
    }
    mLyricsEdit->setMaximumHeight(140);
    mainLayout->addWidget(mLyricsEdit);

    // controls row 1: style / mood / seed
    auto *ctrl1 = new QHBoxLayout();
    ctrl1->setSpacing(4);
    mStyleCombo = new QComboBox(this);
    mMoodCombo = new QComboBox(this);
    mMoodCombo->addItem(tr("氛围：自动"), QString());
    mSeedSpin = new QSpinBox(this);
    mSeedSpin->setRange(0, 9999);
    mSeedDice = new QToolButton(this);
    mSeedDice->setText(tr("🎲"));
    mSeedDice->setToolTip(tr("随机种子"));
    mOmakaseButton = new QToolButton(this);
    mOmakaseButton->setText(tr("おまかせ"));
    mOmakaseButton->setToolTip(tr("一键随机整体方案（JIZURA おまかせ）"));
    ctrl1->addWidget(new QLabel(tr("风格"), this), 0);
    ctrl1->addWidget(mStyleCombo, 1);
    ctrl1->addWidget(new QLabel(tr("氛围"), this), 0);
    ctrl1->addWidget(mMoodCombo, 1);
    ctrl1->addWidget(new QLabel(tr("种子"), this), 0);
    ctrl1->addWidget(mSeedSpin);
    ctrl1->addWidget(mSeedDice);
    ctrl1->addWidget(mOmakaseButton);
    mainLayout->addLayout(ctrl1);

    // controls row 2: density / bpm
    auto *ctrl2 = new QHBoxLayout();
    ctrl2->setSpacing(4);
    mDensitySlider = new QSlider(Qt::Horizontal, this);
    mDensitySlider->setRange(0, 100);
    mDensitySlider->setToolTip(tr("细分：每句切分的粒度"));
    mBpmSpin = new QSpinBox(this);
    mBpmSpin->setRange(0, 300);
    mBpmSpin->setSuffix(QStringLiteral(" BPM"));
    mBpmSpin->setSpecialValueText(tr("BPM 自动/无"));
    ctrl2->addWidget(new QLabel(tr("细分"), this), 0);
    ctrl2->addWidget(mDensitySlider, 1);
    ctrl2->addWidget(mBpmSpin);
    mainLayout->addLayout(ctrl2);

    // style cards
    mGalleryHost = new QWidget(this);
    mCardLayout = new FlowLayout(mGalleryHost, 2, 4, 4);
    mGalleryScroll = new QScrollArea(this);
    mGalleryScroll->setWidgetResizable(true);
    mGalleryScroll->setWidget(mGalleryHost);
    mGalleryScroll->setMinimumHeight(96);
    mGalleryScroll->setFrameShape(QFrame::NoFrame);
    mainLayout->addWidget(mGalleryScroll);

    // cut list
    mCutsTree = new QTreeWidget(this);
    mCutsTree->setHeaderLabels({tr("时间"), tr("文本"), tr("布局"),
                                tr("入场"), tr("退场")});
    mCutsTree->setColumnWidth(0, 90);
    mCutsTree->setAlternatingRowColors(true);
    mainLayout->addWidget(mCutsTree, 1);

    // bottom row
    auto *bottom = new QHBoxLayout();
    mApplyButton = new QPushButton(tr("应用到场景"), this);
    mApplyButton->setToolTip(tr("按当前规划生成文字图层（一个撤销步骤）"));
    mStatus = new QLabel(tr("就绪"), this);
    mStatus->setWordWrap(true);
    bottom->addWidget(mApplyButton);
    bottom->addWidget(mStatus, 1);
    mainLayout->addLayout(bottom);

    // wiring — controls only trigger the debounced replan; settings
    // are persisted once per completed replan, not per keystroke
    connect(mLyricsEdit, &QPlainTextEdit::textChanged,
            this, &LyricMotionPanel::scheduleReplan);
    connect(mStyleCombo, &QComboBox::currentIndexChanged,
            this, [this](int) {
        // card highlight follows the combo (also driven from cards)
        for (LyricStyleCard *card : mCards) {
            card->setSelected(card->toolTip() == mStyleCombo->currentText());
        }
        scheduleReplan();
    });
    connect(mMoodCombo, &QComboBox::currentIndexChanged,
            this, &LyricMotionPanel::scheduleReplan);
    connect(mSeedSpin, qOverload<int>(&QSpinBox::valueChanged),
            this, &LyricMotionPanel::scheduleReplan);
    connect(mSeedDice, &QToolButton::clicked, this, [this]() {
        mSeedSpin->setValue(QRandomGenerator::global()->bounded(10000));
    });
    connect(mOmakaseButton, &QToolButton::clicked, this, [this]() {
        QString err;
        const auto json = mEngine->omakaseJson(collectParams(), &err);
        if (!err.isEmpty()) { setStatus(err, true); return; }
        const auto o = QJsonDocument::fromJson(json.toUtf8()).object();
        const QString style = o.value(QStringLiteral("style")).toString();
        const QString mood = o.value(QStringLiteral("mood")).toString();
        mBuildingUi = true;
        if (!style.isEmpty()) {
            mStyleCombo->setCurrentIndex(mStyleCombo->findData(style));
        }
        const int moodIdx = mMoodCombo->findData(mood);
        mMoodCombo->setCurrentIndex(moodIdx < 0 ? 0 : moodIdx);
        const auto fx = o.value(QStringLiteral("fx")).toObject();
        mDensitySlider->setValue(qRound(
                    fx.value(QStringLiteral("density")).toDouble() * 100));
        mBuildingUi = false;
        if (o.contains(QStringLiteral("seed"))) {
            mSeedSpin->setValue(o.value(QStringLiteral("seed")).toInt());
        } else {
            scheduleReplan();
        }
    });
    connect(mDensitySlider, &QSlider::valueChanged,
            this, &LyricMotionPanel::scheduleReplan);
    connect(mBpmSpin, qOverload<int>(&QSpinBox::valueChanged),
            this, &LyricMotionPanel::scheduleReplan);
    connect(mApplyButton, &QPushButton::clicked,
            this, &LyricMotionPanel::applyToScene);
}

void LyricMotionPanel::loadSettings() {
    mBuildingUi = true;
    mLyricsEdit->setPlainText(AppSupport::getSettings(
        QStringLiteral("LyricPanel"), QStringLiteral("lyrics"),
        QString()).toString());
    mSeedSpin->setValue(AppSupport::getSettings(
        QStringLiteral("LyricPanel"), QStringLiteral("seed"), 1).toInt());
    mDensitySlider->setValue(AppSupport::getSettings(
        QStringLiteral("LyricPanel"), QStringLiteral("density"), 55).toInt());
    mBpmSpin->setValue(AppSupport::getSettings(
        QStringLiteral("LyricPanel"), QStringLiteral("bpm"), 0).toInt());
    const QString style = AppSupport::getSettings(
        QStringLiteral("LyricPanel"), QStringLiteral("style"),
        QStringLiteral("noir")).toString();
    if (mStyleCombo->count() > 0) {
        mStyleCombo->setCurrentIndex(qMax(0, mStyleCombo->findData(style)));
    }
    const QString mood = AppSupport::getSettings(
        QStringLiteral("LyricPanel"), QStringLiteral("mood"),
        QString()).toString();
    if (!mood.isEmpty()) {
        const int idx = mMoodCombo->findData(mood);
        if (idx >= 0) { mMoodCombo->setCurrentIndex(idx); }
    }
    mBuildingUi = false;
}

void LyricMotionPanel::saveSettings() {
    AppSupport::setSettings(QStringLiteral("LyricPanel"),
                            QStringLiteral("lyrics"),
                            mLyricsEdit->toPlainText());
    AppSupport::setSettings(QStringLiteral("LyricPanel"),
                            QStringLiteral("style"),
                            mStyleCombo->currentData().toString());
    AppSupport::setSettings(QStringLiteral("LyricPanel"),
                            QStringLiteral("mood"),
                            mMoodCombo->currentData().toString());
    AppSupport::setSettings(QStringLiteral("LyricPanel"),
                            QStringLiteral("density"),
                            mDensitySlider->value());
    AppSupport::setSettings(QStringLiteral("LyricPanel"),
                            QStringLiteral("bpm"),
                            mBpmSpin->value());
}

LyricMotionEngine::Params LyricMotionPanel::collectParams() const {
    LyricMotionEngine::Params p;
    p.lyrics = mLyricsEdit->toPlainText();
    p.style = mStyleCombo->currentData().toString();
    if (p.style.isEmpty()) { p.style = QStringLiteral("noir"); }
    p.mood = mMoodCombo->currentData().toString();
    p.seed = static_cast<quint32>(mSeedSpin->value());
    p.density = mDensitySlider->value() / 100.0;
    p.bpm = mBpmSpin->value();
    return p;
}

void LyricMotionPanel::scheduleReplan() {
    if (mBuildingUi) { return; }
    mReplanTimer.start();
}

void LyricMotionPanel::rebuildStyleCards() {
    QString err;
    if (!mEngine->ensureLoaded(&err)) {
        setStatus(tr("引擎加载失败: %1").arg(err), true);
        return;
    }
    // combos (first build only)
    mBuildingUi = true;
    if (mStyleCombo->count() == 0) {
        for (const auto &info : mEngine->styles()) {
            mStyleCombo->addItem(info.name, info.key);
        }
    }
    if (mMoodCombo->count() == 1) {
        for (const QString &m : mEngine->moodNames()) {
            const int sep = m.indexOf(QLatin1Char('|'));
            mMoodCombo->addItem(m.mid(sep + 1), m.left(sep));
        }
    }
    mBuildingUi = false;

    // cards
    for (LyricStyleCard *card : mCards) {
        mCardLayout->removeWidget(card);
        card->deleteLater();
    }
    mCards.clear();
    const QString currentStyle = mStyleCombo->currentData().toString();
    for (const auto &info : mEngine->styles()) {
        const auto card = new LyricStyleCard(info, mGalleryHost);
        card->setSelected(info.key == currentStyle);
        card->onClicked = [this, key = info.key]() {
            mBuildingUi = true;
            mStyleCombo->setCurrentIndex(mStyleCombo->findData(key));
            mBuildingUi = false;
            scheduleReplan();
        };
        mCardLayout->addWidget(card);
        mCards << card;
    }
    mPreviewGeneration++; // stale async results are dropped on arrival
    pumpPreviewQueue();
}

void LyricMotionPanel::replanNow() {
    if (!mEngine->isLoaded()) {
        QString err;
        if (!mEngine->ensureLoaded(&err)) {
            setStatus(tr("引擎加载失败: %1").arg(err), true);
            return;
        }
        rebuildStyleCards();
    }
    const auto params = collectParams();
    QString err;
    setStatus(tr("规划中…"));
    const auto json = mEngine->planJson(params, &err);
    if (!err.isEmpty()) { setStatus(tr("规划失败: %1").arg(err), true); return; }
    mPlanJson = json;
    saveSettings();
    populateCuts();
    const int cutCount = QJsonDocument::fromJson(json.toUtf8())
            .object().value(QStringLiteral("plan")).toObject()
            .value(QStringLiteral("cuts")).toArray().size();
    setStatus(tr("规划完成：%1 切 / %2 s").arg(
                  QString::number(cutCount),
                  timeLabel(QJsonDocument::fromJson(json.toUtf8())
                            .object().value(QStringLiteral("plan"))
                            .toObject().value(QStringLiteral("duration"))
                            .toDouble())));
}

void LyricMotionPanel::populateCuts() {
    mCutsTree->clear();
    if (mPlanJson.isEmpty()) { return; }
    const auto doc = QJsonDocument::fromJson(mPlanJson.toUtf8());
    const auto plan = doc.object().value(QStringLiteral("plan")).toObject();
    const auto lines = plan.value(QStringLiteral("lines")).toArray();
    const auto cuts = plan.value(QStringLiteral("cuts")).toArray();

    // group cuts under their line
    QHash<int, QTreeWidgetItem *> lineItems;
    for (const auto &lv : lines) {
        const auto l = lv.toObject();
        const int idx = l.value(QStringLiteral("index")).toInt();
        auto *item = new QTreeWidgetItem(mCutsTree);
        item->setText(0, QStringLiteral("%1–%2s").arg(
                          timeLabel(l.value(QStringLiteral("start")).toDouble()),
                          timeLabel(l.value(QStringLiteral("end")).toDouble())));
        item->setText(1, l.value(QStringLiteral("text")).toString());
        lineItems.insert(idx, item);
    }
    for (const auto &cv : cuts) {
        const auto c = cv.toObject();
        QTreeWidgetItem *parent = lineItems.value(
                    c.value(QStringLiteral("line")).toInt(), nullptr);
        auto *item = parent ? new QTreeWidgetItem(parent)
                            : new QTreeWidgetItem(mCutsTree);
        item->setText(0, QStringLiteral("%1–%2s").arg(
                          timeLabel(c.value(QStringLiteral("start")).toDouble()),
                          timeLabel(c.value(QStringLiteral("end")).toDouble())));
        item->setText(1, c.value(QStringLiteral("text")).toString());
        item->setText(2, c.value(QStringLiteral("layout")).toString());
        item->setText(3, c.value(QStringLiteral("enter")).toString());
        item->setText(4, c.value(QStringLiteral("exit")).toString());
        for (int i = 2; i < 5; i++) {
            item->setForeground(i, QColor(140, 140, 140));
        }
    }
    mCutsTree->expandAll();
}

void LyricMotionPanel::setStatus(const QString &text, const bool error) {
    mStatus->setText(text);
    mStatus->setStyleSheet(error ? QStringLiteral("color:#E06C5A;")
                                 : QString());
}

void LyricMotionPanel::applyToScene() {
    if (mPlanJson.isEmpty()) {
        setStatus(tr("没有可应用的规划"), true);
        return;
    }
    auto * const scene = Document::sInstance ?
                Document::sInstance->fActiveScene : nullptr;
    if (!scene) { setStatus(tr("请先打开一个场景"), true); return; }

    const auto doc = QJsonDocument::fromJson(mPlanJson.toUtf8());
    const auto plan = doc.object().value(QStringLiteral("plan")).toObject();
    const auto cuts = plan.value(QStringLiteral("cuts")).toArray();
    const auto schemes = plan.value(QStringLiteral("style")).toObject()
            .value(QStringLiteral("schemes")).toArray();
    const auto fonts = doc.object().value(QStringLiteral("fonts")).toObject();
    if (cuts.isEmpty() || schemes.isEmpty()) {
        setStatus(tr("规划为空，先重新规划"), true);
        return;
    }

    const qreal fps = scene->getFps();
    const qreal cw = scene->getCanvasWidth();
    const qreal ch = scene->getCanvasHeight();
    const auto schemeAt = [&schemes](const int idx) {
        return schemes.at(((idx % schemes.size()) + schemes.size())
                          % schemes.size()).toObject();
    };

    int created = 0;
    Friction::Core::beginUndoGroupBatch();
    QString error;
    try {
        // replace the previous generation: drop every top-level group
        // named 歌词动画 before laying out the new one
        const QString groupName = tr("歌词动画");
        for (const auto &box : scene->getContainedBoxes()) {
            if (box->getBoxType() == eBoxType::layer &&
                box->prp_getName().startsWith(groupName)) {
                box->setSelected(false);
                box->removeFromParent_k();
            }
        }
        auto group = enve::make_shared<ContainerBox>(eBoxType::layer);
        scene->getCurrentGroup()->addContained(group);
        group->prp_setName(groupName);
        QVector<BoundingBox *> cutLayers(cuts.size(), nullptr);

        for (int i = 0; i < cuts.size(); i++) {
            const auto c = cuts.at(i).toObject();
            const QString text = c.value(QStringLiteral("text")).toString();
            if (text.isEmpty()) { continue; }
            const qreal start = c.value(QStringLiteral("start")).toDouble();
            const qreal end = c.value(QStringLiteral("end")).toDouble();
            if (end - start < 0.05) { continue; }
            const qreal inDur = c.value(QStringLiteral("inDur")).toDouble();
            const qreal outDur = c.value(QStringLiteral("outDur")).toDouble();
            const int schemeIdx = c.value(QStringLiteral("scheme")).toInt();
            const auto scheme = schemeAt(schemeIdx);
            const bool emph = c.value(QStringLiteral("emph")).toDouble() > 0;
            const QString layout = c.value(QStringLiteral("layout")).toString();

            const auto box = enve::make_shared<TextBox>();
            group->addContained(box);
            box->prp_setName(tr("歌词 %1").arg(i + 1));
            box->setCurrentValue(text);

            // font: cut's picked key → local family by kind
            QString family = QStringLiteral("Noto Sans CJK JP");
            int weight = 700;
            const QString fontKey = c.value(QStringLiteral("params"))
                    .toObject().value(QStringLiteral("font")).toString();
            if (!fontKey.isEmpty() && fonts.contains(fontKey)) {
                const auto lf = localFont(fonts.value(fontKey).toObject());
                family = lf.family;
                weight = lf.weight;
            }
            box->setFontFamilyAndStyle(
                        family,
                        SkFontStyle(weight, SkFontStyle::kNormal_Width,
                                    SkFontStyle::kUpright_Slant));
            // size: fit the text into a share of the canvas width
            qreal target = cw * 0.8;
            if (layout.contains(QLatin1String("lower"), Qt::CaseInsensitive) ||
                layout.contains(QLatin1String("tl3rd"), Qt::CaseInsensitive)) {
                target = cw * 0.7;
            }
            box->setFontSize(qBound(12.0,
                                    fitTextSize(text, family, weight, target),
                                    ch * 0.35));
            // color
            const auto fill = box->getFillSettings();
            fill->setPaintType(PaintType::FLATPAINT);
            fill->setCurrentColor(QColor(
                emph ? scheme.value(QStringLiteral("accent")).toString()
                     : scheme.value(QStringLiteral("fg")).toString()));

            // placement + enter/exit animation
            const QPointF anchor = anchorForLayout(layout, cw, ch);
            const MovePlan enterPlan = movePlanFor(
                        c.value(QStringLiteral("enter")).toString(), cw, ch);
            const MovePlan exitPlan = movePlanFor(
                        c.value(QStringLiteral("exit")).toString(), cw, ch);

            auto * const posX = box->getTransformAnimator()
                    ->getPosAnimator()->getXAnimator();
            auto * const posY = box->getTransformAnimator()
                    ->getPosAnimator()->getYAnimator();
            auto * const opaAnim = box->getBoxTransformAnimator()
                    ->getOpacityAnimator();
            posX->setCurrentBaseValue(anchor.x());
            posY->setCurrentBaseValue(anchor.y());
            const int fStart = qFloor(start * fps);
            const int fEnd = qCeil(end * fps);
            const int fIn = qRound(inDur * fps);
            const int fOut = qRound(outDur * fps);
            const int fInEnd = fStart + fIn;
            const int fOutStart = fEnd - fOut;

            // opacity: 0..100 engine range; hard-cut phases get no keys
            // at all so visibility is bounded by the duration rectangle
            const QString enterKey = c.value(QStringLiteral("enter")).toString();
            const bool typewriter = enterKey.contains(
                        QStringLiteral("type"), Qt::CaseInsensitive) && fIn > 0;
            if (typewriter) {
                // reveal the text progressively (text keys, no fade)
                if (auto *textAnim = box->getStringAnimator()) {
                    const int chars = text.length();
                    const int steps = qBound(2, chars, 30);
                    for (int k = 1; k <= steps; k++) {
                        const int frame = fStart + qRound(
                                    qreal(fIn) * k / steps);
                        const QString partial = text.left(
                                    qRound(qreal(chars) * k / steps));
                        textAnim->anim_appendKey(
                                    enve::make_shared<QStringKey>(
                                        partial, frame, textAnim));
                    }
                }
                opaAnim->saveValueToKey(fStart, 100);
            } else if (enterPlan.animated && fIn > 0) {
                posX->saveValueToKey(fStart, anchor.x() + enterPlan.enterOffset.x());
                posY->saveValueToKey(fStart, anchor.y() + enterPlan.enterOffset.y());
                posX->saveValueToKey(fInEnd, anchor.x());
                posY->saveValueToKey(fInEnd, anchor.y());
                opaAnim->saveValueToKey(fStart, 0);
                opaAnim->saveValueToKey(fInEnd, 100);
            } else if (exitPlan.animated && fOut > 0) {
                // anchor for the fade-out interpolation
                opaAnim->saveValueToKey(fStart, 100);
            }
            if (exitPlan.animated && fOut > 0 && fOutStart > fInEnd) {
                posX->saveValueToKey(fOutStart, anchor.x());
                posY->saveValueToKey(fOutStart, anchor.y());
                posX->saveValueToKey(fEnd, anchor.x() + exitPlan.exitOffset.x());
                posY->saveValueToKey(fEnd, anchor.y() + exitPlan.exitOffset.y());
                opaAnim->saveValueToKey(fOutStart, 100);
                opaAnim->saveValueToKey(fEnd, 0);
            }

            // visibility window (1-frame pads so keys are not clipped)
            box->createDurationRectangle();
            if (const auto durRect = box->getDurationRectangle()) {
                durRect->setMinAbsFrame(fStart - 1);
                durRect->setFramesDuration(fEnd - fStart + 2);
            }
            cutLayers[i] = box.get();
            created++;
        }

        // transition pass: the plan chains some cuts with a transition
        // (cut.trans, duration transDur); map it to a crossfade by
        // overlapping the previous layer and fading both
        for (int i = 1; i < cuts.size(); i++) {
            const auto c = cuts.at(i).toObject();
            const QString trans = c.value(QStringLiteral("trans")).toString();
            const qreal transDur = c.value(QStringLiteral("transDur")).toDouble();
            if (trans.isEmpty() || transDur <= 0.01) { continue; }
            auto *prevBox = cutLayers.at(i - 1);
            auto *curBox = cutLayers.at(i);
            if (!prevBox || !curBox) { continue; }
            const qreal start = c.value(QStringLiteral("start")).toDouble();
            const int fStart = qFloor(start * fps);
            const int fTd = qMax(1, qRound(transDur * fps));
            if (const auto dr = prevBox->getDurationRectangle()) {
                const int newMax = qMax(dr->getMaxAbsFrame(), fStart + fTd + 1);
                dr->setFramesDuration(newMax - dr->getMinAbsFrame() + 1);
            }
            auto *prevOpa = prevBox->getBoxTransformAnimator()
                    ->getOpacityAnimator();
            auto *curOpa = curBox->getBoxTransformAnimator()
                    ->getOpacityAnimator();
            prevOpa->saveValueToKey(fStart, 100);
            prevOpa->saveValueToKey(fStart + fTd, 0);
            curOpa->saveValueToKey(fStart, 0);
            curOpa->saveValueToKey(fStart + fTd, 100);
        }

        // extend the scene range to hold the whole lyric
        const qreal duration = plan.value(QStringLiteral("duration")).toDouble();
        const int lastFrame = qCeil(duration * fps) + 1;
        const auto range = scene->getFrameRange();
        if (range.fMax < lastFrame) {
            scene->setFrameRange(FrameRange{range.fMin, lastFrame});
        }
    } catch (const std::exception &e) {
        error = QString::fromUtf8(e.what());
    }
    Friction::Core::endUndoGroupBatch();
    Document::sInstance->actionFinished();
    if (error.isEmpty()) {
        setStatus(tr("已应用到场景：%1 个文字层").arg(created));
    } else {
        setStatus(tr("应用失败: %1").arg(error), true);
    }
}

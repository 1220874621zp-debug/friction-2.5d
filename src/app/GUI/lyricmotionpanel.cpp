#include "lyricmotionpanel.h"

#include "lyricmotionengine.h"
#include "lyricmotionnative.h"
#include "lyricmotionpreview.h"
#include "lyricmotionaudio.h"
#include "Sound/eindependentsound.h"
#include "RasterEffects/rastereffectcollection.h"

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

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QStandardPaths>
#include <QtConcurrent>
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
    mFrameTimer.start();
    // deferred: visibility of dock contents is not settled inside
    // showEvent, so the queue pump runs on the next event loop pass
    QTimer::singleShot(0, this, [this]() { pumpPreviewQueue(); });
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
        int remaining = 0;
        for (LyricStyleCard *card : mCards) {
            if (!card->hasFrames() && card->isPending()) { remaining++; }
        }
        if (remaining > 0) {
            setStatus(tr("风格预览渲染中（剩余 %1）…").arg(remaining));
        }
        pumpPreviewQueue();
    }, Qt::QueuedConnection);
    connect(mPreviewWorker, &LyricPreviewWorker::styleFailed, this,
            [this](const QString &key, const int generation,
                   const QString &error) {
        if (generation != mPreviewGeneration) { return; }
        setStatus(tr("风格预览失败 %1: %2").arg(key, error.left(60)), true);
        pumpPreviewQueue(); // skip the failed one, keep filling
    }, Qt::QueuedConnection);
    connect(mPreviewWorker, &LyricPreviewWorker::cutFrameReady, this,
            [this](const QImage &frame, const int generation) {
        if (generation != mPreviewGeneration) { return; }
        mCutPreview->setPixmap(QPixmap::fromImage(frame));
    }, Qt::QueuedConnection);
    mPreviewThread->start();
}

void LyricMotionPanel::pumpPreviewQueue() {
    if (!isVisible() || !mPreviewWorker) { return; }
    for (LyricStyleCard *card : mCards) {
        if (!card->hasFrames() && !card->isPending()) {
            card->setPending(true);
            const auto params = collectParams();
            mPreviewWorker->renderStyle(card->key(), params.seed,
                                        params.density, params.lyrics,
                                        params.beats, params.audioDuration,
                                        sceneFps(), mPreviewGeneration);
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
    mChromaSlider = new QSlider(Qt::Horizontal, this);
    mChromaSlider->setRange(0, 100);
    mChromaSlider->setValue(70);
    mChromaSlider->setToolTip(tr("色差强度（幽灵三 pass 映射）"));
    mBpmSpin = new QSpinBox(this);
    mBpmSpin->setRange(0, 300);
    mBpmSpin->setSuffix(QStringLiteral(" BPM"));
    mBpmSpin->setSpecialValueText(tr("BPM 自动/无"));
    ctrl2->addWidget(new QLabel(tr("细分"), this), 0);
    ctrl2->addWidget(mDensitySlider, 0);
    ctrl2->addWidget(new QLabel(tr("色差"), this), 0);
    ctrl2->addWidget(mChromaSlider, 0);
    ctrl2->addWidget(mBpmSpin);
    mainLayout->addLayout(ctrl2);

    // audio row
    auto *audioRow = new QHBoxLayout();
    audioRow->setSpacing(4);
    mAudioButton = new QPushButton(tr("载入音频"), this);
    mAudioButton->setToolTip(tr("分析 BPM 并对齐切边界（JIZURA 检测算法）"));
    mAudioLabel = new QLabel(tr("未载入"), this);
    mIncludeAudio = new QCheckBox(tr("应用时含音频层"), this);
    audioRow->addWidget(mAudioButton);
    audioRow->addWidget(mAudioLabel, 1);
    audioRow->addWidget(mIncludeAudio);
    mainLayout->addLayout(audioRow);

    // style cards
    mGalleryHost = new QWidget(this);
    mCardLayout = new FlowLayout(mGalleryHost, 2, 4, 4);
    mGalleryScroll = new QScrollArea(this);
    mGalleryScroll->setWidgetResizable(true);
    mGalleryScroll->setWidget(mGalleryHost);
    mGalleryScroll->setMinimumHeight(96);
    mGalleryScroll->setFrameShape(QFrame::NoFrame);
    mainLayout->addWidget(mGalleryScroll);

    // cut-level large preview (click a cut row to render it)
    mCutPreview = new QLabel(this);
    mCutPreview->setMinimumHeight(120);
    mCutPreview->setMaximumHeight(160);
    mCutPreview->setAlignment(Qt::AlignCenter);
    mCutPreview->setStyleSheet(QStringLiteral("background:#0a0a0a; color:#666;"));
    mCutPreview->setText(tr("点击切行查看该切大图"));
    mCutPreview->setScaledContents(true);
    mainLayout->addWidget(mCutPreview);

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
    mApplyButton->setToolTip(tr("原生构建：把规划落成可编辑的文字层/特效/关键帧（一个撤销步骤）"));
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
    connect(mChromaSlider, &QSlider::valueChanged,
            this, &LyricMotionPanel::scheduleReplan);
    connect(mBpmSpin, qOverload<int>(&QSpinBox::valueChanged),
            this, &LyricMotionPanel::scheduleReplan);
    connect(mAudioButton, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(
                    this, tr("载入音频"), QString(),
                    tr("音频文件 (*.mp3 *.wav *.ogg *.flac *.m4a *.aac)"));
        if (path.isEmpty()) { return; }
        mAudioLabel->setText(tr("分析中…"));
        mAudioButton->setEnabled(false);
        QtConcurrent::run([this, path]() {
            LyricAudioAnalysis analysis;
            QString err;
            const bool ok = LyricAudioAnalyzer::analyze(path, analysis, &err);
            QMetaObject::invokeMethod(this, [this, path, analysis, err, ok]() {
                mAudioButton->setEnabled(true);
                if (!ok) {
                    mAudioLabel->setText(err);
                    return;
                }
                mAudioPath = path;
                mAudioAnalysis = analysis;
                mAudioLabel->setText(QStringLiteral("%1 · BPM %2 · %3 拍")
                    .arg(QFileInfo(path).fileName(),
                         QString::number(analysis.bpm, 'f', 1),
                         QString::number(analysis.beats.size())));
                scheduleReplan();
            }, Qt::QueuedConnection);
        });
    });
    connect(mApplyButton, &QPushButton::clicked,
            this, &LyricMotionPanel::applyToScene);
    connect(mCutsTree, &QTreeWidget::itemClicked, this,
            [this](QTreeWidgetItem *item, int) {
        const QVariant idxVar = item->data(0, Qt::UserRole);
        if (!idxVar.isValid()) { return; } // line rows carry no cut data
        const QVariant tVar = item->data(0, Qt::UserRole + 1);
        if (!tVar.isValid() || !mPreviewWorker) { return; }
        const auto params = collectParams();
        mPreviewWorker->renderCutFrame(
                    params.style, params.seed, params.density,
                    params.lyrics, params.beats, params.audioDuration,
                    sceneFps(), tVar.toDouble(), 480, 270,
                    mPreviewGeneration);
    });
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
    mChromaSlider->setValue(AppSupport::getSettings(
        QStringLiteral("LyricPanel"), QStringLiteral("chroma"), 70).toInt());
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
                            QStringLiteral("chroma"),
                            mChromaSlider->value());
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
    p.chroma = mChromaSlider->value() / 100.0;
    p.bpm = mBpmSpin->value();
    // JIZURA semantics: a manual BPM grid overrides detected beats;
    // detected beats apply while the spin stays on 自动/无
    if (!mAudioPath.isEmpty() && mAudioAnalysis.valid && p.bpm == 0) {
        p.beats = mAudioAnalysis.beats;
        p.audioDuration = mAudioAnalysis.duration;
    }
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
    int j = 0;
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
        item->setData(0, Qt::UserRole, j++);
        item->setData(0, Qt::UserRole + 1,
                      (c.value(QStringLiteral("start")).toDouble()
                       + c.value(QStringLiteral("end")).toDouble()) * 0.5);
    }
    mCutsTree->expandAll();
}

void LyricMotionPanel::setStatus(const QString &text, const bool error) {
    mStatus->setText(text);
    mStatus->setStyleSheet(error ? QStringLiteral("color:#E06C5A;")
                                 : QString());
}

qreal LyricMotionPanel::sceneFps() const {
    const auto * const scene = Document::sInstance
                ? Document::sInstance->fActiveScene : nullptr;
    return scene ? scene->getFps() : 24.0;
}

void LyricMotionPanel::applyToScene() {
    if (mPlanJson.isEmpty()) {
        setStatus(tr("没有可应用的规划"), true);
        return;
    }
    if (mApplying) { return; }
    auto * const scene = Document::sInstance ?
                Document::sInstance->fActiveScene : nullptr;
    if (!scene) { setStatus(tr("请先打开一个场景"), true); return; }

    const auto doc = QJsonDocument::fromJson(mPlanJson.toUtf8());
    const auto plan = doc.object().value(QStringLiteral("plan")).toObject();
    const auto fonts = doc.object().value(QStringLiteral("fonts")).toObject();
    if (plan.value(QStringLiteral("cuts")).toArray().isEmpty()) {
        setStatus(tr("规划为空，先重新规划"), true);
        return;
    }

    mApplying = true;
    mApplyButton->setEnabled(false);
    setStatus(tr("原生构建中…"));
    // audio dedupe: only hand the path over when the layer is new
    const QString newAudio = (mIncludeAudio->isChecked()
                              && !mAudioPath.isEmpty()
                              && mAudioPath != mAppliedAudioPath)
            ? mAudioPath : QString();
    LyricMotionNative::Result result;
    QString error;
    const bool ok = LyricMotionNative::build(scene, plan, fonts, newAudio,
                mIncludeAudio->isChecked(), &result, &error);
    if (ok && !newAudio.isEmpty()) { mAppliedAudioPath = newAudio; }
    mApplying = false;
    mApplyButton->setEnabled(true);
    if (!ok) {
        setStatus(tr("应用失败: %1").arg(error), true);
        return;
    }
    QString extra;
    const int maxNotes = qMin(3, result.notes.size());
    for (int i = 0; i < maxNotes; i++) {
        extra += (i == 0 ? QStringLiteral("；") : QStringLiteral("，"))
                + result.notes.at(i);
    }
    setStatus(tr("已应用到场景：%1 切 · 原生构建（可编辑）%2")
              .arg(result.cutsBuilt).arg(extra));
}

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
#include <QRegularExpression>
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
        // hover-gated: the animation plays only while the pointer is
        // on this card (all cards looping together was noisy)
        if (mFrames.size() < 2 || !mHovered) { return; }
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
    void enterEvent(QEnterEvent *e) override {
        QWidget::enterEvent(e);
        mHovered = true;
        mFrameIdx = 0; // restart the loop on hover
        update();
        // render on demand: hovering a swatch card asks the worker for
        // its frames (each style needs its own QJSEngine rebuild, so
        // eager batch rendering is far too expensive)
        if (mFrames.isEmpty() && !mPending && onHovered) { onHovered(); }
    }
    void leaveEvent(QEvent *e) override {
        QWidget::leaveEvent(e);
        mHovered = false;
        update();
    }
private:
    LyricMotionEngine::StyleInfo mInfo;
    QVector<QImage> mFrames;
    int mFrameIdx = 0;
    bool mPending = false;
    bool mSelected = false;
    bool mHovered = false;
public:
    std::function<void()> onHovered;
};


namespace {

QString timeLabel(const qreal t) {
    return QString::number(t, 'f', 2);
}

// Chinese names for the part keys the panel surfaces (cut list).
// Covers the core families plus common additions; anything else falls
// back to the engine's registered name, then to the raw key.
QHash<QString, QString> makePartZhTable() {
    QHash<QString, QString> t;
    const auto add = [&t](const char *group, std::initializer_list<
                          std::pair<const char*, const char*>> entries) {
        for (const auto &e : entries) {
            t.insert(QStringLiteral("%1/%2").arg(
                         QString::fromUtf8(group),
                         QString::fromUtf8(e.first)),
                     QString::fromUtf8(e.second));
        }
    };
    add("layout", {
        {"center", "中央"}, {"mixed", "大小混排"}, {"vcols", "纵排分栏"},
        {"marquee", "流动条带"}, {"tile", "平铺磁贴"}, {"scatter", "散布"},
        {"ring", "圆环排布"}, {"wave", "波形轨迹"}, {"huge", "超大字"},
        {"labels", "标签贴纸"}, {"condensed", "纵长压缩"},
        {"gloss", "光泽标题"}, {"type", "打字机"}, {"diag", "斜向条带"},
        {"circle", "圆窗"}, {"stack", "残像堆叠"}, {"pill", "胶囊框"},
        {"title", "标题"}, {"interlude", "间奏"},
    });
    add("enter", {
        {"cut", "硬切"}, {"assemble", "分解→集合"}, {"slice", "切片"},
        {"type", "打字"}, {"pop", "弹出"}, {"drop", "坠落"},
        {"stretch", "伸缩"}, {"wipe", "擦除"}, {"blur", "模糊显影"},
        {"spin", "旋转"}, {"flicker", "闪烁"}, {"scramble", "乱码解码"},
        {"zoom", "缩放"}, {"slideL", "左滑入"}, {"slideR", "右滑入"},
        {"flipX", "左右翻转"}, {"flipY", "上下翻转"},
        {"domino", "多米诺"}, {"fold", "折叠展开"}, {"unroll", "卷轴展开"},
        {"iris", "光圈"}, {"blinds", "百叶窗"}, {"bounce", "弹跳"},
        {"squashDrop", "压扁落地"}, {"rubber", "橡皮筋"},
        {"whip", "甩鞭"}, {"echoIn", "残影进入"}, {"spiralIn", "螺旋进入"},
        {"magnet", "磁吸"}, {"inkBleed", "墨晕"}, {"neonOn", "霓虹点亮"},
        {"cursorSweep", "光标扫入"}, {"stamp", "盖章"},
    });
    add("hold", {
        {"still", "静止"}, {"jitter", "抖动"}, {"drift", "漂移"},
        {"breathe", "呼吸"}, {"wave", "波动"}, {"glitchtick", "故障闪烁"},
    });
    add("exit", {
        {"cut", "硬切"}, {"explode", "爆散"}, {"fall", "坠落"},
        {"drift", "漂移"}, {"slice", "切片"}, {"wipe", "擦除"},
        {"shrink", "收缩"}, {"blur", "模糊"}, {"stretch", "伸缩"},
        {"scatter", "飞散"}, {"glitch", "故障"},
    });
    add("decor", {
        {"grid", "网格"}, {"stripes", "条纹"}, {"blobs", "色斑"},
        {"bars", "粗条"}, {"shapes", "几何形"}, {"counter", "计数器"},
        {"brackets", "括角标"}, {"rings", "圆环"}, {"dots", "点阵"},
        {"arrows", "箭头"}, {"slash", "斜杠"}, {"sparks", "火花"},
        {"leaders", "引出线"}, {"waveform", "波形"}, {"barcode", "条形码"},
    });
    add("cam", {{"push", "缓推近"}, {"none", "无"}});
    add("trans", {
        {"push", "推移"}, {"cover", "覆盖"}, {"wipe", "擦除"},
        {"zoom", "变焦"}, {"mosaic", "马赛克"}, {"flash", "闪白"},
        {"morph", "形变"}, {"none", "无"},
    });
    return t;
}

// part key → localized display name: Chinese table → engine name when
// it is kanji/ASCII readable (kana names are skipped) → raw key
QString partZh(const QString &group, const QString &key,
               const LyricMotionEngine * const engine) {
    static const auto zhTable = makePartZhTable();
    const auto it = zhTable.constFind(
                QStringLiteral("%1/%2").arg(group, key));
    if (it != zhTable.constEnd()) { return *it; }
    const QString regName = engine ? engine->partName(group, key) : key;
    if (regName != key) {
        static const QRegularExpression kana(
                    QStringLiteral("[\\x{3040}-\\x{30ff}]"));
        if (!kana.match(regName).hasMatch()) { return regName; }
    }
    return key;
}

// style keys → Chinese (JIZURA names are Japanese; kanji-only names
// like 深海 already read fine in Chinese and are kept via the
// kana-skip fallback, the table covers the rest)
QString styleZh(const QString &key, const QString &name) {
    static const QHash<QString, QString> t = {
        {QStringLiteral("noir"), QStringLiteral("黑夜色差")},
        {QStringLiteral("crimson"), QStringLiteral("绯红信号")},
        {QStringLiteral("caution"), QStringLiteral("警示")},
        {QStringLiteral("magenta"), QStringLiteral("波普品红")},
        {QStringLiteral("paper"), QStringLiteral("纸与墨")},
        {QStringLiteral("hud"), QStringLiteral("暗色HUD")},
        {QStringLiteral("mint"), QStringLiteral("薄荷终端")},
        {QStringLiteral("specimen"), QStringLiteral("标本")},
        {QStringLiteral("transit"), QStringLiteral("过境")},
        {QStringLiteral("blueprint"), QStringLiteral("蓝图")},
        {QStringLiteral("rouge"), QStringLiteral("胭脂渐变")},
        {QStringLiteral("mono"), QStringLiteral("单色RGB")},
        {QStringLiteral("sakura"), QStringLiteral("樱")},
        {QStringLiteral("sunset"), QStringLiteral("夕阳渐变")},
        {QStringLiteral("forest"), QStringLiteral("森林手帖")},
        {QStringLiteral("vapor"), QStringLiteral("蒸汽波")},
        {QStringLiteral("newsprint"), QStringLiteral("报纸")},
        {QStringLiteral("synth80"), QStringLiteral("合成器80s")},
        {QStringLiteral("kraft"), QStringLiteral("牛皮纸")},
        {QStringLiteral("candy"), QStringLiteral("糖果")},
        {QStringLiteral("acid"), QStringLiteral("迷幻酸")},
        {QStringLiteral("sumi"), QStringLiteral("墨与朱")},
        {QStringLiteral("gold"), QStringLiteral("金夜")},
        {QStringLiteral("hrRuin"), QStringLiteral("废墟")},
        {QStringLiteral("hrNightRec"), QStringLiteral("深夜录像")},
        {QStringLiteral("hrCurse"), QStringLiteral("诅咒信件")},
    };
    return t.value(key, name);
}

// mood keys → Chinese
QString moodZh(const QString &key, const QString &name) {
    static const QHash<QString, QString> t = {
        {QStringLiteral("glitch"), QStringLiteral("故障")},
        {QStringLiteral("calm"), QStringLiteral("柔和")},
        {QStringLiteral("pop"), QStringLiteral("律动")},
        {QStringLiteral("graphic"), QStringLiteral("图形")},
        {QStringLiteral("editorial"), QStringLiteral("版式")},
        {QStringLiteral("emotional"), QStringLiteral("抒情")},
        {QStringLiteral("horror"), QStringLiteral("惊悚")},
        {QStringLiteral("chaos"), QStringLiteral("全开混合")},
    };
    return t.value(key, name);
}

} // namespace

LyricMotionPanel::LyricMotionPanel(QWidget * const parent) :
    QWidget(parent) {
    mEngine = new LyricMotionEngine(this);
    setObjectName(QStringLiteral("LyricMotionPanel"));
    setupUi();
    loadSettings();
    // build the gallery from the persisted catalog — compiling the
    // planner here (QV4 on the GUI thread, ~seconds) delayed every
    // startup while the window sat unreponsive before the workspace
    // restore; the engine itself loads lazily on the first replan.
    // Only a first-ever run without a cache still pays the load once
    // (and then writes the cache).
    rebuildStyleCards(true);

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
        if (mPreviewWorker) { mPreviewWorker->setCancelled(); }
        mPreviewThread->quit();
        if (!mPreviewThread->wait(2000)) {
            // a compile in flight cannot be interrupted (QJSEngine::
            // evaluate); leaking the thread beats destroying a running
            // QThread — the exit watchdog reaps the process anyway
            mPreviewThread->setParent(nullptr);
            mPreviewThread->disconnect();
            mPreviewThread = nullptr;
        }
    }
}

void LyricMotionPanel::showEvent(QShowEvent *e) {
    QWidget::showEvent(e);
    mFrameTimer.start();
    // deferred: visibility of dock contents is not settled inside
    // showEvent; render only the selected style's card (the rest stay
    // cheap swatches until hovered — per-card rendering rebuilds the
    // whole QJSEngine, batching all cards ground one core for ~10s
    // at every startup)
    QTimer::singleShot(0, this, [this]() {
        const QString key = mStyleCombo->currentData().toString();
        if (auto *card = findCard(key)) { requestCardPreview(card); }
    });
}

void LyricMotionPanel::hideEvent(QHideEvent *e) {
    QWidget::hideEvent(e);
    mFrameTimer.stop();
}

void LyricMotionPanel::setupPreviewWorker() {
    mPreviewThread = new QThread(this);
    // QV4 compiles the 45k-line vendored sources with deep recursion;
    // the default thread stack is too tight on memory-pressured
    // startups (user hit SEGV inside evaluate at boot)
    mPreviewThread->setStackSize(32 * 1024 * 1024);
    mPreviewWorker = new LyricPreviewWorker(240, 135, 10);
    mPreviewWorker->moveToThread(mPreviewThread);
    connect(mPreviewThread, &QThread::started,
            mPreviewWorker, &LyricPreviewWorker::setup);
    connect(mPreviewWorker, &LyricPreviewWorker::framesReady, this,
            [this](const QString &key, const int generation,
                   const QVector<QImage> &frames) {
        if (generation != mPreviewGeneration) { return; } // stale batch
        if (auto *card = findCard(key)) { card->setFrames(frames); }
    }, Qt::QueuedConnection);
    connect(mPreviewWorker, &LyricPreviewWorker::styleFailed, this,
            [this](const QString &key, const int generation,
                   const QString &error) {
        if (generation != mPreviewGeneration) { return; }
        if (auto *card = findCard(key)) { card->setPending(false); }
        setStatus(tr("风格预览失败 %1: %2").arg(key, error.left(60)), true);
    }, Qt::QueuedConnection);
    // defer the thread start past the boot GL/effects bring-up — the
    // QVSEngine allocation spike landing on that peak crashed startup
    // on memory-pressured machines
    QTimer::singleShot(1200, this, [this]() {
        if (mPreviewThread) { mPreviewThread->start(); }
    });
}

LyricStyleCard *LyricMotionPanel::findCard(const QString &key) const {
    for (LyricStyleCard *card : mCards) {
        if (card->key() == key) { return card; }
    }
    return nullptr;
}

void LyricMotionPanel::requestCardPreview(LyricStyleCard * const card) {
    if (!isVisible() || !mPreviewWorker || !card) { return; }
    if (card->hasFrames() || card->isPending()) { return; }
    card->setPending(true);
    const auto params = collectParams();
    // queue onto the worker's event loop: a direct call here executes
    // in the CALLER's thread regardless of moveToThread, which ran
    // the whole plan + frame render on the GUI thread and froze the
    // workspace for the duration of every dock-open / hover request
    auto * const worker = mPreviewWorker;
    const QString key = card->key();
    const quint32 seed = params.seed;
    const qreal density = params.density;
    const QString lyrics = params.lyrics;
    const QVector<qreal> beats = params.beats;
    const qreal audioDuration = params.audioDuration;
    const qreal fps = sceneFps();
    const int generation = mPreviewGeneration;
    QMetaObject::invokeMethod(worker, [worker, key, seed, density,
                                       lyrics, beats, audioDuration,
                                       fps, generation]() {
        worker->renderStyle(key, seed, density, lyrics, beats,
                            audioDuration, fps, generation);
    }, Qt::QueuedConnection);
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
    mOmakaseButton->setText(tr("随机方案"));
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

    // controls row 3: cut-boundary transition. The planner only rolls
    // a transition on ~30% of boundaries by default, which read as
    // "no transitions at all" — the combo forces one per boundary
    // through the planner's per-line override channel
    auto *ctrl3 = new QHBoxLayout();
    ctrl3->setSpacing(4);
    mTransCombo = new QComboBox(this);
    mTransCombo->addItem(tr("转场：自动"), QString());
    mTransCombo->addItem(tr("每切混合"), QStringLiteral("@mixed"));
    mTransCombo->addItem(tr("擦除"), QStringLiteral("wipe"));
    mTransCombo->addItem(tr("推移"), QStringLiteral("pushSlide"));
    mTransCombo->addItem(tr("甩镜"), QStringLiteral("whipPan"));
    mTransCombo->addItem(tr("缩放穿越"), QStringLiteral("zoomThrough"));
    mTransCombo->addItem(tr("白闪"), QStringLiteral("flashCross"));
    mTransCombo->addItem(tr("对角擦"), QStringLiteral("diagonalWipe"));
    mTransCombo->addItem(tr("圆虹"), QStringLiteral("irisOpen"));
    mTransCombo->addItem(tr("立方"), QStringLiteral("cubeTurn"));
    mTransCombo->addItem(tr("旋转退场"), QStringLiteral("spinOut"));
    mTransCombo->setToolTip(tr("切间转场：自动=规划器按风格抽样；"
                               "其余=每个切边界强制使用所选转场"));
    ctrl3->addWidget(new QLabel(tr("转场"), this), 0);
    ctrl3->addWidget(mTransCombo, 1);
    mainLayout->addLayout(ctrl3);

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
    connect(mTransCombo, &QComboBox::currentIndexChanged,
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
    const QString trans = AppSupport::getSettings(
        QStringLiteral("LyricPanel"), QStringLiteral("trans"),
        QString()).toString();
    if (const int idx = mTransCombo->findData(trans); idx >= 0) {
        mTransCombo->setCurrentIndex(idx);
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
    AppSupport::setSettings(QStringLiteral("LyricPanel"),
                            QStringLiteral("trans"),
                            mTransCombo->currentData().toString());
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
    const QString tdata = mTransCombo->currentData().toString();
    p.transMode = tdata == QLatin1String("@mixed") ? 1
                : tdata.isEmpty() ? 0 : 2;
    p.transKey = tdata;
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

void LyricMotionPanel::rebuildStyleCards(const bool fromCache) {
    QList<LyricMotionEngine::StyleInfo> styles;
    QStringList moods;
    if (fromCache && !mEngine->isLoaded()) {
        // startup fast path: the persisted catalog (written by the
        // engine after its last successful load) mirrors styles()/
        // moodNames() without compiling the planner
        const QString cached = AppSupport::getSettings(
                    QStringLiteral("LyricPanel"),
                    QStringLiteral("catalog")).toString();
        if (LyricMotionEngine::catalogFromJson(cached, &styles, &moods)) {
            buildStyleCardsFrom(styles, moods);
            return;
        }
    }
    QString err;
    if (!mEngine->ensureLoaded(&err)) {
        setStatus(tr("引擎加载失败: %1").arg(err), true);
        return;
    }
    buildStyleCardsFrom(mEngine->styles(), mEngine->moodNames());
}

void LyricMotionPanel::buildStyleCardsFrom(
        const QList<LyricMotionEngine::StyleInfo> &styles,
        const QStringList &moods) {
    // combos (first build only)
    mBuildingUi = true;
    if (mStyleCombo->count() == 0) {
        for (const auto &info : styles) {
            mStyleCombo->addItem(styleZh(info.key, info.name), info.key);
        }
    }
    if (mMoodCombo->count() == 1) {
        for (const QString &m : moods) {
            const int sep = m.indexOf(QLatin1Char('|'));
            const QString key = m.left(sep);
            mMoodCombo->addItem(moodZh(key, m.mid(sep + 1)), key);
        }
    }
    mBuildingUi = false;
    // loadSettings() runs before the combos exist, so the persisted
    // style/mood could not be applied then — replay them now that the
    // combo data is in place (without this every restart fell back to
    // the first style)
    {
        const QString style = AppSupport::getSettings(
                    QStringLiteral("LyricPanel"), QStringLiteral("style"),
                    QStringLiteral("noir")).toString();
        const int sIdx = mStyleCombo->findData(style);
        if (sIdx >= 0) { mStyleCombo->setCurrentIndex(sIdx); }
        const QString mood = AppSupport::getSettings(
                    QStringLiteral("LyricPanel"), QStringLiteral("mood"),
                    QString()).toString();
        const int mIdx = mMoodCombo->findData(mood);
        if (mIdx >= 0) { mMoodCombo->setCurrentIndex(mIdx); }
    }

    // cards
    for (LyricStyleCard *card : mCards) {
        mCardLayout->removeWidget(card);
        card->deleteLater();
    }
    mCards.clear();
    const QString currentStyle = mStyleCombo->currentData().toString();
    for (const auto &info : styles) {
        // localized copy for the card (name plate + tooltip drive the
        // combo-selection highlight, so they must match the combo text)
        auto zhInfo = info;
        zhInfo.name = styleZh(info.key, info.name);
        const auto card = new LyricStyleCard(zhInfo, mGalleryHost);
        card->setSelected(info.key == currentStyle);
        card->onClicked = [this, key = info.key]() {
            mBuildingUi = true;
            mStyleCombo->setCurrentIndex(mStyleCombo->findData(key));
            mBuildingUi = false;
            scheduleReplan();
        };
        card->onHovered = [this, card]() { requestCardPreview(card); };
        mCardLayout->addWidget(card);
        mCards << card;
    }
    mPreviewGeneration++; // stale async results are dropped on arrival
}

void LyricMotionPanel::replanNow() {
    if (!mEngine->isLoaded()) {
        QString err;
        if (!mEngine->ensureLoaded(&err)) {
            setStatus(tr("引擎加载失败: %1").arg(err), true);
            return;
        }
        rebuildStyleCards(false);
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
        const QString layoutKey = c.value(QStringLiteral("layout")).toString();
        const QString enterKey = c.value(QStringLiteral("enter")).toString();
        const QString exitKey = c.value(QStringLiteral("exit")).toString();
        item->setText(2, partZh(QStringLiteral("layout"), layoutKey, mEngine));
        item->setText(3, partZh(QStringLiteral("enter"), enterKey, mEngine));
        item->setText(4, partZh(QStringLiteral("exit"), exitKey, mEngine));
        // the raw keys ride in the tooltip (they are what the plan and
        // the native builder consume)
        item->setToolTip(2, layoutKey);
        item->setToolTip(3, enterKey);
        item->setToolTip(4, exitKey);
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
    const auto replayParams = collectParams();
    const bool ok = LyricMotionNative::build(scene, plan, fonts, newAudio,
                mIncludeAudio->isChecked(), &result, &error,
                &replayParams);
    if (ok && !newAudio.isEmpty()) { mAppliedAudioPath = newAudio; }
    mApplying = false;
    mApplyButton->setEnabled(true);
    if (!ok) {
        setStatus(tr("应用失败: %1").arg(error), true);
        return;
    }
    // the playhead sits at frame 0 on a fresh scene while the first
    // cut starts frames later — jump into the first cut so the canvas
    // immediately shows something instead of looking empty
    {
        const auto firstCut = plan.value(QStringLiteral("cuts"))
                .toArray().first().toObject();
        const qreal mid = (firstCut.value(QStringLiteral("start"))
                           .toDouble()
                           + firstCut.value(QStringLiteral("end")).toDouble()) * 0.5;
        scene->anim_setAbsFrame(qMax(0, qRound(mid * scene->getFps())));
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

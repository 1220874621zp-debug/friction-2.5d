#ifndef LYRICMOTIONPANEL_H
#define LYRICMOTIONPANEL_H

#include "lyricmotionengine.h"
#include "lyricmotionaudio.h"

#include <QWidget>
#include <QTimer>
#include <QList>

class QPlainTextEdit;
class QComboBox;
class QSpinBox;
class QSlider;
class QPushButton;
class QTreeWidget;
class QLabel;
class QCheckBox;
class QFileDialog;
class QLabel;
class QScrollArea;
class QThread;
class QToolButton;
class FlowLayout;
class LyricStyleCard;
class LyricPreviewWorker;

// 歌词动画面板 — vendored JIZURA planner (MIT, (c) 852wa) + one-shot
// "apply to scene" that materializes the plan as friction text layers
// with enter/exit keyframes. Phase 1: planning, style swatch cards,
// cut list, apply. Real rendered previews arrive in phase 2.
class LyricMotionPanel : public QWidget
{
    Q_OBJECT
public:
    explicit LyricMotionPanel(QWidget * const parent = nullptr);
    ~LyricMotionPanel() override;

protected:
    void showEvent(QShowEvent *e) override;
    void hideEvent(QHideEvent *e) override;

private:
    void setupUi();
    void loadSettings();
    void saveSettings();

    LyricMotionEngine::Params collectParams() const;
    void scheduleReplan();
    void replanNow();
    void rebuildStyleCards(bool fromCache);
    void buildStyleCardsFrom(const QList<LyricMotionEngine::StyleInfo> &styles,
                             const QStringList &moods);
    void populateCuts();
    void applyToScene();
    void setStatus(const QString &text, const bool error = false);
    void setupPreviewWorker();
    void requestCardPreview(LyricStyleCard * const card);
    void advancePreviews();
    qreal sceneFps() const;
    LyricStyleCard *findCard(const QString &key) const;

    QThread *mPreviewThread = nullptr;
    LyricPreviewWorker *mPreviewWorker = nullptr;
    int mPreviewGeneration = 0;
    bool mApplying = false;
    QTimer mFrameTimer;

    LyricMotionEngine *mEngine = nullptr;

    QPlainTextEdit *mLyricsEdit = nullptr;
    QComboBox *mStyleCombo = nullptr;
    QComboBox *mMoodCombo = nullptr;
    QComboBox *mTransCombo = nullptr;
    QSpinBox *mSeedSpin = nullptr;
    QToolButton *mSeedDice = nullptr;
    QToolButton *mOmakaseButton = nullptr;
    QSlider *mDensitySlider = nullptr;
    QSlider *mChromaSlider = nullptr;
    QSpinBox *mBpmSpin = nullptr;
    QPushButton *mAudioButton = nullptr;
    QLabel *mAudioLabel = nullptr;
    QCheckBox *mIncludeAudio = nullptr;
    QString mAudioPath;
    QString mAppliedAudioPath;
    LyricAudioAnalysis mAudioAnalysis;

    QScrollArea *mGalleryScroll = nullptr;
    QWidget *mGalleryHost = nullptr;
    FlowLayout *mCardLayout = nullptr;
    QList<LyricStyleCard *> mCards;

    QTreeWidget *mCutsTree = nullptr;
    QPushButton *mApplyButton = nullptr;
    QLabel *mStatus = nullptr;

    QTimer mReplanTimer;
    QString mPlanJson; // last successful plan (JSON, incl. fonts table)
    bool mBuildingUi = false; // suppress replan while populating controls
};

#endif // LYRICMOTIONPANEL_H

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

#ifndef EFFECTSPRESETSPANEL_H
#define EFFECTSPRESETSPANEL_H

#include <QWidget>
#include <QTreeWidget>
#include <QLineEdit>
#include <QByteArray>
#include <QImage>
#include <QMap>
#include <functional>
#include "RasterEffects/rastereffect.h"

class MainWindow;
class BoundingBox;
class QStackedWidget;
class QToolButton;
class QTimer;
class QScrollArea;
class QVBoxLayout;
class QPushButton;
class QLabel;
class QHBoxLayout;
class QButtonGroup;
class QSlider;
class FlowLayout;

// a null target applies to every selected layer (AE double-click
// semantics); an explicit target applies to just that layer
// (drag & drop)
using EffectApplyFn = std::function<void(BoundingBox*)>;

// AE-style Effects & Presets panel with live search filtering,
// category tree, and double-click / Enter application to every
// selected layer. Effect items can also be dragged: dropping on the
// canvas applies to the layer under the cursor, dropping on a
// Timeline layer row applies to that layer. User-saved effect-stack
// presets (".ffp" files) appear under the 我的效果预设 category.
//
// The panel has two views sharing one toolbar: the classic tree and a
// visual card grid (like the text animation preset panel) whose tiles
// play looping effect previews rendered offscreen on worker threads.

class EffectsPresetsPanel;

// animated preview painter with a rounded dark frame; plays back the
// offscreen-rendered frame sequence of one effect
class EffectPreviewArea : public QWidget {
    Q_OBJECT
public:
    EffectPreviewArea(QWidget* const parent = nullptr);

    void setFrames(const QList<QImage>& frames);
    void advance();
    void setPlaceholder(const QString& text);
    // dark shadows are invisible on the dark default base: shadow
    // effects get a light backdrop instead
    void setLightBase(const bool light);

protected:
    void paintEvent(QPaintEvent* const e) override;

private:
    QList<QImage> mFrames;
    int mFrame = 0;
    QString mPlaceholder;
    bool mLightBase = false;
};

// one effect card: preview, name, category tag and an apply button;
// double-click applies, drag&drop applies to the drop target
class EffectPreviewTile : public QWidget {
    Q_OBJECT
public:
    EffectPreviewTile(const RasterEffectType type,
                      const QString& name,
                      const QString& category,
                      const EffectApplyFn& apply,
                      QWidget* const parent = nullptr);

    RasterEffectType effectType() const { return mType; }
    void setChecked(const bool checked);
    void advance() { if (mPreviewArea) { mPreviewArea->advance(); } }
    void applyNow() { if (mApply) { mApply(nullptr); } }
    void setLoading();
    void setPreviewSize(const int size);
    void setLightPreview()
    { if (mPreviewArea) { mPreviewArea->setLightBase(true); } }
    void setTileFrames(const QList<QImage>& frames)
    { if (mPreviewArea) { mPreviewArea->setFrames(frames); } }
    void setUnavailable()
    {
        if (mPreviewArea) {
            mPreviewArea->setPlaceholder(
                        QString::fromUtf8("预览不可用"));
        }
    }

    bool matches(const QString& query, const QString& categoryTag) const;

signals:
    void applyRequested(EffectPreviewTile* tile);
    void tileClicked(EffectPreviewTile* tile);

protected:
    void mousePressEvent(QMouseEvent* const e) override;
    void mouseMoveEvent(QMouseEvent* const e) override;
    void mouseDoubleClickEvent(QMouseEvent* const e) override;
    void paintEvent(QPaintEvent* const e) override;
    QSize sizeHint() const override;

private:
    void startEffectDrag();

    RasterEffectType mType;
    QString mName;
    QString mCategory;
    EffectApplyFn mApply;
    EffectPreviewArea* mPreviewArea = nullptr;
    QLabel* mNameLabel = nullptr;
    QLabel* mTagLabel = nullptr;
    QPushButton* mApplyBtn = nullptr;
    bool mChecked = false;
    QPoint mDragStart;
    bool mDragging = false;
};

class EffectsPresetsPanel : public QWidget {
    Q_OBJECT
public:
    // a null target applies to every selected layer (AE double-click
    // semantics); an explicit target applies to just that layer
    // (drag & drop)
    using EffectApplyFn = ::EffectApplyFn;

    explicit EffectsPresetsPanel(MainWindow * const mainWindow,
                                QWidget * const parent = nullptr);

    void populateEffects();
    void focusSearch();
    void showTreeContextMenu(const QPoint& globalPos);

    // freeze/resume the card gallery while the main canvas preview
    // plays (keeps the UI thread free for playback)
    void setGalleryPaused(const bool paused);

    // drag registry: the apply closure cannot travel through
    // QMimeData, so it is parked here and the mime payload carries
    // only a generation token validated on drop (same process)
    static const QString& sMimeFormat();
    static QByteArray beginEffectDrag(const EffectApplyFn &apply);
    static EffectApplyFn takeEffectDrag(const QByteArray &token);

    EffectApplyFn effectCallback(QTreeWidgetItem *item) const
    { return mApplyCallbacks.value(item); }

private slots:
    void onSearchTextChanged(const QString &text);
    void onItemDoubleClicked(QTreeWidgetItem *item, int column);
    void onApplyPressed();
    void onImportEffectClicked();
    void onOpenFolderClicked();
    void onRefreshClicked();
    void onViewModeToggled(const bool checked);
    void onTileApplyRequested(EffectPreviewTile *tile);
    void onTileClicked(EffectPreviewTile *tile);

protected:
    void showEvent(QShowEvent* const e) override;
    void hideEvent(QHideEvent* const e) override;

private:
    void addEffectItem(const QString &categoryName,
                       const QString &effectName,
                       const QString &desc,
                       const EffectApplyFn &applyFunc);
    void populateUserPresets();

    // visual card view
    QWidget* buildGridView();
    void buildTiles();
    void queueTileRender();
    void filterTiles();
    void updatePlayTimer();
    QString categoryTag(const QString& category) const;

    MainWindow *mMainWindow = nullptr;
    QLineEdit *mSearchEdit = nullptr;
    QTreeWidget *mTreeWidget = nullptr;
    QMap<QString, QTreeWidgetItem*> mCategoryItems;
    QMap<QTreeWidgetItem*, EffectApplyFn> mApplyCallbacks;
    // user preset items -> ".ffp" file path (context menu: delete)
    QMap<QTreeWidgetItem*, QString> mUserPresetPaths;

    QStackedWidget* mStack = nullptr;
    QToolButton* mViewToggleBtn = nullptr;
    QTimer* mPlayTimer = nullptr;
    QScrollArea* mGridScroll = nullptr;
    QWidget* mGridHost = nullptr;
    FlowLayout* mFlow = nullptr;
    QHBoxLayout* mCatLayout = nullptr;
    QButtonGroup* mCatGroup = nullptr;
    QString mActiveCategory = QStringLiteral("all");
    QList<EffectPreviewTile*> mTiles;
    bool mTilesBuilt = false;
    // card zoom (card view): slider-driven preview size, remembered
    QSlider* mTileSizeSlider = nullptr;
    int mTileSize = 130;
    bool mGalleryPaused = false;
    // render requested while the grid view was hidden or the panel
    // invisible; honored on the next showEvent / view switch
    bool mRenderOnShow = false;
    // bumped by every refresh / rebuild; in-flight render batches
    // started with an older generation discard their results
    int mTileGeneration = 0;

    static quint64 sDragGeneration;
    static EffectApplyFn sDragCallback;
};

#endif // EFFECTSPRESETSPANEL_H

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

#ifndef TEXTANIMPRESETPANEL_H
#define TEXTANIMPRESETPANEL_H

#include <QWidget>
#include <QImage>
#include "widgets/flowlayout.h"

class QGridLayout;
class QScrollArea;
class QSlider;
class QSpinBox;
class QLabel;
class QTimer;
class QPushButton;
class QVBoxLayout;
struct TextAnimPreset;
struct LayerAnimPreset;
class Document;
class TextBox;
class TextAnimTile;
class TextAnimPreview;

// One preset card: square black preview area (animated frames),
// name, and apply buttons at the bottom center - IN / OUT for
// one-shot presets, a single apply button for loops.
class TextAnimTile : public QWidget {
    Q_OBJECT
public:
    // exactly one of the two presets is non-null
    TextAnimTile(const TextAnimPreset* const textPreset,
                 const LayerAnimPreset* const layerPreset,
                 QWidget* const parent = nullptr);

    void setFrames(const QList<QImage>& frames);
    void advance();
    void setChecked(const bool checked)
    { mChecked = checked; update(); }
    void setPreviewSize(const int size);

    const TextAnimPreset* textPreset() const { return mTextPreset; }
    const LayerAnimPreset* layerPreset() const { return mLayerPreset; }

protected:
    bool eventFilter(QObject* const obj, QEvent* const e) override;
    void resizeEvent(QResizeEvent* const e) override;
    void paintEvent(QPaintEvent* const e) override;
    void enterEvent(QEvent* const e) override;
    void leaveEvent(QEvent* const e) override;
    QSize sizeHint() const override;

private:
    void repositionButtons();
    void paintFrame();

    enum class Dir { in = 0, out = 1, all = 2 };

signals:
    void previewClicked(TextAnimTile* tile);
    void applyRequested(TextAnimTile* tile, int dir);


private:
    const TextAnimPreset* mTextPreset = nullptr;
    const LayerAnimPreset* mLayerPreset = nullptr;
    class TextAnimPreview* mPreviewArea = nullptr;
    QLabel* mNameLabel = nullptr;
    QList<QPushButton*> mApplyButtons;
    QList<QImage> mFrames;
    int mFrame = 0;
    bool mChecked = false;
    bool mHover = false;
};

// Animated preview painter with a black background.
class TextAnimPreview : public QWidget {
    Q_OBJECT
public:
    TextAnimPreview(QWidget* const parent = nullptr);

    void setFrames(const QList<QImage>& frames);
    void advance();
    void setPlaceholder(const QString& text);

protected:
    void paintEvent(QPaintEvent* const e) override;
    QSize sizeHint() const override { return QSize(320, 132); }

private:
    QList<QImage> mFrames;
    int mFrame = 0;
    QString mPlaceholder;
};

// Dockable animation preset browser: collapsible sections for
// text / image / loop presets, animated square thumbnails with
// per-preset IN (entrance) and OUT (exit) apply buttons, and a
// big preview of the selected preset.
class TextAnimPresetPanel : public QWidget {
    Q_OBJECT
public:
    TextAnimPresetPanel(Document& doc, QWidget* const parent = nullptr);

    // pause the thumbnail gallery animation while the main canvas
    // preview plays: dozens of tiles repainting at 25 fps on the UI
    // thread starve the playback timer and cause frame skips
    void setGalleryPaused(const bool paused);

protected:
    void showEvent(QShowEvent* const e) override;
    void hideEvent(QHideEvent* const e) override;

private:
    struct Section {
        QPushButton* header = nullptr;
        QWidget* body = nullptr;
        class FlowLayout* flow = nullptr;
        bool built = false;
        bool expanded = false;
    };

    void buildSection(Section& section, const int kind);
    void fillGridFromKind(const int kind, FlowLayout* flow);
    void ensureTileFrames(TextAnimTile* const tile);
    void selectTile(TextAnimTile* const tile);
    void applyPreset(TextAnimTile* const tile, const int dir);
    TextBox* selectedTextBox() const;
    QList<TextBox*> selectedTextBoxes() const;
    QList<class BoundingBox*> selectedBoxes() const;
    const QImage& mascotImage();
    const QString& defaultCjkFamily();

    Document& mDocument;
    Section mTextSection;
    Section mImageSection;
    Section mLoopSection;
    QTimer* mPlayTimer = nullptr;

    QScrollArea* mScroll = nullptr;
    QWidget* mScrollHost = nullptr;
    QLabel* mStatusLabel = nullptr;
    QSlider* mDurationSlider = nullptr;
    QSpinBox* mDurationSpin = nullptr;
    QSlider* mTileSizeSlider = nullptr;
    int mTileSize = 150;

    QList<TextAnimTile*> mTiles;
    qreal mDurationScale = 1.0;
    QImage mMascot;
    bool mMascotTried = false;
    QString mDefaultCjkFamily;
};

#endif // TEXTANIMPRESETPANEL_H

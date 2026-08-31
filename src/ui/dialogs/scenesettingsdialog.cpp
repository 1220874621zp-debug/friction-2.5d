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

// Fork of enve - Copyright (C) 2016-2020 Maurycy Liebner

#include "scenesettingsdialog.h"
#include "canvas.h"
#include "GUI/coloranimatorbutton.h"
#include "appsupport.h"
#include "Private/document.h"
#include "Private/esettings.h"

namespace {

// timecode H:MM:SS:FF <-> frame count at the given fps
QString durationTimecode(const int frames, const qreal fps)
{
    const int ifps = qMax(1, qRound(fps));
    const int clamped = qMax(0, frames);
    const int f = clamped % ifps;
    int s = clamped / ifps;
    const int ss = s % 60;
    s /= 60;
    const int mm = s % 60;
    s /= 60;
    return QString("%1:%2:%3:%4").arg(s)
            .arg(mm, 2, 10, QChar('0'))
            .arg(ss, 2, 10, QChar('0'))
            .arg(f, 2, 10, QChar('0'));
}

// accepts 1-4 colon-separated numeric segments (FF, SS:FF,
// MM:SS:FF, H:MM:SS:FF) in frames mode, a plain decimal number of
// seconds in seconds mode
bool parseDurationField(const QString &text,
                        const bool secondsMode,
                        const qreal fps,
                        int &outFrames)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) { return false; }
    if (secondsMode) {
        bool ok = false;
        const double secs = QLocale().toDouble(
                    QString(trimmed).replace(QLatin1Char(','),
                                             QLatin1Char('.')), &ok);
        if (!ok || secs < 0.) { return false; }
        outFrames = qRound(secs * fps);
        return true;
    }
    const auto parts = trimmed.split(QLatin1Char(':'), Qt::SkipEmptyParts);
    if (parts.isEmpty() || parts.count() > 4) { return false; }
    qint64 h = 0, m = 0, s = 0, f = 0;
    const int n = parts.count();
    bool ok = true;
    f = parts.at(n - 1).toLongLong(&ok);
    if (!ok || f < 0) { return false; }
    if (n >= 2) {
        s = parts.at(n - 2).toLongLong(&ok);
        if (!ok || s < 0) { return false; }
    }
    if (n >= 3) {
        m = parts.at(n - 3).toLongLong(&ok);
        if (!ok || m < 0) { return false; }
    }
    if (n >= 4) {
        h = parts.at(n - 4).toLongLong(&ok);
        if (!ok || h < 0) { return false; }
    }
    const qint64 total = qRound((h * 3600 + m * 60 + s) * fps) + f;
    outFrames = int(qBound<qint64>(0, total, INT_MAX));
    return true;
}

} // namespace

SceneSettingsDialog::SceneSettingsDialog(Canvas * const canvas,
                                         QWidget * const parent)
    : SceneSettingsDialog(canvas->prp_getName(),
                          canvas->getCanvasWidth(),
                          canvas->getCanvasHeight(),
                          canvas->getFrameRange(),
                          canvas->getFps(),
                          canvas->getBgColorAnimator(),
                          parent,
                          false)
{
    mTargetCanvas = canvas;
}

SceneSettingsDialog::SceneSettingsDialog(const QString &defName,
                                         QWidget * const parent)
    : SceneSettingsDialog(defName,
                          AppSupport::getSettings("scene",
                                                  "DefaultWidth",
                                                  1920).toInt(),
                          AppSupport::getSettings("scene",
                                                  "DefaultHeight",
                                                  1080).toInt(),
                          {AppSupport::getSettings("scene",
                                                   "DefaultMin",
                                                   0).toInt(),
                           AppSupport::getSettings("scene",
                                                   "DefaultMax",
                                                   299).toInt()},
                          AppSupport::getSettings("scene",
                                                  "DefaultFps",
                                                  30.).toDouble(),
                          nullptr,
                          parent,
                          true)
{

}

SceneSettingsDialog::SceneSettingsDialog(const QString &name,
                                         const int width,
                                         const int height,
                                         const FrameRange& range,
                                         const qreal fps,
                                         ColorAnimator * const bg,
                                         QWidget * const parent,
                                         bool isNew)
    : Friction::Ui::Dialog(parent)
{
    const auto presetsFpsSettings = AppSupport::getFpsPresetStatus();
    const auto presetsResolutionSettings = AppSupport::getResolutionPresetStatus();

    mEnableFpsPresets = presetsFpsSettings.first;
    mEnableFpsPresetsAuto = presetsFpsSettings.second;
    mEnableResolutionPresets = presetsResolutionSettings.first;
    mEnableResolutionPresetsAuto = presetsResolutionSettings.second;

    setWindowTitle(tr("Scene Properties"));
    mMainLayout = new QVBoxLayout(this);
    mMainLayout->setSizeConstraint(QLayout::SetFixedSize);

    setLayout(mMainLayout);

    // form grid: labels live in column 0 and every input column starts
    // at the same left edge, so name/size/duration/fps/background rows
    // line up instead of each input trailing its own label width
    mFormGrid = new QGridLayout();
    mFormGrid->setContentsMargins(0, 0, 0, 0);
    mFormGrid->setHorizontalSpacing(6);
    mFormGrid->setVerticalSpacing(6);
    const auto formLabel = [](QLabel * const w) {
        w->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    };
    // uniform number-field widths so the columns read as one block
    const int inputMinWidth = 96;

    // row 0: name
    mNameEditLabel = new QLabel(tr("Name"), this);
    formLabel(mNameEditLabel);
    mNameEdit = new QLineEdit(name, this);
    connect(mNameEdit, &QLineEdit::textChanged,
            this, &SceneSettingsDialog::validate);
    mFormGrid->addWidget(mNameEditLabel, 0, 0);
    mFormGrid->addWidget(mNameEdit, 0, 1, 1, 3);

    // row 1: resolution presets - picking one fills width/height
    mResPresetLabel = new QLabel(tr("Preset"), this);
    formLabel(mResPresetLabel);
    mResPresetCombo = new QComboBox(this);
    connect(mResPresetCombo,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](const int index) {
        if (index <= 0) { return; }
        const QSize sz = mResPresetCombo->itemData(index).toSize();
        if (sz.width() <= 0 || sz.height() <= 0) { return; }
        mWidthSpinBox->setValue(sz.width());
        mHeightSpinBox->setValue(sz.height());
    });
    mFormGrid->addWidget(mResPresetLabel, 1, 0);
    mFormGrid->addWidget(mResPresetCombo, 1, 1, 1, 3);

    // rows 2-3: width / height
    mWidthLabel = new QLabel(tr("Width"), this);
    formLabel(mWidthLabel);
    mWidthSpinBox = new QSpinBox(this);
    mWidthSpinBox->setRange(1, INT_MAX);
    mWidthSpinBox->setMinimumWidth(inputMinWidth);
    mWidthSpinBox->setValue(width);
    connect(mWidthSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &SceneSettingsDialog::syncResPresetCombo);
    mFormGrid->addWidget(mWidthLabel, 2, 0);
    mFormGrid->addWidget(mWidthSpinBox, 2, 1);

    mHeightLabel = new QLabel(tr("Height"), this);
    formLabel(mHeightLabel);
    mHeightSpinBox = new QSpinBox(this);
    mHeightSpinBox->setRange(1, INT_MAX);
    mHeightSpinBox->setMinimumWidth(inputMinWidth);
    mHeightSpinBox->setValue(height);
    connect(mHeightSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &SceneSettingsDialog::syncResPresetCombo);
    mFormGrid->addWidget(mHeightLabel, 3, 0);
    mFormGrid->addWidget(mHeightSpinBox, 3, 1);

    // row 4: duration typed directly as timecode H:MM:SS:FF (or plain
    // seconds when the time ruler below is set to seconds); the scene
    // start frame is preserved, the field only sets the length
    mRangeMin = range.fMin;
    mInitialFrames = qMax(1, range.fMax - range.fMin + 1);
    mDurationLabel = new QLabel(tr("Duration"), this);
    formLabel(mDurationLabel);
    mDurationEdit = new QLineEdit(
                durationTimecode(mInitialFrames, fps), this);
    mDurationEdit->setMinimumWidth(110);
    mDurationEdit->setToolTip(tr(
        "Duration as timecode hours:minutes:seconds:frames, "
        "e.g. 0:00:10:00 is 10 seconds"));
    connect(mDurationEdit, &QLineEdit::textChanged,
            this, &SceneSettingsDialog::validate);
    mFormGrid->addWidget(mDurationLabel, 4, 0);
    mFormGrid->addWidget(mDurationEdit, 4, 1);

    // row 5: time ruler - the unit the duration field is entered in
    mRulerLabel = new QLabel(tr("Time Ruler"), this);
    formLabel(mRulerLabel);
    mTypeTime = new QComboBox(this);
    mTypeTime->addItem(tr("Frames"), "Frames");
    mTypeTime->addItem(tr("Seconds"), "Seconds");
    mFormGrid->addWidget(mRulerLabel, 5, 0);
    mFormGrid->addWidget(mTypeTime, 5, 1);

    mFpsToolButton = new QToolButton(this);
    mFpsToolButton->setArrowType(Qt::NoArrow);
    mFpsToolButton->setPopupMode(QToolButton::InstantPopup);
    mFpsToolButton->setObjectName("FlatButton");
    mFpsToolButton->setIcon(QIcon::fromTheme("dots"));
    mFpsToolButton->setVisible(mEnableFpsPresets);
    mFpsToolButton->setEnabled(mEnableFpsPresets);
    mFpsToolButton->setIconSize(QSize(eSizesUI::widget, eSizesUI::widget));
    mFpsToolButton->setFixedSize(QSize(eSizesUI::widget, eSizesUI::widget));

    // row 6: fps (+ presets dropdown)
    mFPSLabel = new QLabel(tr("Fps"), this);
    formLabel(mFPSLabel);
    mFPSSpinBox = new QDoubleSpinBox(this);
    mFPSSpinBox->setLocale(QLocale(QLocale::English,
                                   QLocale::UnitedStates));
    mFPSSpinBox->setRange(1, INT_MAX);
    mFPSSpinBox->setMinimumWidth(inputMinWidth);
    mFPSSpinBox->setDecimals(3);
    mFPSSpinBox->setValue(fps);

    mFormGrid->addWidget(mFPSLabel, 6, 0);
    mFormGrid->addWidget(mFPSSpinBox, 6, 1);
    mFormGrid->addWidget(mFpsToolButton, 6, 2, Qt::AlignVCenter);

    // row 7: background color
    mBgColorLabel = new QLabel(tr("Background"), this);
    formLabel(mBgColorLabel);
    mBgColorButton = new ColorAnimatorButton(bg, this);
    if (!bg) {
        if (isNew) {
            mBgColorButton->setColor(AppSupport::getSettings("scene",
                                                             "DefaultColor",
                                                             QColor(Qt::black)).value<QColor>());
        } else { mBgColorButton->setColor(Qt::black); }
    }

    mFormGrid->addWidget(mBgColorLabel, 7, 0);
    mFormGrid->addWidget(mBgColorButton, 7, 1);

    mFormGrid->setColumnStretch(1, 1);
    mMainLayout->addLayout(mFormGrid);

    mErrorLabel = new QLabel(this);
    mErrorLabel->setObjectName("errorLabel");
    mMainLayout->addWidget(mErrorLabel);

    mOkButton = new QPushButton(QIcon::fromTheme("dialog-ok"),
                                tr("Ok"), this);
    mCancelButton = new QPushButton(QIcon::fromTheme("dialog-cancel"),
                                    tr("Cancel"), this);
    mButtonsLayout = new QHBoxLayout();

    if (isNew) {
        mSaveAsDefault = new QCheckBox(this);
        mSaveAsDefault->setText(tr("Set as default"));
        mSaveAsDefault->setToolTip(tr("Use selected properties as default for new scenes"));
        mSaveAsDefault->setChecked(AppSupport::getSettings("scene",
                                                           "SaveDefault",
                                                           false).toBool());
        mMainLayout->addWidget(mSaveAsDefault);
    }

    mMainLayout->addLayout(mButtonsLayout);

    mButtonsLayout->addWidget(mOkButton);
    mButtonsLayout->addWidget(mCancelButton);

    connect(mOkButton, &QPushButton::released,
            this, &SceneSettingsDialog::accept);
    connect(mCancelButton, &QPushButton::released,
            this, &SceneSettingsDialog::reject);
    connect(this, &QDialog::rejected, this, &QDialog::close);
    connect(mTypeTime, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SceneSettingsDialog::updateDuration);

    validate();

    // black input fields: name / size / duration / fps (user
    // preference); only the input classes are restyled, the presets
    // buttons and combo keep the themed look
    setStyleSheet(QStringLiteral(
        "QLineEdit, QSpinBox, QDoubleSpinBox {"
        " background-color: #000000;"
        " color: #f0f0f2;"
        " border: 1px solid #3c3c42;"
        " border-radius: 2px;"
        " selection-background-color: #3d6a99;"
        " selection-color: #ffffff;"
        "}"
        "QLineEdit:hover, QSpinBox:hover, QDoubleSpinBox:hover {"
        " border-color: #55555f;"
        "}"
        "QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus {"
        " border-color: #3d6a99;"
        "}"));

    populateResPresets();
    populateFpsPresets();
}

bool SceneSettingsDialog::validate()
{
    QString nameError;
    const bool validName = Property::prp_sValidateName(mNameEdit->text(),
                                                       &nameError);
    if (!validName) { nameError += "\n"; }
    int frames = 0;
    const bool secondsMode =
            mTypeTime->currentData().toString() == QLatin1String("Seconds");
    if (!parseDurationField(mDurationEdit->text(), secondsMode,
                            mFPSSpinBox->value(), frames)) {
        nameError += tr("Invalid duration");
    }
    mErrorLabel->setText(nameError);
    const bool valid = validName && nameError.isEmpty();
    mOkButton->setEnabled(valid);
    return valid;
}

int SceneSettingsDialog::getCanvasWidth() const
{
    return mWidthSpinBox->value();
}

int SceneSettingsDialog::getCanvasHeight() const
{
    return mHeightSpinBox->value();
}

QString SceneSettingsDialog::getCanvasName() const
{
    return mNameEdit->text();
}

FrameRange SceneSettingsDialog::getFrameRange() const
{
    // the duration field sets the scene length; the start frame set at
    // construction (existing scene min / default min) is preserved
    FrameRange range;
    const bool secondsMode =
            mTypeTime->currentData().toString() == QLatin1String("Seconds");
    int frames = mInitialFrames;
    parseDurationField(mDurationEdit->text(), secondsMode,
                       mFPSSpinBox->value(), frames);
    frames = qMax(1, frames);
    range = {mRangeMin, mRangeMin + frames - 1};
    range.fixOrder();
    return range;
}

qreal SceneSettingsDialog::getFps() const
{
    return mFPSSpinBox->value();
}

void SceneSettingsDialog::populateFpsPresets()
{
    if (!mEnableFpsPresets) { return; }
    QStringList presets = AppSupport::getFpsPresets();

    QMap<double, QString> m;
    for (auto s : presets) { m[s.toDouble()] = s; }
    presets = QStringList(m.values());

    for (const auto &preset : presets) {
        const auto act = new QAction(preset, this);
        connect (act, &QAction::triggered, [this, preset]() {
            mFPSSpinBox->setValue(preset.toDouble());
        });
        mFpsToolButton->addAction(act);
    }
}

void SceneSettingsDialog::populateResPresets()
{
    mResPresetCombo->blockSignals(true);
    mResPresetCombo->clear();
    // index 0 = placeholder: picking any other entry fills width/height
    mResPresetCombo->addItem(tr("Select resolution preset"), QSize());
    if (mEnableResolutionPresets) {
        const auto presets = AppSupport::getResolutionPresets();
        for (const auto &preset : presets) {
            mResPresetCombo->addItem(QString("%1 x %2")
                                     .arg(preset.first)
                                     .arg(preset.second),
                                     QSize(preset.first, preset.second));
        }
    }
    mResPresetCombo->setEnabled(mEnableResolutionPresets);
    syncResPresetCombo();
    mResPresetCombo->blockSignals(false);
}

void SceneSettingsDialog::syncResPresetCombo()
{
    if (!mResPresetCombo) { return; }
    mResPresetCombo->blockSignals(true);
    int match = 0;
    for (int i = 1; i < mResPresetCombo->count(); i++) {
        const QSize sz = mResPresetCombo->itemData(i).toSize();
        if (sz.width() == mWidthSpinBox->value() &&
            sz.height() == mHeightSpinBox->value()) {
            match = i;
            break;
        }
    }
    mResPresetCombo->setCurrentIndex(match);
    mResPresetCombo->blockSignals(false);
}

void SceneSettingsDialog::applySettingsToCanvas(Canvas * const canvas) const
{
    if (!canvas) { return; }
    canvas->prp_setNameAction(getCanvasName());
    canvas->setCanvasSize(getCanvasWidth(), getCanvasHeight());
    canvas->setFps(getFps());
    canvas->setFrameRange(getFrameRange());

    if (mSaveAsDefault) {
        const bool saveDef = mSaveAsDefault->isChecked();
        AppSupport::setSettings("scene", "SaveDefault", saveDef);
        if (saveDef) {
            AppSupport::setSettings("scene", "DefaultWidth", getCanvasWidth());
            AppSupport::setSettings("scene", "DefaultHeight", getCanvasHeight());
            AppSupport::setSettings("scene", "DefaultMin", getFrameRange().fMin);
            AppSupport::setSettings("scene", "DefaultMax", getFrameRange().fMax);
            AppSupport::setSettings("scene", "DefaultFps", getFps());
            AppSupport::setSettings("scene", "DefaultColor", mBgColorButton->color());
        }
    }
    if (mEnableFpsPresets &&
        mEnableFpsPresetsAuto) { AppSupport::saveFpsPreset(getFps()); }
    if (mEnableResolutionPresets &&
        mEnableResolutionPresetsAuto) { AppSupport::saveResolutionPreset(getCanvasWidth(),
                                                                         getCanvasHeight()); }
    if (canvas != mTargetCanvas) {
        canvas->getBgColorAnimator()->setColor(mBgColorButton->color());

        // Adjust default fill/stroke color to background color
        auto settings = eSettings::sInstance;
        settings->fLastUsedFillColor = AppSupport::adjustColorVisibility(eSettings::instance().fLastUsedFillColor,
                                                                         mBgColorButton->color());
        settings->fLastUsedStrokeColor = AppSupport::adjustColorVisibility(eSettings::instance().fLastUsedStrokeColor,
                                                                           mBgColorButton->color());
    }
}

void SceneSettingsDialog::sNewSceneDialog(Document& document,
                                          QWidget * const parent)
{
    const QString defName = tr("Scene %1").arg(document.fScenes.count());

    const auto dialog = new SceneSettingsDialog(defName, parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    const auto docPtr = &document;
    connect(dialog, &QDialog::accepted, dialog, [dialog, docPtr]() {
        const auto newCanvas = docPtr->createNewScene();
        const auto block = newCanvas->blockUndoRedo();
        dialog->applySettingsToCanvas(newCanvas);
        newCanvas->anim_setAbsFrame(newCanvas->getFrameRange().fMin);
        dialog->close();
        docPtr->actionFinished();
#ifdef Q_OS_MAC
        // https://github.com/friction2d/friction/pull/736
        emit docPtr->fitCanvasToSize();
#endif
    });

    dialog->show();
}

void SceneSettingsDialog::updateDuration(int index)
{
    const qreal fps = mFPSSpinBox->value();
    if (fps <= 0) { return; }
    // switching the time ruler re-expresses the current duration in
    // the new unit: index 1 (seconds) means the field held a timecode,
    // index 0 (frames) means it held plain seconds
    int frames = 0;
    const bool textWasSeconds = index == 0;
    if (!parseDurationField(mDurationEdit->text(), textWasSeconds,
                            fps, frames)) {
        validate(); // unparseable: let the error label speak
        return;
    }
    if (index == 1) {
        mDurationEdit->setText(QString::number(frames / fps, 'f', 3));
    } else {
        mDurationEdit->setText(durationTimecode(frames, fps));
    }
    validate();
}

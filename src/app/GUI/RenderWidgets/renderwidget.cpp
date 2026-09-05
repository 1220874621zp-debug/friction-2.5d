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

#include "renderwidget.h"
#include "canvas.h"
#include "GUI/global.h"
#include "renderinstancewidget.h"
#include "optimalscrollarena/scrollarea.h"
#include "videoencoder.h"
#include "renderhandler.h"
#include "themesupport.h"
#include "../mainwindow.h"
#include "../timelinedockwidget.h"

RenderWidget::RenderWidget(QWidget *parent)
    : QWidget(parent)
    , mMainLayout(nullptr)
    , mRenderProgressBar(nullptr)
    , mStartRenderButton(nullptr)
    , mStopRenderButton(nullptr)
    , mAddRenderButton(nullptr)
    , mClearQueueButton(nullptr)
    , mContWidget(nullptr)
    , mContLayout(nullptr)
    , mScrollArea(nullptr)
    , mCurrentRenderedSettings(nullptr)
    , mState(RenderState::none)
{
    mMainLayout = new QVBoxLayout(this);
    mMainLayout->setMargin(0);
    mMainLayout->setSpacing(0);
    setLayout(mMainLayout);

    const auto topWidget = new QWidget(this);
    topWidget->setContentsMargins(0, 0, 0, 0);
    const auto topLayout = new QHBoxLayout(topWidget);

    setPalette(ThemeSupport::getDarkPalette());
    setAutoFillBackground(true);

    topWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);

    mRenderProgressBar = new QProgressBar(this);
    mRenderProgressBar->setMinimumWidth(10);
    setSizePolicy(QSizePolicy::Expanding,
                  QSizePolicy::Expanding);
    mRenderProgressBar->setFormat("");
    mRenderProgressBar->setValue(0);

    mStartRenderButton = new QPushButton(QIcon::fromTheme("render_animation"),
                                         tr("Render"),
                                         this);
    mStartRenderButton->setFocusPolicy(Qt::NoFocus);
    mStartRenderButton->setSizePolicy(QSizePolicy::Preferred,
                                      QSizePolicy::Preferred);
    // the one affirmative action of the queue reads as "go"
    mStartRenderButton->setStyleSheet(
                QStringLiteral(
                    "QPushButton {"
                    "  background-color: #2f9e44;"
                    "  color: #ffffff;"
                    "  font-weight: bold;"
                    "  border: none;"
                    "  border-radius: 3px;"
                    "  padding: 2px 12px;"
                    "}"
                    "QPushButton:hover { background-color: #37b24d; }"
                    "QPushButton:pressed { background-color: #2b8a3e; }"
                    "QPushButton:disabled {"
                    "  background-color: #3d4a41;"
                    "  color: #96a79d;"
                    "}"));
    connect(mStartRenderButton, &QPushButton::pressed,
            this, qOverload<>(&RenderWidget::render));

    mStopRenderButton = new QPushButton(QIcon::fromTheme("cancel"),
                                        QString(),
                                        this);
    mStopRenderButton->setToolTip(tr("Stop Rendering"));
    mStopRenderButton->setFocusPolicy(Qt::NoFocus);
    mStopRenderButton->setSizePolicy(QSizePolicy::Preferred,
                                     QSizePolicy::Preferred);
    connect(mStopRenderButton, &QPushButton::pressed,
            this, &RenderWidget::stopRendering);
    mStopRenderButton->setEnabled(false);

    mAddRenderButton = new QPushButton(QIcon::fromTheme("plus"),
                                       QString(),
                                       this);
    mAddRenderButton->setToolTip(tr("Add current scene to queue"));
    mAddRenderButton->setFocusPolicy(Qt::NoFocus);
    mAddRenderButton->setSizePolicy(QSizePolicy::Preferred,
                                    QSizePolicy::Preferred);
    connect(mAddRenderButton, &QPushButton::pressed,
            this, []() {
        MainWindow::sGetInstance()->addCanvasToRenderQue();
    });

    mClearQueueButton = new QPushButton(QIcon::fromTheme("trash"),
                                        QString(),
                                        this);
    mClearQueueButton->setToolTip(tr("Clear Queue"));
    mClearQueueButton->setFocusPolicy(Qt::NoFocus);
    mClearQueueButton->setSizePolicy(QSizePolicy::Preferred,
                                     QSizePolicy::Preferred);
    connect(mClearQueueButton, &QPushButton::pressed,
            this, &RenderWidget::clearRenderQueue);

    eSizesUI::widget.add(mStartRenderButton, [this](const int size) {
        Q_UNUSED(size)
        mStartRenderButton->setFixedHeight(eSizesUI::button);
        mStopRenderButton->setFixedHeight(eSizesUI::button);
        mAddRenderButton->setFixedHeight(eSizesUI::button);
        mClearQueueButton->setFixedHeight(eSizesUI::button);
    });

    mContWidget = new QWidget(this);
    mContWidget->setPalette(ThemeSupport::getDarkPalette());
    mContWidget->setAutoFillBackground(true);
    mContWidget->setContentsMargins(0, 0, 0, 0);
    mContLayout = new QVBoxLayout(mContWidget);
    mContLayout->setAlignment(Qt::AlignTop);
    mContLayout->setMargin(0);
    mContLayout->setSpacing(0);
    mContWidget->setLayout(mContLayout);
    mScrollArea = new ScrollArea(this);
    mScrollArea->setWidget(mContWidget);
    mScrollArea->setWidgetResizable(true);
    mScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    // single toolbar: all actions grouped left, the progress/state
    // readout right (the old split top/bottom rows fragmented both)
    topLayout->addWidget(mAddRenderButton);
    topLayout->addWidget(mStartRenderButton);
    topLayout->addWidget(mStopRenderButton);
    topLayout->addWidget(mClearQueueButton);
    topLayout->addStretch();
    mRenderProgressBar->setTextVisible(true);
    mRenderProgressBar->setMaximumWidth(180);
    topLayout->addWidget(mRenderProgressBar);

    mMainLayout->addWidget(topWidget);
    mMainLayout->addWidget(mScrollArea);

    const auto vidEmitter = VideoEncoder::sInstance->getEmitter();
    connect(vidEmitter, &VideoEncoderEmitter::encodingStarted,
            this, &RenderWidget::handleRenderStarted);

    connect(vidEmitter, &VideoEncoderEmitter::encodingFinished,
            this, &RenderWidget::handleRenderFinished);
    connect(vidEmitter, &VideoEncoderEmitter::encodingFinished,
            this, &RenderWidget::sendNextForRender);

    connect(vidEmitter, &VideoEncoderEmitter::encodingInterrupted,
            this, &RenderWidget::clearAwaitingRender);
    connect(vidEmitter, &VideoEncoderEmitter::encodingInterrupted,
            this, &RenderWidget::handleRenderInterrupted);

    connect(vidEmitter, &VideoEncoderEmitter::encodingFailed,
            this, &RenderWidget::handleRenderFailed);
    connect(vidEmitter, &VideoEncoderEmitter::encodingFailed,
            this, &RenderWidget::sendNextForRender);

    connect(vidEmitter, &VideoEncoderEmitter::encodingStartFailed,
            this, &RenderWidget::handleRenderFailed);
    connect(vidEmitter, &VideoEncoderEmitter::encodingStartFailed,
            this, &RenderWidget::sendNextForRender);
}

void RenderWidget::createNewRenderInstanceWidgetForCanvas(Canvas *canvas)
{
    const auto wid = new RenderInstanceWidget(canvas, this);
    addRenderInstanceWidget(wid);
}

void RenderWidget::addRenderInstanceWidget(RenderInstanceWidget *wid)
{
    mContLayout->addWidget(wid);
    connect(wid, &RenderInstanceWidget::destroyed,
            this, [this, wid]() {
        mRenderInstanceWidgets.removeOne(wid);
        mAwaitingSettings.removeOne(wid);
    });
    connect(wid, &RenderInstanceWidget::duplicate,
            this, [this](RenderInstanceSettings& sett) {
        addRenderInstanceWidget(new RenderInstanceWidget(sett, this));
    });
    mRenderInstanceWidgets << wid;
}

void RenderWidget::handleRenderState(const RenderState &state)
{
    mState = state;

    QString renderStateFormat;
    switch (mState) {
    case RenderState::rendering:
        renderStateFormat = tr("%p%");
        break;
    case RenderState::error:
        renderStateFormat = tr("Error");
        break;
    case RenderState::paused:
        renderStateFormat = tr("Paused %p%");
        break;
    case RenderState::waiting:
        renderStateFormat = tr("Waiting %p%");
        break;
    default:
        renderStateFormat = "";
        break;
    }
    bool isIdle = (mState == RenderState::error ||
                   mState == RenderState::finished ||
                   mState == RenderState::none);
    mStartRenderButton->setEnabled(isIdle);
    mStopRenderButton->setEnabled(!isIdle);
    mAddRenderButton->setEnabled(isIdle);
    mRenderProgressBar->setFormat(renderStateFormat);

    const auto timeline = MainWindow::sGetInstance()->getTimeLineWidget();
    if (timeline) { timeline->setEnabled(isIdle); }

    emit renderStateChanged(renderStateFormat, mState);

    if (isIdle) {
        emit progress(mRenderProgressBar->maximum(), mRenderProgressBar->maximum());
        mRenderProgressBar->setValue(0);
        mRenderProgressBar->setRange(0, 100);
    }
}

void RenderWidget::handleRenderStarted()
{
    handleRenderState(RenderState::rendering);
}

void RenderWidget::releaseCurrentRenderedSettings()
{
    if (mCurrentRenderedSettings.isNull()) { return; }
    disconnect(mCurrentRenderedSettings, nullptr, this, nullptr);
    mCurrentRenderedSettings = nullptr;
}

void RenderWidget::handleRenderFinished()
{
    releaseCurrentRenderedSettings();
    handleRenderState(RenderState::finished);
}

void RenderWidget::handleRenderInterrupted()
{
    releaseCurrentRenderedSettings();
    handleRenderState(RenderState::finished);
}

void RenderWidget::handleRenderFailed()
{
    releaseCurrentRenderedSettings();
    handleRenderState(RenderState::error);
}

void RenderWidget::setRenderedFrame(const int frame)
{
    if (mCurrentRenderedSettings.isNull()) { return; }
    mRenderProgressBar->setValue(frame - mProgressBase);
    emit progress(frame - mProgressBase, mRenderProgressBar->maximum());
}

void RenderWidget::clearRenderQueue()
{
    // the encoder and RenderHandler reference the active settings
    // asynchronously (the interrupt flag is processed on the encoder
    // thread), so the rendering item must outlive the teardown - hand
    // its deletion to the terminal encoding signals instead of
    // deleting it here (used to UAF in the encoder's afterProcessing)
    RenderInstanceWidget *activeWid = nullptr;
    if ((mState == RenderState::rendering || mState == RenderState::paused) &&
        !mCurrentRenderedSettings.isNull()) {
        for (const auto wid : mRenderInstanceWidgets) {
            if (&wid->getSettings() == mCurrentRenderedSettings) {
                activeWid = wid;
                break;
            }
        }
    }
    if (activeWid) {
        const auto emitter = VideoEncoder::sInstance->getEmitter();
        const auto deleteActive = [activeWid]() { activeWid->deleteLater(); };
        connect(emitter, &VideoEncoderEmitter::encodingInterrupted,
                activeWid, deleteActive);
        connect(emitter, &VideoEncoderEmitter::encodingFinished,
                activeWid, deleteActive);
        connect(emitter, &VideoEncoderEmitter::encodingFailed,
                activeWid, deleteActive);
        connect(emitter, &VideoEncoderEmitter::encodingStartFailed,
                activeWid, deleteActive);
    }
    if (mState == RenderState::rendering ||
        mState == RenderState::paused ||
        mState == RenderState::waiting) {
        stopRendering();
    }
    for (int i = mRenderInstanceWidgets.count() - 1; i >= 0; i--) {
        const auto wid = mRenderInstanceWidgets.at(i);
        if (wid == activeWid) { continue; }
        delete wid;
    }
}

void RenderWidget::write(eWriteStream &dst) const
{
    dst << mRenderInstanceWidgets.count();
    for (const auto widget : mRenderInstanceWidgets) {
        widget->write(dst);
    }
}

void RenderWidget::read(eReadStream &src)
{
    int nWidgets; src >> nWidgets;
    for (int i = 0; i < nWidgets; i++) {
        const auto wid = new RenderInstanceWidget(nullptr, this);
        wid->read(src);
        addRenderInstanceWidget(wid);
    }
}

void RenderWidget::updateRenderSettings()
{
    for (const auto &wid: mRenderInstanceWidgets) {
        wid->updateRenderSettings();
    }
}

void RenderWidget::render(RenderInstanceSettings &settings)
{
    const RenderSettings &renderSettings = settings.getRenderSettings();
    // 0..span range: a single-frame render (min == max) would put
    // QProgressBar into busy mode
    mProgressBase = renderSettings.fMinFrame;
    mRenderProgressBar->setRange(0, qMax(1, renderSettings.fMaxFrame -
                                         renderSettings.fMinFrame));
    mRenderProgressBar->setValue(0);
    handleRenderState(RenderState::waiting);
    // drop the previous item's connections first - they used to
    // accumulate on every re-render of the same queue item
    releaseCurrentRenderedSettings();
    mCurrentRenderedSettings = &settings;

    connect(&settings, &RenderInstanceSettings::renderFrameChanged,
            this, &RenderWidget::setRenderedFrame, Qt::UniqueConnection);
    connect(&settings, &RenderInstanceSettings::stateChanged,
            this, &RenderWidget::handleRenderState, Qt::UniqueConnection);

    // the scene we want to render MUST be visible!
    // this is a workaround until we detach the renderer from the app
    const auto lay = MainWindow::sGetInstance()->getLayoutHandler();
    lay->setCurrentScene(settings.getTargetCanvas());

    // set correct resolution; not a user edit - keep it off the undo
    // stack so queue rendering does not mark the document dirty
    if (const auto canvas = settings.getTargetCanvas()) {
        const auto block = canvas->blockUndoRedo();
        canvas->setResolution(renderSettings.fResolution);
    }

    // give the ui time to update before renderer starts; a Stop or
    // queue clear inside this second must not launch a ghost render
    QTimer::singleShot(1000, this, [this, settingsPtr = &settings]() {
        if (mCurrentRenderedSettings.isNull() ||
            mCurrentRenderedSettings != settingsPtr) { return; }
        RenderHandler::sInstance->renderFromSettings(settingsPtr);
    });
}

void RenderWidget::render()
{
    int c = 0;
    for (RenderInstanceWidget *wid : mRenderInstanceWidgets) {
        if (!wid->isChecked()) { continue; }
        mAwaitingSettings << wid;
        wid->getSettings().setCurrentState(RenderState::waiting);
        c++;
    }
    if (c > 0) { handleRenderState(RenderState::waiting); }
    else { handleRenderState(RenderState::none); }
    sendNextForRender();
}

void RenderWidget::stopRendering()
{
    clearAwaitingRender();
    VideoEncoder::sInterruptEncoding();
    releaseCurrentRenderedSettings();
}

void RenderWidget::clearAwaitingRender()
{
    for (RenderInstanceWidget *wid : mAwaitingSettings) {
        wid->getSettings().setCurrentState(RenderState::none);
    }
    handleRenderState(RenderState::none);
    mAwaitingSettings.clear();
}

void RenderWidget::sendNextForRender()
{
    while (!mAwaitingSettings.isEmpty()) {
        const auto wid = mAwaitingSettings.takeFirst();
        if (wid->isChecked() && wid->getSettings().getTargetCanvas()) {
            wid->setDisabled(true);
            render(wid->getSettings());
            return;
        }
    }
}

int RenderWidget::count()
{
    return mRenderInstanceWidgets.count();
}

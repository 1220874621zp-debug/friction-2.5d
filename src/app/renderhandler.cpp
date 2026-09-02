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

#include "renderhandler.h"
#include "videoencoder.h"
#include "memoryhandler.h"
#include "Private/Tasks/taskscheduler.h"
#include "hardwareinfo.h"
#include "canvas.h"
#include "Sound/soundcomposition.h"
#include "CacheHandlers/soundcachecontainer.h"
#include "CacheHandlers/sceneframecontainer.h"
#include "Private/document.h"

RenderHandler* RenderHandler::sInstance = nullptr;

RenderHandler::RenderHandler(Document &document,
                             AudioHandler& audioHandler,
                             VideoEncoder& videoEncoder,
                             MemoryHandler& memoryHandler) :
    mDocument(document),
    mAudioHandler(audioHandler),
    mVideoEncoder(videoEncoder) {
    Q_ASSERT(!sInstance);
    sInstance = this;

    connect(&memoryHandler, &MemoryHandler::allMemoryUsed,
            this, &RenderHandler::outOfMemory);

    mPreviewFPSTimer = new QTimer(this);
    mPipelineTimer = new QTimer(this);
    // low-rate keepalive/watchdog only - the hot path is event-driven
    // through Canvas::sceneFrameCached
    mPipelineTimer->setInterval(25);
    connect(mPipelineTimer, &QTimer::timeout,
            this, &RenderHandler::pipelineTick);
    mPreviewFPSTimer->setTimerType(Qt::PreciseTimer);
    connect(mPreviewFPSTimer, &QTimer::timeout,
            this, &RenderHandler::nextPreviewFrame);
    connect(mPreviewFPSTimer, &QTimer::timeout,
            this, &RenderHandler::audioPushTimerExpired);

    // keepalive during output rendering: when the backlog cap pauses frame
    // advancement, the task underflow/all-finished callbacks no longer fire
    // on their own, so poll to resume once the encoder drains the backlog
    mBacklogTimer = new QTimer(this);
    connect(mBacklogTimer, &QTimer::timeout, this, [this]() {
        if(mCurrentRenderSettings) nextSaveOutputFrame();
    });
    connect(audioHandler.audioOutput(), &QAudioOutput::notify,
            this, &RenderHandler::audioPushTimerExpired);

    const auto vidEmitter = videoEncoder.getEmitter();
//    connect(vidEmitter, &VideoEncoderEmitter::encodingStarted,
//            this, &SceneWindow::leaveOnlyInterruptionButtonsEnabled);
    connect(vidEmitter, &VideoEncoderEmitter::encodingFinished,
            this, &RenderHandler::interruptOutputRendering);
    connect(vidEmitter, &VideoEncoderEmitter::encodingInterrupted,
            this, &RenderHandler::interruptOutputRendering);
    connect(vidEmitter, &VideoEncoderEmitter::encodingFailed,
            this, &RenderHandler::interruptOutputRendering);
    connect(vidEmitter, &VideoEncoderEmitter::encodingStartFailed,
            this, &RenderHandler::interruptOutputRendering);
}

void RenderHandler::renderFromSettings(RenderInstanceSettings * const settings) {
    setCurrentScene(settings->getTargetCanvas());
    if(VideoEncoder::sStartEncoding(settings)) {
        mSavedCurrentFrame = mCurrentScene->getCurrentFrame();
        mSavedResolutionFraction = mCurrentScene->getResolution();

        mCurrentRenderSettings = settings;
        const auto &renderSettings = settings->getRenderSettings();
        setFrameAction(renderSettings.fMinFrame);

        const qreal resolutionFraction = renderSettings.fResolution;
        mMinRenderFrame = renderSettings.fMinFrame;
        mMaxRenderFrame = renderSettings.fMaxFrame;
        const qreal fps = mCurrentScene->getFps();
        mMaxSoundSec = qFloor(mMaxRenderFrame/fps);

        const auto nextFrameFunc = [this]() {
            nextSaveOutputFrame();
        };
        TaskScheduler::sSetTaskUnderflowFunc(nextFrameFunc);
        TaskScheduler::sSetAllTasksFinishedFunc(nextFrameFunc);

        mCurrentRenderFrame = renderSettings.fMinFrame;
        mCurrRenderRange = {mCurrentRenderFrame, mCurrentRenderFrame};

        mCurrentEncodeFrame = mCurrentRenderFrame;
        mFirstEncodeSoundSecond = qFloor(mCurrentRenderFrame/fps);
        mCurrentEncodeSoundSecond = mFirstEncodeSoundSecond;
        if(!VideoEncoder::sEncodeAudio())
            mMaxSoundSec = mCurrentEncodeSoundSecond - 1;
        mCurrentScene->setMinFrameUseRange(mCurrentRenderFrame);
        mCurrentSoundComposition->setMinFrameUseRange(mCurrentRenderFrame);
        mCurrentSoundComposition->scheduleFrameRange({mCurrentRenderFrame,
                                                      mCurrentRenderFrame});
        mCurrentScene->anim_setAbsFrame(mCurrentRenderFrame);
        mCurrentScene->setOutputRendering(true);
        TaskScheduler::instance()->setAlwaysQue(true);
        // frame landings drive backlog draining and re-feeding
        connect(mCurrentScene, &Canvas::sceneFrameCached,
                this, &RenderHandler::onSceneFrameCached,
                Qt::ConnectionType(Qt::QueuedConnection | Qt::UniqueConnection));
        mBacklogTimer->start(100);
        //fitSceneToSize();
        if(!isZero6Dec(mSavedResolutionFraction - resolutionFraction)) {
            mCurrentScene->setResolution(resolutionFraction);
            mDocument.actionFinished();
        } else {
            nextCurrentRenderFrame();
            if(TaskScheduler::sAllQuedCpuTasksFinished()) {
                nextSaveOutputFrame();
            }
        }
    }
}

void RenderHandler::setPreviewFrame(const int &frame)
{
    mCurrentPreviewFrame = frame;
    // rebase the absolute-time clock: the playhead was repositioned
    // externally (scrub while paused), so playback must continue from
    // here - otherwise the next tick computes the target from the old
    // base and the playhead jumps back to the pre-scrub position
    mPreviewStartFrame = frame;
    mPreviewAccumMs = 0;
    if(mPreviewClock.isValid()) mPreviewClock.restart();
}

void RenderHandler::setLoop(const bool loop) {
    mLoop = loop;
}

void RenderHandler::setFrameAction(const int frame) {
    if(mCurrentScene) mCurrentScene->anim_setAbsFrame(frame);
    mDocument.actionFinished();
}

void RenderHandler::setCurrentScene(Canvas * const scene) {
    mCurrentScene = scene;
    mCurrentSoundComposition = scene ? scene->getSoundComposition() : nullptr;
}

void RenderHandler::nextCurrentRenderFrame() {
    auto& cacheHandler = mCurrentScene->getSceneFramesHandler();
    int newCurrentRenderFrame = cacheHandler.
            firstEmptyFrameAtOrAfter(mCurrentRenderFrame + 1);
    const bool allDone = newCurrentRenderFrame > mMaxRenderFrame;
    newCurrentRenderFrame = qMin(mMaxRenderFrame, newCurrentRenderFrame);
    const FrameRange newSoundRange = {mCurrentRenderFrame, newCurrentRenderFrame};
    mCurrentSoundComposition->scheduleFrameRange(newSoundRange);
    mCurrentSoundComposition->setMaxFrameUseRange(newCurrentRenderFrame);
    mCurrentScene->setMaxFrameUseRange(newCurrentRenderFrame);

    mCurrentRenderFrame = newCurrentRenderFrame;
    mCurrRenderRange.fMax = mCurrentRenderFrame;
    if(allDone) Document::sInstance->actionFinished();
    else setFrameAction(mCurrentRenderFrame);
}

void RenderHandler::setPreviewState(const PreviewState state)
{
    if (mPreviewState == state) { return; }
    if (mPreviewState == PreviewState::stopped) {
        if (state == PreviewState::playing) {
            // resuming straight from stopped (Space / resume button
            // after a full stop): entering playing without the
            // mPreviewing flag left every later pause/resume a silent
            // no-op - the preview kept rolling and Space seemed dead
            setPreviewing(true);
        } else {
            setRenderingPreview(true);
        }
    } else if (mPreviewState == PreviewState::rendering) {
        setRenderingPreview(false);
        if (state == PreviewState::playing) { setPreviewing(true); }
    } else if (state == PreviewState::stopped) {
        setPreviewing(false);
    }
    mPreviewState = state;
}

void RenderHandler::renderPreview() {
    setCurrentScene(mDocument.fActiveScene);
    if(!mCurrentScene) return;
    mSavedCurrentFrame = mCurrentScene->getCurrentFrame();

    const auto fIn = mCurrentScene->getFrameIn();
    const auto fOut = mCurrentScene->getFrameOut();

    mMinRenderFrame = mLoop ? (fIn.enabled? fIn.frame : mCurrentScene->getMinFrame()) - 1:
                          (fIn.enabled ? fIn.frame : mSavedCurrentFrame);

    mMaxRenderFrame = fOut.enabled ? fOut.frame : mCurrentScene->getMaxFrame();

    mCurrentRenderFrame = mMinRenderFrame;
    mCurrRenderRange = {mCurrentRenderFrame, mCurrentRenderFrame};
    mCurrentScene->setMinFrameUseRange(mCurrentRenderFrame);
    mCurrentSoundComposition->setMinFrameUseRange(mCurrentRenderFrame);

    // restart-equivalent staleness check: cached frames may embed
    // content from this scene or linked scenes edited after they were
    // cached - drop them once here instead of serving pre-edit pixels
    // (lazy, entry-time only; reactive invalidation feedback-loops
    // with rendering and stalls the warm-up)
    const qint64 effGen = mCurrentScene->effectiveContentGen();
    if(mCurrentScene->cacheGen() != effGen) {
        mCurrentScene->invalidateSceneFramesCache();
        mCurrentScene->setCacheGen(effGen);
    }

    setPreviewState(PreviewState::rendering);

    emit previewBeingRendered();

    // feed the first frames immediately; frame-landing events keep the
    // pool fed while previous frames are still rendering, the timer is
    // the keepalive/watchdog fallback
    mInFlightFrames.clear();
    mInFlightFedAgo.clear();
    mLastDiscardCount = mCurrentScene->renderDataDiscardCount();
    connect(mCurrentScene, &Canvas::sceneFrameCached,
            this, &RenderHandler::onSceneFrameCached,
            Qt::ConnectionType(Qt::QueuedConnection | Qt::UniqueConnection));
    mPipelineTimer->start();
    pipelineTick();
}

void RenderHandler::pipelineTick() {
    if(!mRenderingPreview || !mCurrentScene) return;
    const auto& cacheHandler = mCurrentScene->getSceneFramesHandler();
    // retire frames whose scene-frame container has landed in the cache
    while(!mInFlightFrames.isEmpty() &&
          cacheHandler.atFrame(mInFlightFrames.first()) != nullptr) {
        mInFlightFrames.removeFirst();
        mInFlightFedAgo.removeFirst();
    }
    // watchdog: an edit mid-warm-up bumps the state and the
    // completion check discards in-flight frames - they would never
    // land and retirement would stall forever. A rising discard count
    // proves a frame was dropped, so re-feed the oldest immediately
    // (queTasks is idempotent, the frame re-renders fresh); the tick
    // budget (~1s) remains as a fallback for silently dropped tasks.
    if(!mInFlightFrames.isEmpty()) {
        const int discards = mCurrentScene->renderDataDiscardCount();
        const bool discarded = discards != mLastDiscardCount;
        mLastDiscardCount = discards;
        if(discarded || ++mInFlightFedAgo.first() > 40) {
            const int stuck = mInFlightFrames.takeFirst();
            mInFlightFedAgo.removeFirst();
            setFrameAction(stuck);
            mInFlightFrames.prepend(stuck);
            mInFlightFedAgo.prepend(0);
        }
    }
    // feed up to the adaptive in-flight bound: the next frame assembles
    // while the previous one's tasks are still on the pool
    while(mInFlightFrames.count() < maxInFlightFrames()) {
        const int nextFrame = cacheHandler.firstEmptyFrameAtOrAfter(
                    mCurrentRenderFrame + 1);
        if(nextFrame > mMaxRenderFrame) break;
        const FrameRange newSoundRange = {mCurrentRenderFrame, nextFrame};
        mCurrentSoundComposition->scheduleFrameRange(newSoundRange);
        mCurrentSoundComposition->setMaxFrameUseRange(nextFrame);
        mCurrentScene->setMaxFrameUseRange(nextFrame);
        mCurrentRenderFrame = nextFrame;
        mCurrRenderRange.fMax = nextFrame;
        mInFlightFrames << nextFrame;
        mInFlightFedAgo << 0;
        setFrameAction(nextFrame);
    }
    // finished once nothing is in flight and no empty frame remains
    if(mInFlightFrames.isEmpty()) {
        const int nextFrame = cacheHandler.firstEmptyFrameAtOrAfter(
                    mCurrentRenderFrame + 1);
        if(nextFrame > mMaxRenderFrame) {
            mPipelineTimer->stop();
            playPreviewAfterAllTasksCompleted();
        }
    }
}

void RenderHandler::interruptPreview() {
    if(mRenderingPreview) interruptPreviewRendering();
    else if(mPreviewing) stopPreview();
}

void RenderHandler::outOfMemory() {
    if(mRenderingPreview) {
        // Play what has been rendered so far; if nothing is playable yet,
        // interrupt so the user does not get stuck in the rendering state.
        if(!playPreview()) interruptPreviewRendering();
    }
}

void RenderHandler::setRenderingPreview(const bool rendering) {
    mRenderingPreview = rendering;
    if(!rendering) {
        mPipelineTimer->stop();
        mInFlightFrames.clear();
        mInFlightFedAgo.clear();
    }
    if(mCurrentScene) mCurrentScene->setRenderingPreview(rendering);
    TaskScheduler::instance()->setAlwaysQue(rendering);
    // keep caches in memory while the preview is being rendered:
    // evicting them now only forces an async reload and the canvas
    // would flicker with blank frames
    MemoryHandler::sInstance->setAutoCheckPaused(rendering || mPreviewing);
}

void RenderHandler::setPreviewing(const bool previewing) {
    mPreviewing = previewing;
    if(mCurrentScene) mCurrentScene->setPreviewing(previewing);
    // keep caches in memory during playback for the same reason
    MemoryHandler::sInstance->setAutoCheckPaused(previewing || mRenderingPreview);
}

void RenderHandler::interruptPreviewRendering() {
    TaskScheduler::sClearAllFinishedFuncs();
    stopPreview();
}

void RenderHandler::interruptOutputRendering() {
    if(mCurrentScene) mCurrentScene->setOutputRendering(false);
    TaskScheduler::instance()->setAlwaysQue(false);
    TaskScheduler::sClearAllFinishedFuncs();
    mBacklogTimer->stop();
    stopPreview();
}

void RenderHandler::stopPreview() {
    if(mCurrentScene) {
        mCurrentScene->clearUseRange();
        setFrameAction(mSavedCurrentFrame);
        mCurrentScene->setSceneFrame(mSavedCurrentFrame);
        emit mCurrentScene->currentFrameChanged(mSavedCurrentFrame);
        emit mCurrentScene->requestUpdate();
    }

    mPreviewFPSTimer->stop();
    stopAudio();
    emit previewFinished();
    setPreviewState(PreviewState::stopped);
}

void RenderHandler::pausePreview() {
    if(mPreviewing) {
        // a paused preview is editable: bring the transform gizmos
        // back so pausing with Space returns to the editor without
        // jumping the playhead back to the pre-play frame
        if(mCurrentScene) mCurrentScene->setGizmosSuppressed(false);
        mAudioHandler.pauseAudio();
        mPreviewFPSTimer->stop();
        // freeze the playback clock: QElapsedTimer cannot be paused,
        // so bank the elapsed time and restart the clock on resume
        if(mPreviewClock.isValid()) {
            mPreviewAccumMs += mPreviewClock.elapsed();
            mPreviewClock.invalidate();
        }
        emit previewPaused();
        setPreviewState(PreviewState::paused);
    }
}

void RenderHandler::resumePreview() {
    if(mPreviewing) {
        mAudioHandler.resumeAudio();
        mPreviewClock.start();
        mPreviewFPSTimer->start();
        emit previewBeingPlayed();
        setPreviewState(PreviewState::playing);
    }
}

void RenderHandler::playPreviewAfterAllTasksCompleted() {
    if(mRenderingPreview) {
        TaskScheduler::sSetTaskUnderflowFunc(nullptr);
        Document::sInstance->actionFinished();
        if(TaskScheduler::sAllTasksFinished()) {
            if(!playPreview()) interruptPreviewRendering();
        } else {
            TaskScheduler::sSetAllTasksFinishedFunc([this]() {
                if(!playPreview()) interruptPreviewRendering();
            });
        }
    }
}

bool RenderHandler::playPreview() {
    // the warm-cache path (spaceToggle) calls playPreview directly,
    // bypassing renderPreview - mCurrentScene may still point at the
    // scene previewed BEFORE a scene switch, advancing that scene
    // while the active scene's canvas and playhead never move
    setCurrentScene(mDocument.fActiveScene);
    if(!mCurrentScene) return false;
    // direct start (no cache warm-up preceding): anchor the start
    // frame and the render frontier to where the user actually is -
    // mSavedCurrentFrame / mCurrentRenderFrame would otherwise keep
    // stale values from an earlier warm-up, starting playback from
    // that old position and capping it at the old frontier
    if(!mRenderingPreview) {
        mSavedCurrentFrame = mCurrentScene->getCurrentFrame();
        const auto fOutDirect = mCurrentScene->getFrameOut();
        mMaxRenderFrame = fOutDirect.enabled ?
                    fOutDirect.frame : mCurrentScene->getMaxFrame();
        mCurrentRenderFrame = mMaxRenderFrame;
    }
    //setFrameAction(mSavedCurrentFrame);
    TaskScheduler::sClearAllFinishedFuncs();
    const auto fIn = mCurrentScene->getFrameIn();
    const auto fOut = mCurrentScene->getFrameOut();
    const int minPreviewFrame = fIn.enabled? (fIn.frame < mSavedCurrentFrame && mSavedCurrentFrame < mCurrentRenderFrame ? mSavedCurrentFrame : fIn.frame) : mSavedCurrentFrame;
    const int maxPreviewFrame = qMin(mMaxRenderFrame, mCurrentRenderFrame);
    if(minPreviewFrame >= maxPreviewFrame) return false;
    mMinPreviewFrame = mLoop ? (fIn.enabled? fIn.frame : mCurrentScene->getMinFrame()) : (fIn.enabled? (fIn.frame < minPreviewFrame && minPreviewFrame < mCurrentRenderFrame ? minPreviewFrame : fIn.frame) : minPreviewFrame);
    mMaxPreviewFrame = fOut.enabled ? fOut.frame : maxPreviewFrame;
    // Reload cached frames that were swapped out to tmp files while the
    // preview was stopped (memory pressure evicts them between runs);
    // the tmp files hold raw RGBA so the reloads are fast and the frames
    // are back in memory before the playhead reaches them.
    mCurrentScene->scheduleLoadMissingSceneFrames(minPreviewFrame,
                                                  maxPreviewFrame);
    mCurrentPreviewFrame = minPreviewFrame;
    mCurrentScene->setSceneFrame(mCurrentPreviewFrame);

    setPreviewState(PreviewState::playing);

    startAudio();

    // absolute-time base: nextPreviewFrame() maps elapsed wall time
    // to a target frame, so tick with a floor-truncated interval
    // (slightly fast) - early ticks are no-ops, late ticks skip frames
    mPreviewStartFrame = mCurrentPreviewFrame;
    mPreviewAccumMs = 0;
    mPreviewClock.start();
    const int mSecInterval = qMax(1, int(1000/mCurrentScene->getFps()));
    mPreviewFPSTimer->setInterval(mSecInterval);
    mPreviewFPSTimer->start();
    emit previewBeingPlayed();
    emit mCurrentScene->requestUpdate();
    return true;
}

void RenderHandler::nextPreviewRenderFrame() {
    if(!mRenderingPreview) return;
    if(mCurrentRenderFrame >= mMaxRenderFrame) {
        playPreviewAfterAllTasksCompleted();
    } else {
        nextCurrentRenderFrame();
        if(TaskScheduler::sAllTasksFinished()) {
            nextPreviewRenderFrame();
        }
    }
}

void RenderHandler::nextPreviewFrame() {
    if(!mCurrentScene) return;
    const qreal fps = mCurrentScene->getFps();
    if(fps <= 0) return;
    // map elapsed wall time to the target frame; ticks that arrive
    // before the next frame is due are no-ops, ticks that arrive late
    // skip intermediate frames so playback never falls behind audio
    qint64 elapsed = mPreviewAccumMs;
    if(mPreviewClock.isValid()) elapsed += mPreviewClock.elapsed();
    int target = mPreviewStartFrame + int(elapsed*fps/1000);
    if(target <= mCurrentPreviewFrame) return;
    if(target > mMaxPreviewFrame) {
        if(mLoop) {
            const int span = mMaxPreviewFrame - mMinPreviewFrame + 1;
            if(span > 0) {
                target = mMinPreviewFrame + (target - mMinPreviewFrame)%span;
            } else {
                target = mMinPreviewFrame;
            }
            mPreviewStartFrame = target;
            mPreviewAccumMs = 0;
            mPreviewClock.restart();
            stopAudio();
            startAudio();
        } else {
            // show the final frame, then stop
            if(mCurrentPreviewFrame < mMaxPreviewFrame) {
                mCurrentPreviewFrame = mMaxPreviewFrame;
                mCurrentScene->anim_setAbsFrame(mCurrentPreviewFrame);
                mCurrentScene->setMinFrameUseRange(mCurrentPreviewFrame);
                emit mCurrentScene->requestUpdate();
            }
            stopPreview();
            return;
        }
    }
    mCurrentPreviewFrame = target;
    // anim_setAbsFrame updates the canvas's internal frame counter
    // (drives the playhead drawing) AND displays the cached frame
    // when available - setSceneFrame only displayed, leaving the
    // playhead frozen at the pre-playback position
    mCurrentScene->anim_setAbsFrame(mCurrentPreviewFrame);
    if(!mLoop) mCurrentScene->setMinFrameUseRange(mCurrentPreviewFrame);
    emit mCurrentScene->requestUpdate();
}

void RenderHandler::finishEncoding() {
    TaskScheduler::sClearAllFinishedFuncs();
    mBacklogTimer->stop();
    mCurrentRenderSettings = nullptr;
    mCurrentScene->setOutputRendering(false);
    TaskScheduler::instance()->setAlwaysQue(false);
    setFrameAction(mSavedCurrentFrame);
    if(!isZero4Dec(mSavedResolutionFraction - mCurrentScene->getResolution())) {
        mCurrentScene->setResolution(mSavedResolutionFraction);
    }
    mCurrentSoundComposition->clearUseRange();
    VideoEncoder::sFinishEncoding();
    // actually release the rendered frames: without this they stay in RAM
    // until the next memory-pressure pass, so a second render of the same
    // scene starts with the previous run's whole output still resident
    // (second render then hits CRITICAL memory state and deadlocks)
    mCurrentScene->getSceneFramesHandler().clearUseRange();
    mCurrentScene->getSceneFramesHandler().clear();
    mDocument.actionFinished();
}

int RenderHandler::maxBacklogFrames() const {
    const qint64 totRamBytes = qint64(HardwareInfo::sRamKB().fValue) * 1024;
    const qint64 budget = qMin<qint64>(1536LL * 1024 * 1024,
                                       totRamBytes / 4);
    const auto &renSettings = mCurrentRenderSettings->getRenderSettings();
    const qint64 frameBytes = qint64(qMax(1, renSettings.fVideoWidth)) *
                              qint64(qMax(1, renSettings.fVideoHeight)) * 4;
    return int(qMax<qint64>(8, budget / frameBytes));
}

int RenderHandler::maxInFlightFrames() const {
    if(!mCurrentScene) return 2;
    // each in-flight frame pins one full frame buffer (plus its render
    // data) in RAM, so scale the bound with what a 128MB budget buys
    // at the current effective frame size: ~4 for HD and below, ~3 for
    // 4K, 2 for 8K - below 2 the pool starves at frame boundaries,
    // beyond 4 the extra lookahead stops paying off
    constexpr qint64 budgetBytes = 128LL*1024*1024;
    const qreal resFrac = qMax<qreal>(1, mCurrentScene->getResolution())/100;
    const qint64 w = qint64(mCurrentScene->getCanvasWidth()*resFrac);
    const qint64 h = qint64(mCurrentScene->getCanvasHeight()*resFrac);
    const qint64 frameBytes = qMax<qint64>(1, w*h*4);
    return int(qBound<qint64>(2, budgetBytes/frameBytes, 4));
}

void RenderHandler::onSceneFrameCached() {
    if(mRenderingPreview) pipelineTick();
    else if(mCurrentRenderSettings) nextSaveOutputFrame();
}

void RenderHandler::nextSaveOutputFrame() {
    if(!mCurrentRenderSettings || !mCurrentScene) return;
    const auto& sCacheHandler = mCurrentSoundComposition->getCacheHandler();
    const qreal fps = mCurrentScene->getFps();
    const int sampleRate = eSoundSettings::sSampleRate();
    while(mCurrentEncodeSoundSecond <= mMaxSoundSec) {
        const auto cont = sCacheHandler.atFrame(mCurrentEncodeSoundSecond);
        if(!cont) break;
        const auto sCont = cont->ref<SoundCacheContainer>();
        const auto samples = sCont->getSamples();
        if(mCurrentEncodeSoundSecond == mFirstEncodeSoundSecond) {
            const int minSample = qRound(mMinRenderFrame*sampleRate/fps);
            const int max = samples->fSampleRange.fMax;
            VideoEncoder::sAddCacheContainerToEncoder(
                        samples->mid({minSample, max}));
        } else {
            VideoEncoder::sAddCacheContainerToEncoder(
                        enve::make_shared<Samples>(samples));
        }
        mCurrentEncodeSoundSecond++;
    }
    if(mCurrentEncodeSoundSecond > mMaxSoundSec) VideoEncoder::sAllAudioProvided();

    const auto& cacheHandler = mCurrentScene->getSceneFramesHandler();
    while(mCurrentEncodeFrame <= mMaxRenderFrame) {
        const auto cont = cacheHandler.atFrame(mCurrentEncodeFrame);
        if(!cont) break;
        VideoEncoder::sAddCacheContainerToEncoder(cont->ref<SceneFrameContainer>());
        mCurrentEncodeFrame = cont->getRangeMax() + 1;
    }

    //mCurrentScene->renderCurrentFrameToOutput(*mCurrentRenderSettings);
    if(mCurrentRenderFrame >= mMaxRenderFrame) {
        if(mCurrentEncodeSoundSecond <= mMaxSoundSec) return;
        if(mCurrentEncodeFrame <= mMaxRenderFrame) return;
        TaskScheduler::sSetTaskUnderflowFunc(nullptr);
        Document::sInstance->actionFinished();
        if(TaskScheduler::sAllTasksFinished()) {
            finishEncoding();
        } else {
            TaskScheduler::sSetAllTasksFinishedFunc([this]() {
                finishEncoding();
            });
        }
    } else {
        // multi-frame in-flight feed: assemble the next frames while
        // the previous ones' tasks are still on the pool instead of
        // waiting for every task of a frame to finish - the pool no
        // longer drains at frame boundaries. Two bounds apply: the
        // backlog cap keeps rendered-but-unencoded frames within the
        // memory budget (render is multi-threaded, encoding is not, so
        // the backlog would otherwise grow unbounded), and the
        // per-pass cap keeps the event loop responsive - frame-landing
        // and underflow callbacks re-enter and continue feeding
        for(int fed = 0; fed < 8; fed++) {
            const auto useRange =
                    mCurrentScene->getSceneFramesHandler().useRange();
            const int encodedUpTo = useRange.isValid() ?
                        useRange.fMin - 1 : mMinRenderFrame - 1;
            const int backlog = mCurrentRenderFrame - encodedUpTo;
            if(backlog >= maxBacklogFrames()) return; // encoder drains, timer re-invokes
            if(mCurrentRenderFrame >= mMaxRenderFrame) break;
            mCurrentRenderSettings->setCurrentRenderFrame(mCurrentRenderFrame);
            nextCurrentRenderFrame();
            // pool busy with at least two frames in flight: enough
            // lookahead for this pass, landing events will re-feed
            if(!TaskScheduler::sAllTasksFinished() && fed >= 1) return;
        }
        if(TaskScheduler::sAllTasksFinished()) {
            nextSaveOutputFrame();
        }
    }
}

void RenderHandler::startAudio() {
    mAudioHandler.startAudio();
    if(mCurrentSoundComposition)
        mCurrentSoundComposition->start(mCurrentPreviewFrame);
    audioPushTimerExpired();
}

void RenderHandler::stopAudio() {
    mAudioHandler.stopAudio();
    if(mCurrentSoundComposition) mCurrentSoundComposition->stop();
}

void RenderHandler::audioPushTimerExpired() {
    if(!mCurrentSoundComposition) return;
    while(auto request = mAudioHandler.dataRequest()) {
        const qint64 len = mCurrentSoundComposition->read(
                    request.fData, request.fSize);
        if(len <= 0) break;
        request.fSize = int(len);
        mAudioHandler.provideData(request);
    }
}

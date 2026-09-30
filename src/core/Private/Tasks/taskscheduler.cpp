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

#include "taskscheduler.h"

#include "canvas.h"
#include "execcontroller.h"
#include "gputaskexecutor.h"
#include "taskexecutor.h"
#include "complextask.h"
#include "Private/document.h"
#include "Boxes/boxrenderdata.h"

#include <QTimer>
#include <QDateTime>

TaskScheduler *TaskScheduler::sInstance = nullptr;

namespace {
// a healthy batch drains within milliseconds; several seconds without a
// single take means the batch is wedged (its tasks can never become ready)
const qint64 kStuckQueLimitMs = 5000;
const int kStuckQueWatchdogIntervalMs = 2000;
// repeated reports of the same stall are throttled to this rate
const qint64 kStuckQueLogThrottleMs = 10000;
// set while the user is interacting with a canvas
bool sInteractionFlag = false;
}

TaskScheduler::TaskScheduler() {
    Q_ASSERT(!sInstance);
    sInstance = this;
    qRegisterMetaType<stdsptr<eTask>>();
    const int numberThreads = qMax(1, QThread::idealThreadCount());
    for(int i = 0; i < numberThreads; i++) {
        const auto taskExecutor = std::make_shared<CpuExecController>(this);
        connect(taskExecutor.get(), &ExecController::finishedTaskSignal,
                this, &TaskScheduler::afterCpuGpuTaskFinished);

        mCpuExecs << taskExecutor;
    }

    mHddExec = std::make_shared<HddExecController>(this);
    connect(mHddExec.get(), &ExecController::finishedTaskSignal,
            this, &TaskScheduler::afterHddTaskFinished);

    mGpuExec = std::make_shared<GpuExecController>(this);
    connect(mGpuExec.get(), &ExecController::finishedTaskSignal,
            this, &TaskScheduler::afterCpuGpuTaskFinished);

    // stalled-queue watchdog: the render pipeline is a closed loop
    // (paint -> feed -> render -> repaint) and a batch that never empties
    // keeps overflowed() true forever, which stops the loop from being fed
    // at all - newly drawn content then never appears again. Nothing else
    // observes this condition, so poll for it.
    mStuckQueWatchdog = new QTimer(this);
    mStuckQueWatchdog->setInterval(kStuckQueWatchdogIntervalMs);
    connect(mStuckQueWatchdog, &QTimer::timeout,
            this, &TaskScheduler::checkForStuckQues);
    mStuckQueWatchdog->start();
}

TaskScheduler::~TaskScheduler()
{
    if(mStuckQueWatchdog) mStuckQueWatchdog->stop();
    mGpuExec->stop(); // workaround for deadlock, waiting will not work here
    // may result in "QThread: Destroyed while thread is still running" during shutdown

    for (const auto& exec : mCpuExecs) { exec->stopAndWait(); }
    mHddExec->stopAndWait();
}

void TaskScheduler::sSetTaskUnderflowFunc(const Func& func) {
    sInstance->setTaskUnderflowFunc(func);
}

void TaskScheduler::sSetAllTasksFinishedFunc(const Func& func) {
    sInstance->setAllTasksFinishedFunc(func);
}

void TaskScheduler::sClearAllFinishedFuncs() {
    sSetTaskUnderflowFunc(nullptr);
    sSetAllTasksFinishedFunc(nullptr);
}

bool TaskScheduler::sAllTasksFinished() {
    return sInstance->allQuedTasksFinished();
}

bool TaskScheduler::sAllQuedCpuTasksFinished() {
    return sInstance->allQuedCpuTasksFinished();
}

void TaskScheduler::sClearTasks() {
    sInstance->clearTasks();
}

void TaskScheduler::initializeGpu() {
    try {
        mGpuExec->initialize();
    } catch(...) {
        RuntimeThrow("Failed to initialize GPU execution controller.");
    }
}

void TaskScheduler::queHddTask(const stdsptr<eTask>& task) {
    mQuedHddTasks << task;
    processNextQuedHddTask();
}

void TaskScheduler::queCpuTask(const stdsptr<eTask>& task) {
    mQuedCGTasks.addTask(task);
    // nested que during the scene-assembly iteration (queScheduled-
    // CpuTasks runs between beginQue/endQue): addTask appends to the
    // CURRENT que which is safe, but processing from here would
    // recurse into processNextTasks -> underflow -> updateScenes ->
    // queTasks and re-enter the queue structures mid-iteration
    // (0xC0000005 in QList detach). Defer to the outer loop's
    // endQue + processNextTasks
    if(mCpuQueing) return;
    // in the critical memory state only the cheap, immediately visible
    // tasks may start; the rest waits for the state to lift (where
    // finishCriticalMemoryState() refeeds everything)
    if(mCriticalMemoryState && !task->allowedInCriticalMemory()) return;
    if(task->readyToBeProcessed()) {
        if(task->hardwareSupport() == HardwareSupport::cpuOnly ||
           !processNextQuedGpuTask()) {
            processNextQuedCpuTask();
        }
    }
}

void TaskScheduler::clearTasks() {
    mQuedCGTasks.clear();

    for(const auto& hddTask : mQuedHddTasks)
        hddTask->cancel();
    mQuedHddTasks.clear();

    callAllTasksFinishedFunc();
}

bool TaskScheduler::overflowed() const {
    const int nQues = mQuedCGTasks.countQues();
    const int maxQues = mAlwaysQue ? mCpuExecs.count() : 1;
    return nQues >= maxQues;
}

void TaskScheduler::callAllTasksFinishedFunc() const {
    if(allQuedTasksFinished()) {
        if(mAllTasksFinishedFunc) mAllTasksFinishedFunc();
        emit finishedAllQuedTasks();
    }
}

bool TaskScheduler::shouldQueMoreCpuTasks() const {
    // NOTE: this used to demand GpuTaskExecutor::sUsageCount() == 0, i.e.
    // a single in-flight GPU task stopped the CPU side from being fed at
    // all. Under external load (a screen recorder competing for the GPU)
    // that made renders arrive in bursts, so newly drawn shapes took
    // seconds to appear. A stalled GPU stage is already bounded by the
    // waiting-task count, which is what we check here.
    return !mCpuQueing && !overflowed() &&
            availableCpuThreads() > 0 &&
            (mAlwaysQue || GpuTaskExecutor::sWaitingTasks() < 8);
}

bool TaskScheduler::shouldQueMoreHddTasks() const {
    return !mCpuQueing && !overflowed() &&
            mQuedHddTasks.count() + HddTaskExecutor::sWaitingTasks() < 2;
}

void TaskScheduler::queTasks() {
    queScheduledCpuTasks();
    processNextQuedHddTask();
}

void TaskScheduler::sSetOutputRenderScene(Canvas * const scene) {
    sInstance->mOutputRenderScene = scene;
}

bool TaskScheduler::sOutputRenderActive() {
    return sInstance && !sInstance->mOutputRenderScene.isNull();
}

void TaskScheduler::queScheduledCpuTasks() {
    if(!mAlwaysQue && !shouldQueMoreCpuTasks()) return;
    mCpuQueing = true;
    mQuedCGTasks.beginQue();
    for(const auto& it : Document::sInstance->fVisibleScenes) {
        const auto scene = it.first;
        // NOTE: do NOT filter to the output target scene here - linked
        // scenes refresh their render data through their own queTasks,
        // and starving them froze link content mid-render (output video
        // showed stale frames while the preview animated fine)
        scene->queTasks();
    }
    mQuedCGTasks.endQue();
    mCpuQueing = false;

    if(!mQuedCGTasks.isEmpty()) processNextTasks();
}

void TaskScheduler::afterHddTaskFinished(const stdsptr<eTask>& finishedTask) {
    TaskExecutor::sTaskFinishSignals--;
    finishedTask->finishedProcessing();
    processNextTasks();
    if(!hddTaskBeingProcessed()) queTasks();
    callAllTasksFinishedFunc();
}

void TaskScheduler::processNextQuedHddTask() {
    bool finished = false;
    QList<stdsptr<eTask>> tasks;
    for(int i = 0; i < mQuedHddTasks.count(); i++) {
        const auto task = mQuedHddTasks.at(i);
        if(!task->readyToBeProcessed()) continue;
        task->aboutToProcess(Hardware::hdd);
        if(task->getState() > eTaskState::processing)
            finished = true;
        mQuedHddTasks.removeAt(i--);
        tasks << task;
    }
    if(!tasks.isEmpty()) HddTaskExecutor::sAddTasks(tasks);
    if(finished) processNextTasks();

    emit hddUsageChanged(busyHddThreads());
}

void TaskScheduler::processNextTasks() {
    // memory relief must keep flowing even in critical state: tmp saves
    // are the escape route out of memory pressure and reloads unblock the
    // encoder - blocking HDD dispatch here deadlocked output rendering
    // once memory filled up (render frozen at full RAM, zero disk I/O)
    processNextQuedHddTask();
    if(mCriticalMemoryState) {
        // The critical state used to stop every CPU/GPU dispatch. That is
        // too blunt for an interactive editor: the user is drawing right
        // now, and a small layer rasterization is cheap while blocking it
        // leaves the canvas showing nothing at all. Dispatch the tasks
        // that opt in (see eTask::allowedInCriticalMemory) and let the
        // rest wait for finishCriticalMemoryState().
        processNextQuedCpuCriticalOnly();
        return;
    }
    processNextQuedGpuTask();
    processNextQuedCpuTask();
    if(mTaskUnderflowFunc) {
        if(shouldQueMoreCpuTasks() || shouldQueMoreHddTasks()) {
            mTaskUnderflowFunc();
        }
    }
}

void TaskScheduler::processNextQuedCpuCriticalOnly() {
    bool finished = false;
    QList<stdsptr<eTask>> tasks;
    for(int i = 0; i < 2; i++) {
        const auto task = mQuedCGTasks.takeQuedForCriticalProcessing();
        if(!task) break;
        task->aboutToProcess(Hardware::cpu);
        if(task->getState() > eTaskState::processing) {
            finished = true;
            i--; continue;
        }
        tasks << task;
    }
    if(!tasks.isEmpty()) CpuTaskExecutor::sAddTasks(tasks);
    if(finished) processNextTasks();
    emit cpuUsageChanged(busyCpuThreads());
}

void TaskScheduler::checkForStuckQues() {
    if(mQuedCGTasks.isEmpty()) {
        mStuckQueStrikes = 0;
        return;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if(!mQuedCGTasks.hasStuckQue(now, kStuckQueLimitMs)) {
        mStuckQueStrikes = 0;
        return;
    }
    const auto diag = mQuedCGTasks.describeStuckQues(now);
    const int reclaimed = mQuedCGTasks.discardDeadTasks();
    if(reclaimed > 0) {
        qWarning() << "QUE-WATCHDOG: reclaimed" << reclaimed
                   << "dead tasks from a stalled batch" << diag;
        mStuckQueStrikes = 0;
        queTasks();
        processNextTasks();
        callAllTasksFinishedFunc();
        return;
    }
    if(mStuckQueStrikes < 1) {
        // first sighting: report it, but do not throw work away yet - the
        // tasks may be waiting on a slow (but healthy) disk load
        mStuckQueStrikes++;
        if(now - mStuckQueLastLogMs > kStuckQueLogThrottleMs) {
            mStuckQueLastLogMs = now;
            qWarning() << "QUE-WATCHDOG: batch stalled for more than"
                       << kStuckQueLimitMs << "ms" << diag
                       << "cpuBusy=" << busyCpuThreads()
                       << "hddBusy=" << busyHddThreads()
                       << "gpuBusy=" << GpuTaskExecutor::sUsageCount();
        }
        return;
    }
    // Confirmed stall. Only act when nothing is running at all: if a pool
    // is busy the batch is probably waiting on real (slow) work.
    const bool idle = busyCpuThreads() == 0 && busyHddThreads() == 0 &&
                      GpuTaskExecutor::sUsageCount() == 0;
    if(!idle) {
        if(now - mStuckQueLastLogMs > kStuckQueLogThrottleMs) {
            mStuckQueLastLogMs = now;
            qWarning() << "QUE-WATCHDOG: stalled batch kept, pool still busy"
                       << diag;
        }
        return;
    }
    const int dropped = mQuedCGTasks.dropStuckQues(now, kStuckQueLimitMs);
    mStuckQueStrikes = 0;
    qWarning() << "QUE-WATCHDOG: dropped" << dropped
               << "stalled batch(es), refeeding the pipeline" << diag;
    queTasks();
    processNextTasks();
    callAllTasksFinishedFunc();
}

void TaskScheduler::sSetInteractionActive(const bool active) {
    sInteractionFlag = active;
}

bool TaskScheduler::sInteractionActive() {
    return sInteractionFlag;
}

bool TaskScheduler::processNextQuedGpuTask() {
    bool finished = false;
    QList<stdsptr<eTask>> tasks;
    const int count = 3 - GpuTaskExecutor::sWaitingTasks();
    for(int i = 0; i < count; i++) {
        const auto task = mQuedCGTasks.takeQuedForGpuProcessing();
        if(!task) break;
        task->aboutToProcess(Hardware::gpu);
        if(task->getState() > eTaskState::processing) {
            finished = true;
            i--; continue;
        }
        tasks << task;
    }
    if(!tasks.isEmpty()) GpuTaskExecutor::sAddTasks(tasks);
    if(finished) processNextTasks();

    emit gpuUsageChanged(GpuTaskExecutor::sUsageCount() > 0);
    return !tasks.isEmpty();
}

void TaskScheduler::afterCpuGpuTaskFinished(const stdsptr<eTask>& task) {
    TaskExecutor::sTaskFinishSignals--;
    task->finishedProcessing();
    processNextTasks();
    if(!cpuTasksBeingProcessed()) queTasks();
    callAllTasksFinishedFunc();
}

void TaskScheduler::setTaskUnderflowFunc(const Func& func) {
    mTaskUnderflowFunc = func;
}

void TaskScheduler::setAllTasksFinishedFunc(const Func& func) {
    mAllTasksFinishedFunc = func;
}

bool TaskScheduler::allQuedTasksFinished() const {
    return allQuedCpuTasksFinished() &&
           allQuedHddTasksFinished() &&
           allQuedGpuTasksFinished() &&
           TaskExecutor::sTaskFinishSignals == 0;
}

bool TaskScheduler::allQuedGpuTasksFinished() const {
    return mQuedCGTasks.isEmpty() && GpuTaskExecutor::sUsageCount() == 0;
}

bool TaskScheduler::allQuedCpuTasksFinished() const {
    return mQuedCGTasks.isEmpty() && !cpuTasksBeingProcessed();
}

bool TaskScheduler::allQuedHddTasksFinished() const {
    return mQuedHddTasks.isEmpty() && !hddTaskBeingProcessed();
}

bool TaskScheduler::cpuTasksBeingProcessed() const {
    return busyCpuThreads() > 0;
}

bool TaskScheduler::hddTaskBeingProcessed() const {
    return busyHddThreads() > 0;
}

int TaskScheduler::busyHddThreads() const {
    return HddTaskExecutor::sUsageCount();
}

int TaskScheduler::busyCpuThreads() const {
    return CpuTaskExecutor::sUsageCount();
}

int TaskScheduler::availableCpuThreads() const {
    const int cap = eSettings::sInstance->fCpuThreadsCap;
    const int free = mCpuExecs.count() - busyCpuThreads();
    if(cap > 0) return qMin(free, cap);
    return free;
}

void TaskScheduler::setAlwaysQue(const bool alwaysQue) {
    mAlwaysQue = alwaysQue;
}

void TaskScheduler::addComplexTask(const qsptr<ComplexTask> &task) {
    if(task->done()) return;
    mComplexTasks << task;
    const QWeakPointer<ComplexTask> taskPtr = task;
    const auto deleter = [this, taskPtr]() {
        mComplexTasks.removeOne(taskPtr);
    };
    emit complexTaskAdded(task.data());
    connect(task.data(), &ComplexTask::canceled, this, deleter);
    connect(task.data(), &ComplexTask::finishedAll, this, deleter);
}

void TaskScheduler::enterCriticalMemoryState() {
    if(mCriticalMemoryState) return;
    mCriticalMemoryState = true;
}

void TaskScheduler::finishCriticalMemoryState() {
    if(!mCriticalMemoryState) return;
    mCriticalMemoryState = false;
    queTasks();
    processNextTasks();
}

void TaskScheduler::waitTillFinished() {
    if(allQuedTasksFinished()) return;
    QEventLoop loop;
    QObject::connect(this, &TaskScheduler::finishedAllQuedTasks,
                     &loop, &QEventLoop::quit, Qt::QueuedConnection);
    loop.exec();
}

void TaskScheduler::processNextQuedCpuTask() {
    bool finished = false;
    QList<stdsptr<eTask>> tasks;
    const int count = 3*mCpuExecs.count() - CpuTaskExecutor::sWaitingTasks();
    for(int i = 0; i < count; i++) {
        const auto task = mQuedCGTasks.takeQuedForCpuProcessing();
        if(!task) break;
        task->aboutToProcess(Hardware::cpu);
        if(task->getState() > eTaskState::processing) {
            finished = true;
            i--; continue;
        }
        tasks << task;
    }
    if(!tasks.isEmpty()) CpuTaskExecutor::sAddTasks(tasks);
    if(finished) processNextTasks();
    emit cpuUsageChanged(busyCpuThreads());
}

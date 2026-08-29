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

#include "memoryhandler.h"
#include "Boxes/boxrendercontainer.h"
#include "CacheHandlers/cachecontainer.h"
#include "GUI/mainwindow.h"
#include "Private/Tasks/taskscheduler.h"
#include <QDebug>
#include <QMetaType>

#ifdef Q_OS_MAC
#include <malloc/malloc.h>
#else
#include <malloc.h>
#endif

MemoryHandler *MemoryHandler::sInstance = nullptr;
Q_DECLARE_METATYPE(MemoryState)
Q_DECLARE_METATYPE(longB)
Q_DECLARE_METATYPE(intKB)

MemoryHandler::MemoryHandler(QObject * const parent) : QObject(parent) {
    Q_ASSERT(!sInstance);
    sInstance = this;

    mMemoryChekerThread = new QThread(this);
    mMemoryChecker = new MemoryChecker();
    mMemoryChecker->moveToThread(mMemoryChekerThread);
    qRegisterMetaType<MemoryState>();
    qRegisterMetaType<longB>();
    connect(mMemoryChecker, &MemoryChecker::handleMemoryState,
            this, &MemoryHandler::freeMemory);
    qRegisterMetaType<intKB>();
    connect(mMemoryChecker, &MemoryChecker::memoryCheckedKB,
            this, &MemoryHandler::memoryChecked);

    mTimer = new QTimer(this);
    connect(mTimer, &QTimer::timeout,
            mMemoryChecker, &MemoryChecker::checkMemory);
    mTimer->start(1000);
    mMemoryChekerThread->start();
}

MemoryHandler::~MemoryHandler() {
    mMemoryChekerThread->quit();
    mMemoryChekerThread->wait();
    sInstance = nullptr;
    delete mMemoryChecker;
}

void MemoryHandler::clearMemory() {
    freeMemory(NORMAL_MEMORY_STATE, longB(std::numeric_limits<qint64>::max()));
}

void MemoryHandler::setAutoCheckPaused(const bool paused) {
    if(paused) {
        mTimer->stop();
    } else {
        mTimer->start(1000);
    }
}

MemoryState MemoryHandler::sMemoryState() {
    return sInstance->mMemoryState;
}

void MemoryHandler::freeMemory(const MemoryState newState,
                               const longB &minFreeBytes) {
    if(newState != mMemoryState) {
        if(newState == NORMAL_MEMORY_STATE) {
            mTimer->setInterval(1000);
        } else if(newState >= VERY_LOW_MEMORY_STATE) {
            if(mMemoryState < VERY_LOW_MEMORY_STATE) {
                mTimer->setInterval(250);
            }
        } else {
            mTimer->setInterval(500);
        }
        if(mMemoryState == CRITICAL_MEMORY_STATE) {
            emit finishedCriticalState();
        } else if(newState == CRITICAL_MEMORY_STATE) {

        }
        mMemoryState = newState;
    }

    if(minFreeBytes.fValue <= 0) return;
    qint64 memToFree = minFreeBytes.fValue;
    // no per-poll logging here: under persistent EXTERNAL memory pressure
    // the state stays put and the count pins at the min working set, so
    // even a "state-changed or has-containers" gate reprints the same
    // line every 500ms and drowns the real diagnostics in the debug log
    // (user request: keep the log clean)
    // keep a minimum working set: when the system deficit is caused by
    // OTHER programs (often 1.5GB+), evicting our few hundred MB of
    // image caches achieves nothing except a black canvas - the images
    // get swapped to tmp, reload is async, and the user sees nothing.
    // If our total possible contribution is under 25% of the deficit,
    // the pressure is external: hold on to the last few containers so
    // the app stays usable
    const int minKeep = 16;
    // never evict mid-render: containers evicted while render tasks are
    // still running leave their dependent render chains waiting on an
    // async reload - the affected layers then show blank (dashed bounds
    // only) or stale-composited frames (visible color shifts for
    // blend-mode layers). Defer to the next poll instead.
    const auto sched = TaskScheduler::instance();
    if(sched && (sched->busyCpuThreads() > 0 ||
                 sched->busyHddThreads() > 0)) {
        emit memoryFreed();
        return;
    }
    while(memToFree > 0 && mDataHandler.count() > minKeep) {
        const auto contRaw = mDataHandler.takeFirst();
        // hold a strong reference while evicting: free_RAM_k() may
        // release the last owner of the container (e.g. noDataLeft_k
        // resetting the owning data handler), destroying it mid-call
        // and leaving the loop with a dangling pointer (crash inside
        // StdSelfRef::ref() / heap corruption)
        const auto cont = contRaw->ref<CacheContainer>();
        memToFree -= cont->free_RAM_k();
    }
    // Only a real critical memory state (as reported by the memory checker)
    // escalates to critical. Failing to free enough from our own caches
    // (e.g. system memory is consumed by other applications) must not
    // trigger allMemoryUsed in a loop, which used to break the preview
    // state machine (playback became unresponsive).
    if(newState == CRITICAL_MEMORY_STATE) {
        mMemoryState = CRITICAL_MEMORY_STATE;
        emit enteredCriticalState();
        emit allMemoryUsed();
    }
    emit memoryFreed();
}

void MemoryHandler::memoryChecked(const intKB memKb,
                                  const intKB totMemKb,
                                  const intKB usedKb)
{
    Q_UNUSED(memKb)
    Q_UNUSED(totMemKb)
    emit memoryUsed(intMB(usedKb));
}

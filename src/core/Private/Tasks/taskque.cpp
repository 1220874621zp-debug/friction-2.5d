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

#include "taskque.h"
#include "Private/esettings.h"
#include <QDateTime>

TaskQue::TaskQue() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    mCreatedMs = now;
    mLastProgressMs = now;
}

TaskQue::~TaskQue() {
    for(const auto& task : mCpuOnly) task->cancel();
    for(const auto& task : mCpuPreffered) task->cancel();
    for(const auto& task : mGpuPreffered) task->cancel();
    for(const auto& task : mGpuOnly) task->cancel();
}

int TaskQue::countQued() const {
    return mCpuOnly.count() + mCpuPreffered.count() +
           mGpuPreffered.count() + mGpuOnly.count();
}

bool TaskQue::allDone() const { return countQued() == 0; }

void TaskQue::addTask(const stdsptr<eTask> &task) {
    mLastProgressMs = QDateTime::currentMSecsSinceEpoch();
    const auto hwSupport = task->hardwareSupport();
    switch(eSettings::sInstance->fAccPreference) {
        case AccPreference::gpuStrongPreference:
            switch(hwSupport) {
                case HardwareSupport::gpuOnly:
                case HardwareSupport::gpuPreffered:
                case HardwareSupport::cpuPreffered:
                    mGpuOnly << task;
                    break;
                case HardwareSupport::cpuOnly:
                    mCpuOnly << task;
                    break;
            default:;
            }
            break;
        case AccPreference::gpuSoftPreference:
            switch(hwSupport) {
                case HardwareSupport::gpuOnly:
                case HardwareSupport::gpuPreffered:
                    mGpuOnly << task;
                    break;
                case HardwareSupport::cpuPreffered:
                    mCpuPreffered << task;
                    break;
                case HardwareSupport::cpuOnly:
                    mCpuOnly << task;
                    break;
            default:;
            }
            break;
        case AccPreference::defaultPreference:
            switch(hwSupport) {
                case HardwareSupport::gpuOnly:
                    mGpuOnly << task;
                    break;
                case HardwareSupport::gpuPreffered:
                    mGpuPreffered << task;
                    break;
                case HardwareSupport::cpuPreffered:
                    mCpuPreffered << task;
                    break;
                case HardwareSupport::cpuOnly:
                    mCpuOnly << task;
                    break;
            default:;
            }
            break;
        case AccPreference::cpuSoftPreference:
            switch(hwSupport) {
                case HardwareSupport::gpuOnly:
                    mGpuOnly << task;
                    break;
                case HardwareSupport::gpuPreffered:
                    mGpuPreffered << task;
                    break;
                case HardwareSupport::cpuPreffered:
                case HardwareSupport::cpuOnly:
                    mCpuOnly << task;
                    break;
            default:;
            }
            break;
        case AccPreference::cpuStrongPreference:
            switch(hwSupport) {
                case HardwareSupport::gpuOnly:
                    mGpuOnly << task;
                    break;
                case HardwareSupport::gpuPreffered:
                case HardwareSupport::cpuPreffered:
                case HardwareSupport::cpuOnly:
                    mCpuOnly << task;
                    break;
            default:;
            }
            break;
    }
}

stdsptr<eTask> TaskQue::takeTask(const QList<QList<stdsptr<eTask>>*> &lists,
                                const bool requireCriticalAllowed) {
    // two passes: interactive tasks (the user is waiting for exactly
    // those) win over the regular order, so a background backlog such as
    // preview warm-up or tmp reloads cannot starve the box being drawn
    for(const auto list : lists) {
        for(int i = 0; i < list->count(); i++) {
            const auto& task = list->at(i);
            if(!task || !task->readyToBeProcessed()) continue;
            if(!task->interactive()) continue;
            if(requireCriticalAllowed && !task->allowedInCriticalMemory()) continue;
            mLastProgressMs = QDateTime::currentMSecsSinceEpoch();
            return list->takeAt(i);
        }
    }
    for(const auto list : lists) {
        for(int i = 0; i < list->count(); i++) {
            const auto& task = list->at(i);
            if(!task || !task->readyToBeProcessed()) continue;
            if(requireCriticalAllowed && !task->allowedInCriticalMemory()) continue;
            mLastProgressMs = QDateTime::currentMSecsSinceEpoch();
            return list->takeAt(i);
        }
    }
    return nullptr;
}

// drop tasks canceled while sitting in the que (RenderDataHandler::
// cancelAll marks them in place) - used by TaskQue::flushCanceled
template <typename LIST>
static void purgeCanceled(LIST &list) {
    for(int i = 0; i < list.count(); i++) {
        if(list.at(i)->getState() == eTaskState::canceled) {
            list.removeAt(i);
            i--;
        }
    }
}

int TaskQue::flushCanceled() {
    const int before = countQued();
    purgeCanceled(mCpuOnly);
    purgeCanceled(mCpuPreffered);
    purgeCanceled(mGpuPreffered);
    purgeCanceled(mGpuOnly);
    return before - countQued();
}

stdsptr<eTask> TaskQue::takeQuedForCpuProcessing() {
    return takeTask({&mCpuOnly, &mCpuPreffered, &mGpuPreffered}, false);
}

stdsptr<eTask> TaskQue::takeQuedForGpuProcessing() {
    return takeTask({&mGpuOnly, &mGpuPreffered, &mCpuPreffered}, false);
}

stdsptr<eTask> TaskQue::takeQuedForCriticalProcessing() {
    // gpuOnly tasks are left alone: they need the GPU context, which this
    // path (CPU pool) does not provide
    return takeTask({&mCpuOnly, &mCpuPreffered, &mGpuPreffered}, true);
}

int TaskQue::discardDeadTasks() {
    int count = 0;
    const auto all = {&mGpuOnly, &mGpuPreffered, &mCpuPreffered, &mCpuOnly};
    for(const auto list : all) {
        for(int i = 0; i < list->count();) {
            const auto& task = list->at(i);
            const auto state = task ? task->getState() : eTaskState::canceled;
            if(state == eTaskState::canceled || state == eTaskState::finished) {
                list->removeAt(i);
                count++;
            } else {
                i++;
            }
        }
    }
    if(count > 0) mLastProgressMs = QDateTime::currentMSecsSinceEpoch();
    return count;
}

int TaskQue::countDeadTasks() {
    int count = 0;
    const auto all = {&mGpuOnly, &mGpuPreffered, &mCpuPreffered, &mCpuOnly};
    for(const auto list : all) {
        for(const auto& task : *list) {
            if(!task) { count++; continue; }
            const auto state = task->getState();
            if(state == eTaskState::canceled || state == eTaskState::finished)
                count++;
        }
    }
    return count;
}

int TaskQue::countBlockedTasks() {
    int count = 0;
    const auto all = {&mGpuOnly, &mGpuPreffered, &mCpuPreffered, &mCpuOnly};
    for(const auto list : all) {
        for(const auto& task : *list) {
            if(!task || !task->readyToBeProcessed()) count++;
        }
    }
    return count;
}

bool TaskQue::stuckSince(const qint64 nowMs, const qint64 limitMs) const {
    if(allDone()) return false;
    return nowMs - mLastProgressMs > limitMs;
}

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

TaskQue::TaskQue() {}

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

// stale-render cancellation drops tasks from the GUI thread while they
// may still sit in one of the ques: purge them on take, otherwise
// aboutToProcess would overwrite the canceled state and resurrect the
// task (its dependents were already canceled once - resurrecting ran
// the render anyway and its completion polluted the display path)
template <typename LIST>
static void purgeCanceled(LIST &list) {
    for(int i = 0; i < list.count(); i++) {
        if(list.at(i)->getState() == eTaskState::canceled) {
            list.removeAt(i);
            i--;
        }
    }
}

stdsptr<eTask> TaskQue::takeQuedForCpuProcessing() {
    purgeCanceled(mCpuOnly);
    purgeCanceled(mCpuPreffered);
    purgeCanceled(mGpuPreffered);
    purgeCanceled(mGpuOnly);
    for(int i = 0; i < mCpuOnly.count(); i++) {
        const auto& task = mCpuOnly.at(i);
        if(task->readyToBeProcessed())
            return mCpuOnly.takeAt(i);
    }
    for(int i = 0; i < mCpuPreffered.count(); i++) {
        const auto& task = mCpuPreffered.at(i);
        if(task->readyToBeProcessed())
            return mCpuPreffered.takeAt(i);
    }
    for(int i = 0; i < mGpuPreffered.count(); i++) {
        const auto& task = mGpuPreffered.at(i);
        if(task->readyToBeProcessed())
            return mGpuPreffered.takeAt(i);
    }
    return nullptr;
}

stdsptr<eTask> TaskQue::takeQuedForGpuProcessing() {
    purgeCanceled(mGpuOnly);
    purgeCanceled(mGpuPreffered);
    purgeCanceled(mCpuPreffered);
    purgeCanceled(mCpuOnly);
    for(int i = 0; i < mGpuOnly.count(); i++) {
        const auto& task = mGpuOnly.at(i);
        if(task->readyToBeProcessed())
            return mGpuOnly.takeAt(i);
    }
    for(int i = 0; i < mGpuPreffered.count(); i++) {
        const auto& task = mGpuPreffered.at(i);
        if(task->readyToBeProcessed())
            return mGpuPreffered.takeAt(i);
    }
    for(int i = 0; i < mCpuPreffered.count(); i++) {
        const auto& task = mCpuPreffered.at(i);
        if(task->readyToBeProcessed())
            return mCpuPreffered.takeAt(i);
    }
    return nullptr;
}

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

#ifndef TASKQUE_H
#define TASKQUE_H
#include "Tasks/updatable.h"

class CORE_EXPORT TaskQue {
    friend class TaskQueHandler;
public:
    explicit TaskQue();
    TaskQue(const TaskQue&) = delete;
    TaskQue& operator=(const TaskQue&) = delete;

    ~TaskQue();
protected:
    int countQued() const;
    // drop tasks canceled while sitting in the que (RenderDataHandler::
    // cancelAll marks them in place); returns how many were removed
    int flushCanceled();
    bool allDone() const;
    void addTask(const stdsptr<eTask>& task);

    stdsptr<eTask> takeQuedForCpuProcessing();
    stdsptr<eTask> takeQuedForGpuProcessing();
    // Critical-memory path: only tasks cheap enough to run while the
    // system is out of memory (see eTask::allowedInCriticalMemory).
    // gpuOnly tasks are excluded - this path dispatches to the CPU pool.
    stdsptr<eTask> takeQuedForCriticalProcessing();

    // Drops tasks that will never be processed again (canceled) so the
    // batch can still reach "all done" and release the scheduler's
    // overflow gate.
    int discardDeadTasks();
    int countDeadTasks();
    int countBlockedTasks();

    // no task of this batch was taken for longer than limitMs - a healthy
    // batch drains within milliseconds, so this means something is wedged
    bool stuckSince(const qint64 nowMs, const qint64 limitMs) const;
    qint64 ageMs(const qint64 nowMs) const { return nowMs - mCreatedMs; }
private:
    stdsptr<eTask> takeTask(const QList<QList<stdsptr<eTask>>*>& lists,
                            const bool requireCriticalAllowed);

    QList<stdsptr<eTask>> mGpuOnly;
    QList<stdsptr<eTask>> mGpuPreffered;
    QList<stdsptr<eTask>> mCpuPreffered;
    QList<stdsptr<eTask>> mCpuOnly;
    qint64 mCreatedMs = 0;
    qint64 mLastProgressMs = 0;
};
#endif // TASKQUE_H

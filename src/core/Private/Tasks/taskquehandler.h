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

#ifndef TASKQUEHANDLER_H
#define TASKQUEHANDLER_H
#include "taskque.h"
#include <QString>

class CORE_EXPORT TaskQueHandler {
public:
    int countQues() const;
    bool isEmpty() const;

    void clear();

    stdsptr<eTask> takeQuedForGpuProcessing();
    stdsptr<eTask> takeQuedForCpuProcessing();
    // Critical-memory path - see TaskQue::takeQuedForCriticalProcessing
    stdsptr<eTask> takeQuedForCriticalProcessing();

    void beginQue();

    void addTask(const stdsptr<eTask>& task);

    void endQue();

    int taskCount() const { return mTaskCount; }

    // ---- stalled-queue watchdog support ------------------------------
    // A batch that was not progressed for longer than limitMs is wedged:
    // its tasks can never be taken (their dependency count never reaches
    // zero), so the batch never empties and TaskScheduler::overflowed()
    // stays true forever, which stops the pipeline from being fed at all.
    bool hasStuckQue(const qint64 nowMs, const qint64 limitMs) const;
    // Reclaims tasks that can never run again; returns how many.
    int discardDeadTasks();
    // Last resort: abandons whole stalled batches. Their content is
    // rebuildable derived state, so dropping is safe; returns how many.
    int dropStuckQues(const qint64 nowMs, const qint64 limitMs);
    QString describeStuckQues(const qint64 nowMs) const;
private:
    void queDone(const TaskQue * const que, const int queId);

    int mTaskCount = 0;
    QList<stdsptr<TaskQue>> mQues;
    TaskQue * mCurrentQue = nullptr;
};
#endif // TASKQUEHANDLER_H

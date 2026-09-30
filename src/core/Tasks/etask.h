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

#ifndef ETASK_H
#define ETASK_H

#include "../glhelpers.h"
#include "../hardwareenums.h"
#include "../switchablecontext.h"
#include "etaskbase.h"

class CORE_EXPORT eTask : public StdSelfRef, public eTaskBase {
    friend class TaskScheduler;
    friend class Que;
    friend class eTaskBase;
    template <typename T> friend class TaskCollection;
protected:
    eTask() {}

    virtual void queTaskNow() = 0;
    virtual void afterQued() {}
    virtual void beforeProcessing(const Hardware) {}
public:
    virtual HardwareSupport hardwareSupport() const = 0;
    virtual void processGpu(QGL33 * const gl,
                            SwitchableContext &context) = 0;
    virtual void process() = 0;

    virtual bool nextStep() { return false; }

    // May this task run while the scheduler is in the critical memory
    // state? That state used to block every CPU/GPU dispatch, which made
    // the canvas stop showing content the user had just drawn - the very
    // tasks that are cheap and immediately visible (a small layer
    // rasterization) must stay allowed.
    virtual bool allowedInCriticalMemory() const { return false; }

    // Task created by an edit the user is waiting on (drawing a shape,
    // dragging a layer). Interactive tasks are taken first, so a
    // background backlog (preview warm-up, tmp reloads) cannot starve the
    // thing the user is looking at.
    virtual bool interactive() const { return false; }

    bool queTask();

    void aboutToProcess(const Hardware hw);
};

Q_DECLARE_METATYPE(stdsptr<eTask>);

#endif // ETASK_H

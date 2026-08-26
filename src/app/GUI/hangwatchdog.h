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

#ifndef HANGWATCHDOG_H
#define HANGWATCHDOG_H

// GUI hang watchdog: a background thread monitors a heartbeat that is
// refreshed by the GUI thread. When the heartbeat stops (GUI frozen)
// the watchdog dumps the call stacks of ALL threads of the process to
// %TEMP%/friction_hang_stack.txt (appending a new snapshot every 10
// seconds while the freeze lasts) so the hang location can be
// identified from a plain text file.
//
// start() MUST be called after QApplication has been constructed,
// otherwise the heartbeat timer cannot start and the watchdog would
// false-positive.

namespace HangWatchdog {
// Start the heartbeat timer and the watchdog thread.
// Must be called from the GUI thread, after QApplication exists.
void start();
}

#endif // HANGWATCHDOG_H

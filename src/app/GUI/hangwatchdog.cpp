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

#include "hangwatchdog.h"

#include <QDateTime>
#include <QFile>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QCoreApplication>

#include <atomic>

#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#endif

namespace {

#ifdef Q_OS_WIN

std::atomic<qint64> gHeartbeat{0};

struct ModuleInfo {
    DWORD64 base;
    DWORD64 size;
    QString name;
};

QList<ModuleInfo> snapshotModules()
{
    QList<ModuleInfo> mods;
    const HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,
                                                 GetCurrentProcessId());
    if (snap == INVALID_HANDLE_VALUE) { return mods; }
    MODULEENTRY32W me;
    ZeroMemory(&me, sizeof(me));
    me.dwSize = sizeof(me);
    if (Module32FirstW(snap, &me)) {
        do {
            ModuleInfo info;
            info.base = reinterpret_cast<DWORD64>(me.modBaseAddr);
            info.size = me.modBaseSize;
            info.name = QString::fromWCharArray(me.szModule);
            mods.append(info);
        } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);
    return mods;
}

QString moduleNameForAddress(const DWORD64 pc,
                             const QList<ModuleInfo> &mods)
{
    for (const auto &m : mods) {
        if (pc >= m.base && pc < m.base + m.size) {
            return QStringLiteral("%1+0x%2").arg(m.name)
                    .arg(pc - m.base, 0, 16);
        }
    }
    return QStringLiteral("0x%1").arg(pc, 16, 16, QChar('0'));
}

QString stackFrameToString(const DWORD64 pc,
                           HANDLE hProcess,
                           const QList<ModuleInfo> &mods)
{
    // symbol name (requires a matching PDB next to the binary)
    char symBuf[sizeof(SYMBOL_INFO) + 256] = {};
    const auto sym = reinterpret_cast<SYMBOL_INFO*>(symBuf);
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = 255;
    DWORD64 disp = 0;
    QString frame;
    if (SymFromAddr(hProcess, pc, &disp, sym)) {
        frame = QStringLiteral("%1+0x%2")
                .arg(QString::fromLatin1(sym->Name))
                .arg(disp, 0, 16);
    } else {
        frame = moduleNameForAddress(pc, mods);
    }
    // source line when available
    IMAGEHLP_LINE64 line;
    ZeroMemory(&line, sizeof(line));
    line.SizeOfStruct = sizeof(line);
    DWORD lineDisp = 0;
    if (SymGetLineFromAddr64(hProcess, pc, &lineDisp, &line)) {
        frame += QStringLiteral("  [%1:%2]")
                .arg(QString::fromLatin1(line.FileName))
                .arg(line.LineNumber);
    }
    return frame;
}

// Suspend the given thread, walk its stack, resume it again.
QString dumpThreadStack(const DWORD threadId,
                        HANDLE hProcess,
                        const QList<ModuleInfo> &mods)
{
    const HANDLE hThread = OpenThread(THREAD_SUSPEND_RESUME
                                          | THREAD_GET_CONTEXT
                                          | THREAD_QUERY_INFORMATION,
                                      FALSE, threadId);
    if (!hThread) {
        return QStringLiteral("  (OpenThread failed for %1)\n").arg(threadId);
    }
    QString result;
    if (SuspendThread(hThread) != DWORD(-1)) {
        CONTEXT ctx;
        ZeroMemory(&ctx, sizeof(ctx));
        ctx.ContextFlags = CONTEXT_FULL;
        if (GetThreadContext(hThread, &ctx)) {
            STACKFRAME64 sf;
            ZeroMemory(&sf, sizeof(sf));
            sf.AddrPC.Offset = ctx.Rip;
            sf.AddrPC.Mode = AddrModeFlat;
            sf.AddrFrame.Offset = ctx.Rbp;
            sf.AddrFrame.Mode = AddrModeFlat;
            sf.AddrStack.Offset = ctx.Rsp;
            sf.AddrStack.Mode = AddrModeFlat;
            const DWORD machine =
#ifdef _M_X64
                    IMAGE_FILE_MACHINE_AMD64;
#else
                    IMAGE_FILE_MACHINE_I386;
#endif
            for (int i = 0; i < 96; i++) {
                if (!StackWalk64(machine, hProcess, hThread, &sf, &ctx,
                                 nullptr, SymFunctionTableAccess64,
                                 SymGetModuleBase64, nullptr)) { break; }
                const DWORD64 pc = sf.AddrPC.Offset;
                if (!pc) { break; }
                result += QStringLiteral("  #%1 %2\n")
                        .arg(i).arg(stackFrameToString(pc, hProcess, mods));
            }
        } else {
            result = QStringLiteral("  (GetThreadContext failed)\n");
        }
        ResumeThread(hThread);
    } else {
        result = QStringLiteral("  (SuspendThread failed)\n");
    }
    CloseHandle(hThread);
    return result;
}

class HangWatchdogThread : public QThread {
public:
    HangWatchdogThread(const DWORD guiThread, QObject *parent = nullptr)
        : QThread(parent)
        , mGuiThread(guiThread) {}

    void run() override {
        const DWORD self = GetCurrentThreadId();
        const DWORD pid = GetCurrentProcessId();
        int episode = 0;
        int dumps = 0;
        qint64 lastDump = 0;
        while (!isInterruptionRequested()) {
            msleep(1000);
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            const qint64 beat = gHeartbeat.load();
            if (now - beat <= 8000) {
                if (dumps > 0) {
                    // recovered; next freeze starts a new episode
                    episode++;
                    dumps = 0;
                }
                continue;
            }
            if (dumps >= 60) { continue; }          // episode cap
            if (now - lastDump < 10000) { continue; } // dump every 10s
            lastDump = now;
            dumps++;

            const QString path =
                    QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                    + QStringLiteral("/friction_hang_stack.txt");

            // NOTE: do not use qWarning() here - if the GUI thread is
            // stuck inside the message handler it holds the log mutex
            // and we would deadlock as well. Write the file directly.
            QFile f(path);
            if (!f.open(QIODevice::WriteOnly | QIODevice::Append
                        | QIODevice::Text)) { continue; }
            f.write(QStringLiteral(
                        "\n===== HANG SNAPSHOT %1 @ %2 (episode %3, dump %4) =====\n")
                    .arg(QDateTime::currentDateTime()
                         .toString(QStringLiteral("hh:mm:ss.zzz")))
                    .arg(episode + 1).arg(dumps).toUtf8());

            const QList<ModuleInfo> mods = snapshotModules();

            // dump the GUI thread first (marked), then all others
            f.write(QStringLiteral("--- GUI thread %1 ---\n").arg(mGuiThread).toUtf8());
            f.write(dumpThreadStack(mGuiThread, GetCurrentProcess(), mods).toUtf8());

            const HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
            if (snap != INVALID_HANDLE_VALUE) {
                THREADENTRY32 te;
                ZeroMemory(&te, sizeof(te));
                te.dwSize = sizeof(te);
                if (Thread32First(snap, &te)) {
                    do {
                        if (te.th32OwnerProcessID != pid) { continue; }
                        if (te.th32ThreadID == self) { continue; }
                        if (te.th32ThreadID == mGuiThread) { continue; }
                        f.write(QStringLiteral("--- thread %1 ---\n")
                                .arg(te.th32ThreadID).toUtf8());
                        f.write(dumpThreadStack(te.th32ThreadID,
                                                GetCurrentProcess(),
                                                mods).toUtf8());
                    } while (Thread32Next(snap, &te));
                }
                CloseHandle(snap);
            }
            f.write(QStringLiteral(
                        "--- end of snapshot (file: %1) ---\n").arg(path).toUtf8());
        }
    }
private:
    const DWORD mGuiThread;
};

#endif // Q_OS_WIN

} // namespace

namespace HangWatchdog {

void start()
{
#ifdef Q_OS_WIN
    // The heartbeat timer requires a running event dispatcher; refuse
    // to start before QApplication exists (would false-positive).
    if (!QCoreApplication::instance()) {
        Q_ASSERT(false);
        return;
    }

    const HANDLE hProcess = GetCurrentProcess();
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS
                  | SYMOPT_LOAD_LINES);
    SymInitialize(hProcess, nullptr, TRUE);

    gHeartbeat = QDateTime::currentMSecsSinceEpoch();

    // heartbeat timer (runs in the GUI thread event loop)
    const auto timer = new QTimer();
    QObject::connect(timer, &QTimer::timeout, timer, []() {
        gHeartbeat = QDateTime::currentMSecsSinceEpoch();
    });
    timer->start(500);

    const auto watchdog = new HangWatchdogThread(GetCurrentThreadId());
    QObject::connect(QThread::currentThread(), &QThread::finished,
                     watchdog, &QThread::requestInterruption);
    watchdog->start();
#endif
}

} // namespace HangWatchdog

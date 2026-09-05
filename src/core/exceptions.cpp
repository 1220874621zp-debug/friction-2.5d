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

#include "exceptions.h"
#include <QMessageBox>
#include <QCoreApplication>
#include <QPointer>
#include <QThread>

std::string operator+(const std::string& c, const QString& k) {
    return c + k.toStdString();
}

std::string operator<<(const std::string& c, const QString& k) {
    return c + k.toStdString();
}

std::string operator>>(const QString& k, const std::string& c) {
    return k.toStdString() + c;
}

QDebug operator<<(QDebug out, const std::string& str) {
    out << str.c_str();
    return out;
}

bool isExceptionNested(const std::exception& e) {
    if(auto ne = dynamic_cast<const std::nested_exception*>(std::addressof(e))) {
        if(ne->nested_ptr()) return true;
    }
    return false;
}

void _gPrintException(const std::exception& e,
                      QString allText,
                      const uint level,
                      const bool fatal) {
    allText = QString::number(level) + ") " + e.what() + "\n " + allText;
    qCritical() << std::to_string(level) + ") " << e.what();
    try {
        if(!isExceptionNested(e)) {
            gPrintException(fatal, allText);
            if(!fatal) return;
        }
        std::rethrow_if_nested(e);
    } catch(const std::exception& ne) {
        _gPrintException(ne, allText, level + 1, fatal);
    } catch(...) {}
}

void gPrintExceptionCritical(const std::exception& e) {
    _gPrintException(e, "", 0, false);
}

void gPrintExceptionFatal(const std::exception& e) {
    _gPrintException(e, "", 0, true);
}

QString gAllTextFromException(const std::exception &e,
                              QString allText,
                              const uint level) {
    allText = (allText.isEmpty() ? "" : allText + "\n") +
            (std::to_string(level) + ") ").c_str() + e.what();
    qCritical() << std::to_string(level) + ") " << e.what();
    try {
        if(!isExceptionNested(e))  return allText;
        std::rethrow_if_nested(e);
    } catch(const std::exception& ne) {
        gAllTextFromException(ne, allText, level + 1);
    } catch(...) {}
    return allText;
}

void gPrintException(const bool fatal, const QString &allText) {
    qCritical() << (fatal ? "Fatal" : "Critical") << "Error" << allText;
    // A failing task queue reports one exception per finished task; each
    // modal exec() runs a nested event loop that immediately processes the
    // next failure, so a burst of failures recursed until the stack
    // overflowed. Show a single non-modal box, append further errors to it
    // (capped) and never spawn dialogs from worker threads.
    if(!QCoreApplication::instance()) return;
    if(QThread::currentThread() != QCoreApplication::instance()->thread()) return;
    static QPointer<QMessageBox> errorDialog;
    if(errorDialog) {
        const auto txt = errorDialog->text();
        if(txt.length() < 8000) {
            errorDialog->setText(txt + "\n\n" + allText);
        } else if(!txt.endsWith("…")) {
            errorDialog->setText(txt + "\n\n…");
        }
        return;
    }
    const QString txt = fatal ? "Fatal" : "Critical";
    const auto icon = fatal ? QMessageBox::Critical : QMessageBox::Warning;
    errorDialog = new QMessageBox(icon, txt + " Error", allText);
    errorDialog->setAttribute(Qt::WA_DeleteOnClose);
    errorDialog->setModal(false);
    errorDialog->show();
}

void gPrintException(const QString &allText) {
    gPrintException(false, allText);
}

void gPrintExceptionCritical(const std::exception_ptr &eptr) {
    try {
        if(eptr) std::rethrow_exception(eptr);
    } catch(const std::exception& e) {
        gPrintExceptionCritical(e);
    }
}

void gPrintExceptionFatal(const std::exception_ptr &eptr) {
    try {
        if(eptr) std::rethrow_exception(eptr);
    } catch(const std::exception& e) {
        gPrintExceptionFatal(e);
    }
}

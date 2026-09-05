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

#include "eimporters.h"
#include <QMessageBox>
#include <QProgressDialog>

#include "GUI/mainwindow.h"
#include "eimporters.h"
#include "svgimporter.h"
#include "Psd/psdimporter.h"
#include "Kra/kraimporter.h"
//#include "Ora/oraimporter.h"

qsptr<BoundingBox> eXevImporter::import(const QFileInfo &fileInfo, Canvas * const scene) const {
    Q_UNUSED(scene);
    MainWindow::sGetInstance()->loadXevFile(fileInfo.absoluteFilePath());
    return nullptr;
}

qsptr<BoundingBox> evImporter::import(const QFileInfo &fileInfo, Canvas * const scene) const {
    Q_UNUSED(scene);
    MainWindow::sGetInstance()->loadEVFile(fileInfo.absoluteFilePath());
    return nullptr;
}

qsptr<BoundingBox> eSvgImporter::import(const QFileInfo &fileInfo, Canvas * const scene) const {
    const auto gradientCreator = [scene]() {
        return scene->createNewGradient();
    };
    return ImportSVG::loadSVGFile(fileInfo.absoluteFilePath(),
                                  gradientCreator);
}

qsptr<BoundingBox> ePsdImporter::import(const QFileInfo &fileInfo, Canvas * const scene) const {
    Q_UNUSED(scene);
    // big PSDs take seconds to extract - show progress so it does not
    // look like a freeze
    QProgressDialog progress(
                QObject::tr("Importing PSD ..."),
                QString(), 0, 1);
    progress.setWindowModality(Qt::WindowModal);
    // show early: most imports finish in under a second and the old
    // 300ms delay meant the dialog barely flashed
    progress.setMinimumDuration(120);
    const auto result = ImportPSD::loadPSDFile(
                fileInfo.absoluteFilePath(),
                [&progress](const int cur, const int total) {
        if (total > 0 && progress.maximum() != total) {
            progress.setMaximum(total);
        }
        progress.setValue(cur);
        QCoreApplication::processEvents();
    });
    progress.close();
    return result;
}

qsptr<BoundingBox> eKraImporter::import(const QFileInfo &fileInfo, Canvas * const scene) const {
    // decoding the tiled layer data of an animated document takes a
    // while - show progress so it does not look like a freeze
    QProgressDialog progress(
                QObject::tr("Importing Krita file ..."),
                QString(), 0, 1);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(300);
    QStringList skipped;
    const auto result = ImportKRA::loadKRAFile(
                fileInfo.absoluteFilePath(),
                scene,
                [&progress](const int cur, const int total) {
        if (total > 0 && progress.maximum() != total) {
            progress.setMaximum(total);
        }
        progress.setValue(cur);
        QCoreApplication::processEvents();
    },
                &skipped);
    progress.close();
    if (!skipped.isEmpty()) {
        QMessageBox::information(
                    MainWindow::sGetInstance(),
                    QObject::tr("Krita import"),
                    QObject::tr("The following items were skipped:") +
                    QLatin1Char('\n') + skipped.join(QLatin1Char('\n')));
    }
    return result;
}

/*qsptr<BoundingBox> eOraImporter::import(const QFileInfo &fileInfo, Canvas * const scene) const {
    const auto gradientCreator = [scene]() {
        return scene->createNewGradient();
    };
    return ImportORA::loadORAFile(fileInfo.absoluteFilePath(),
                                  gradientCreator);
}*/

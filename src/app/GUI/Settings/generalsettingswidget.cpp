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

#include "generalsettingswidget.h"
#include "appsupport.h"
#include "themesupport.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QGroupBox>
#include <QScrollArea>
#include <QPushButton>
#include <QLineEdit>
#include <QStandardPaths>
#include <QDesktopServices>
#include <QDir>
#include <QMessageBox>
#include <QUrl>
#include <functional>

#include "Private/esettings.h"
#include "GUI/global.h"

#include "../mainwindow.h"
#include "widgets/twocolumnlayout.h"
#include "Private/document.h"
#include "canvas.h"
#include "Boxes/containerbox.h"
#include "Psd/psdimagebox.h"

GeneralSettingsWidget::GeneralSettingsWidget(QWidget *parent)
    : SettingsWidget(parent)
    , mAutoBackup(nullptr)
    , mAutoSave(nullptr)
    , mAutoSaveTimer(nullptr)
    , mDefaultInterfaceScaling(nullptr)
    , mInterfaceScaling(nullptr)
    , mTheme(nullptr)
    , mImportFileDir(nullptr)
{
    const auto mGeneralWidget = new QWidget(this);
    mGeneralWidget->setContentsMargins(0, 0, 0, 0);
    const auto mGeneralLayout = new QVBoxLayout(mGeneralWidget);
    mGeneralLayout->setContentsMargins(0, 0, 0, 0);

    const auto mProjectWidget = new QGroupBox(this);
    mProjectWidget->setObjectName("BlueBox");
    mProjectWidget->setTitle(tr("Project I/O"));
    mProjectWidget->setContentsMargins(0, 0, 0, 0);
    const auto mProjectLayout = new QVBoxLayout(mProjectWidget);

    mAutoBackup = new QCheckBox(tr("Enable Backup on Save"), this);
    if (AppSupport::isFlatpak()) {
        mAutoBackup->setChecked(false);
        mAutoBackup->setCheckable(false);
        mAutoBackup->setToolTip(tr("Backup files are not supported in flatpak."));
    } else {
        mAutoBackup->setCheckable(true);
        mAutoBackup->setToolTip(tr("Creates a backup file after each successful save.\n\n"
                                   "Backup files are stored in a folder called PROJECT.friction_backup."));
    }

    mProjectLayout->addWidget(mAutoBackup);

    mGeneralLayout->addWidget(mProjectWidget);

    const auto mAutoSaveWidget = new QWidget(this);
    mAutoSaveWidget->setContentsMargins(0, 0, 0, 0);
    const auto mAutoSaveLayout = new QHBoxLayout(mAutoSaveWidget);
    mAutoSaveLayout->setContentsMargins(0, 0, 0, 0);
    mAutoSaveLayout->setMargin(0);

    mAutoSave = new QCheckBox(tr("Enable Auto Save"), this);
    mAutoSave->setCheckable(true);
    mAutoSave->setToolTip(tr("Will auto save each X min if project is unsaved.\n\n"
                             "Enable Backup on Save for incremental saves (and as a failsafe)."));

    mAutoSaveTimer = new QSpinBox(this);
    mAutoSaveTimer->setRange(1, 60);
    mAutoSaveTimer->setSuffix(tr(" min"));

    mAutoSaveLayout->addWidget(mAutoSave);
    mAutoSaveLayout->addStretch();
    mAutoSaveLayout->addWidget(mAutoSaveTimer);

    mProjectLayout->addWidget(mAutoSaveWidget);

    const auto mScaleWidget = new QGroupBox(this);
    mScaleWidget->setObjectName("BlueBox");
    mScaleWidget->setTitle(tr("Interface Scaling"));
    mScaleWidget->setContentsMargins(0, 0, 0, 0);
    const auto mScaleLayout = new QVBoxLayout(mScaleWidget);

    const auto mScaleContainer = new QWidget(this);
    mScaleContainer->setContentsMargins(0, 0, 0, 0);
    const auto mScaleContainerLayout = new QHBoxLayout(mScaleContainer);
    mScaleLayout->addWidget(mScaleContainer);

    mInterfaceScaling = new QSlider(Qt::Horizontal, this);
    mInterfaceScaling->setRange(50, 150);
    mScaleContainerLayout->addWidget(mInterfaceScaling);

    const auto mScaleLabel = new QLabel(this);
    connect(mInterfaceScaling, &QSlider::valueChanged,
            mScaleLabel, [mScaleLabel](const int value) {
        mScaleLabel->setText(QString("%1 %").arg(value));
    });
    emit mInterfaceScaling->valueChanged(100);
    mScaleContainerLayout->addWidget(mScaleLabel);

    mDefaultInterfaceScaling = new QCheckBox(this);
    mDefaultInterfaceScaling->setText(tr("Auto"));
    mDefaultInterfaceScaling->setToolTip(tr("Use scaling reported by the system."));
    connect(mDefaultInterfaceScaling, &QCheckBox::stateChanged,
            this, [this]() {
        mInterfaceScaling->setEnabled(!mDefaultInterfaceScaling->isChecked());
    });

    mScaleContainerLayout->addWidget(mDefaultInterfaceScaling);

    // setting for HiDPI scale factor
    const auto passThroughInterfaceScaling = new QCheckBox(this);
    passThroughInterfaceScaling->setText(tr("HiDPI PassThrough"));
    passThroughInterfaceScaling->setToolTip(tr("If enabled the scaling factor for the UI will not be rounded, recommended for HiDPI displays.\n"
                                               "If you use a negative font/display system scaling and/or see artifacts on UI elements "
                                               "then disable this option.\nThe scaling factor will then round up for .75 and above."));
    passThroughInterfaceScaling->setChecked(AppSupport::getSettings("settings",
                                                                    "interfaceScalingPassThrough",
                                                                    true).toBool());
    connect(passThroughInterfaceScaling, &QCheckBox::stateChanged,
            this, [passThroughInterfaceScaling]() {
        // we just save on change, we don't care to integrate
        // this setting with the rest of the system
        // as qApp/eSettings etc is not available when this setting is needed
        AppSupport::setSettings("settings",
                                "interfaceScalingPassThrough",
                                passThroughInterfaceScaling->isChecked());
    });
    mScaleContainerLayout->addWidget(passThroughInterfaceScaling);

    const auto infoLabel = new QLabel(this);
    infoLabel->setText(tr("Changes here will require a restart of Friction."));
    mScaleLayout->addWidget(infoLabel);

    mGeneralLayout->addWidget(mScaleWidget);

    const auto mThemeWidget = new QGroupBox(this);
    mThemeWidget->setObjectName("BlueBox");
    mThemeWidget->setTitle(tr("Theme"));
    mThemeWidget->setContentsMargins(0, 0, 0, 0);
    const auto mThemeLayout = new QVBoxLayout(mThemeWidget);

    const auto mThemeContainer = new QWidget(this);
    mThemeContainer->setContentsMargins(0, 0, 0, 0);
    const auto mThemeContainerLayout = new QHBoxLayout(mThemeContainer);
    mThemeContainerLayout->setContentsMargins(0, 0, 0, 0);
    mThemeLayout->addWidget(mThemeContainer);

    mTheme = new QComboBox(this);
    for (const auto &id : ThemeSupport::availableThemeIds()) {
        mTheme->addItem(ThemeSupport::themeDisplayName(id), id);
    }
    mThemeContainerLayout->addWidget(mTheme);
    mThemeContainerLayout->addStretch();

    const auto themeInfoLabel = new QLabel(this);
    themeInfoLabel->setText(tr("Changing the theme requires a restart of Friction."));
    mThemeLayout->addWidget(themeInfoLabel);

    mGeneralLayout->addWidget(mThemeWidget);

    const auto mImportFileWidget = new QWidget(this);
    mImportFileWidget->setContentsMargins(0, 0, 0, 0);
    const auto mImportFileLayout = new QHBoxLayout(mImportFileWidget);
    mImportFileLayout->setContentsMargins(0, 0, 0, 0);
    mImportFileLayout->setMargin(0);

    const auto mImportFileLabel = new QLabel(tr("Default import directory"), this);
    mImportFileDir = new QComboBox(this);
    mImportFileDir->addItem(tr("Last used directory"), eSettings::ImportFileDirRecent);
    mImportFileDir->addItem(tr("Project directory"), eSettings::ImportFileDirProject);

    mImportFileLayout->addWidget(mImportFileLabel);
    mImportFileLayout->addWidget(mImportFileDir);

    mProjectLayout->addSpacing(10);
    mProjectLayout->addWidget(mImportFileWidget);

    const auto mCacheWidget = new QGroupBox(this);
    mCacheWidget->setObjectName("BlueBox");
    mCacheWidget->setTitle(tr("Cache"));
    mCacheWidget->setContentsMargins(0, 0, 0, 0);
    const auto mCacheLayout = new QVBoxLayout(mCacheWidget);

    const auto cachePathLabel = new QLabel(
                AppSupport::getAppCachePath(), this);
    cachePathLabel->setWordWrap(true);
    cachePathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    mCacheLayout->addWidget(cachePathLabel);

    const auto cacheInfoLabel = new QLabel(tr(
            "PSD layer pixel cache. Safe to clear - layers currently "
            "in use are re-extracted from their .fpsd packages."), this);
    cacheInfoLabel->setWordWrap(true);
    mCacheLayout->addWidget(cacheInfoLabel);

    const auto cacheBtnRow = new QWidget(this);
    cacheBtnRow->setContentsMargins(0, 0, 0, 0);
    const auto cacheBtnLayout = new QHBoxLayout(cacheBtnRow);
    cacheBtnLayout->setContentsMargins(0, 0, 0, 0);
    const auto openCacheBtn = new QPushButton(tr("Open Directory"), this);
    const auto clearCacheBtn = new QPushButton(tr("Clear PSD Cache"), this);
    cacheBtnLayout->addWidget(openCacheBtn);
    cacheBtnLayout->addWidget(clearCacheBtn);
    cacheBtnLayout->addStretch();
    mCacheLayout->addWidget(cacheBtnRow);

    connect(openCacheBtn, &QPushButton::clicked, this,
            [cachePathLabel]() {
        QDesktopServices::openUrl(
                    QUrl::fromLocalFile(cachePathLabel->text()));
    });
    connect(clearCacheBtn, &QPushButton::clicked, this, [this]() {
        // remove the PSDCache subtree, then re-extract the pixels of
        // every PSD layer currently in use so nothing goes blank
        const QString psdCache =
                AppSupport::getAppCachePath() + "/PSDCache";
        int removed = 0;
        QDir dir(psdCache);
        if(dir.exists()) {
            const auto dirs = dir.entryList(
                        QDir::Dirs | QDir::NoDotAndDotDot);
            for(const auto& d : dirs) {
                QDir sub(psdCache + "/" + d);
                removed += sub.entryList(QDir::Files).count();
            }
            dir.removeRecursively();
        }
        int refreshed = 0;
        if(Document::sInstance) {
            for(const auto& scene : Document::sInstance->fScenes) {
                if(!scene) continue;
                std::function<void(ContainerBox*)> walk =
                        [&](ContainerBox* const cont) {
                    for(const auto& c : cont->getContained()) {
                        if(const auto psd =
                                dynamic_cast<PsdImageBox*>(c.data())) {
                            if(psd->ensureCachedFile()) refreshed++;
                        } else if(const auto group =
                                  dynamic_cast<ContainerBox*>(c.data())) {
                            walk(group);
                        }
                    }
                };
                walk(scene.data());
            }
        }
        QMessageBox::information(this, tr("Cache"),
                tr("Removed %1 cache file(s). Re-extracted %2 layer(s) "
                   "currently in use.").arg(removed).arg(refreshed));
    });

    mGeneralLayout->addWidget(mCacheWidget);

    // snapshot export destination (timeline camera button)
    const auto mSnapshotWidget = new QGroupBox(this);
    mSnapshotWidget->setObjectName("BlueBox");
    mSnapshotWidget->setTitle(tr("Snapshots"));
    mSnapshotWidget->setContentsMargins(0, 0, 0, 0);
    const auto mSnapshotLayout = new QVBoxLayout(mSnapshotWidget);

    const auto snapDirRow = new QWidget(this);
    snapDirRow->setContentsMargins(0, 0, 0, 0);
    const auto snapDirLayout = new QHBoxLayout(snapDirRow);
    snapDirLayout->setContentsMargins(0, 0, 0, 0);
    const auto snapDirLabel = new QLabel(tr("Snapshot folder"), this);
    const auto snapDirEdit = new QLineEdit(this);
    snapDirEdit->setReadOnly(true);
    {
        QString dir = AppSupport::getSettings(QStringLiteral("snapshots"),
                                              QStringLiteral("dir")).toString();
        if(dir.isEmpty()) {
            dir = QStandardPaths::writableLocation(
                        QStandardPaths::DesktopLocation);
        }
        snapDirEdit->setText(dir);
    }
    const auto snapDirBrowse = new QPushButton(tr("Browse..."), this);
    snapDirLayout->addWidget(snapDirLabel);
    snapDirLayout->addWidget(snapDirEdit, 1);
    snapDirLayout->addWidget(snapDirBrowse);
    mSnapshotLayout->addWidget(snapDirRow);

    const auto snapInfoLabel = new QLabel(tr(
            "Snapshot PNGs are exported at 100% resolution. "
            "Empty folder means the Desktop is used."), this);
    snapInfoLabel->setWordWrap(true);
    mSnapshotLayout->addWidget(snapInfoLabel);

    connect(snapDirBrowse, &QPushButton::clicked, this, [this, snapDirEdit]() {
        const QString dir = AppSupport::getExistingDirectory(
                    this, tr("Choose Snapshot Folder"),
                    snapDirEdit->text());
        if(dir.isEmpty()) return;
        snapDirEdit->setText(dir);
        AppSupport::setSettings(QStringLiteral("snapshots"),
                                QStringLiteral("dir"), dir);
    });

    mGeneralLayout->addWidget(mSnapshotWidget);
    mGeneralLayout->addStretch();
    addWidget(mGeneralWidget);

    eSizesUI::widget.add(mAutoBackup, [this](const int size) {
        mAutoBackup->setFixedHeight(size);
        mAutoSave->setFixedHeight(size);
        mDefaultInterfaceScaling->setFixedHeight(size);
    });
}

void GeneralSettingsWidget::applySettings()
{
    AppSupport::setSettings("files",
                            "BackupOnSave",
                            mAutoBackup->isChecked());
    AppSupport::setSettings("files",
                            "AutoSave",
                            mAutoSave->isChecked());
    AppSupport::setSettings("files",
                            "AutoSaveTimeout",
                            (mAutoSaveTimer->value() * 60) * 1000);
    MainWindow::sGetInstance()->updateAutoSaveBackupState();

    mSett.fDefaultInterfaceScaling = mDefaultInterfaceScaling->isChecked();
    mSett.fInterfaceScaling = mInterfaceScaling->value() * 0.01;
    mSett.fImportFileDirOpt = mImportFileDir->currentData().toInt();

    AppSupport::setSettings("ui",
                            "theme",
                            mTheme->currentData().toString());

    //eSizesUI::font.updateSize();
    //eSizesUI::widget.updateSize();
}

void GeneralSettingsWidget::updateSettings(bool restore)
{
    const bool canBackup = AppSupport::isFlatpak() ? false :
                               AppSupport::getSettings("files",
                                                       "BackupOnSave",
                                                       false).toBool();

    mAutoBackup->setChecked(restore ? false : canBackup);
    mAutoSave->setChecked(restore ? false : AppSupport::getSettings("files",
                                                                    "AutoSave",
                                                                    false).toBool());
    int ms = restore ? 300000 : AppSupport::getSettings("files",
                                                        "AutoSaveTimeout",
                                                        300000).toInt();
    if (ms < 60000) { ms = 60000; }
    mAutoSaveTimer->setValue((ms / 1000) / 60);

    mDefaultInterfaceScaling->setChecked(mSett.fDefaultInterfaceScaling);
    mInterfaceScaling->setEnabled(!mDefaultInterfaceScaling->isChecked());
    mInterfaceScaling->setValue(mDefaultInterfaceScaling->isChecked() ? 100 : 100 * mSett.fInterfaceScaling);

    const QString themeId = restore ? QStringLiteral("friction") :
                                      AppSupport::getSettings("ui",
                                                              "theme",
                                                              "friction").toString();
    const int themeIndex = mTheme->findData(themeId);
    mTheme->setCurrentIndex(themeIndex < 0 ? 0 : themeIndex);

    for (int i = 0; i < mImportFileDir->count(); i++) {
        if (mImportFileDir->itemData(i).toInt() == mSett.fImportFileDirOpt) {
            mImportFileDir->setCurrentIndex(i);
            return;
        }
    }
}

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
#include <QSet>
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
#include "fileshandler.h"
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

    // language selector (restart to apply; Chinese is the default)
    {
        const auto langWidget = new QWidget(this);
        langWidget->setContentsMargins(0, 0, 0, 0);
        const auto langLayout = new QHBoxLayout(langWidget);
        langLayout->setContentsMargins(0, 0, 0, 0);
        const auto langLabel = new QLabel(tr("Language"), this);
        const auto langCombo = new QComboBox(this);
        langCombo->addItem(QStringLiteral("\u4E2D\u6587"),   // 中文
                           QStringLiteral("zh_CN"));
        langCombo->addItem(QStringLiteral("English"),
                           QStringLiteral("en"));
        const QString curLang = AppSupport::getSettings(
                    QStringLiteral("ui"), QStringLiteral("language"),
                    QStringLiteral("zh_CN")).toString();
        const int idx = curLang == QStringLiteral("en") ? 1 : 0;
        langCombo->setCurrentIndex(idx);
        connect(langCombo, qOverload<int>(&QComboBox::activated),
                this, [langCombo](const int index) {
            AppSupport::setSettings(QStringLiteral("ui"),
                                    QStringLiteral("language"),
                                    langCombo->itemData(index).toString());
        });
        langLayout->addWidget(langLabel);
        langLayout->addWidget(langCombo);
        langLayout->addStretch();
        mThemeLayout->addWidget(langWidget);

        const auto langInfoLabel = new QLabel(
                    tr("Changing the language requires a restart of Friction."),
                    this);
        mThemeLayout->addWidget(langInfoLabel);
    }

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

    // KRA import cache: configurable location + reference-aware clear
    mCacheLayout->addSpacing(10);

    const auto kraDefaultRoot = AppSupport::getAppCachePath() +
            QStringLiteral("/KRACache");
    const auto kraCurrentRoot = [kraDefaultRoot]() {
        const QString custom = AppSupport::getSettings(
                    QStringLiteral("settings"),
                    QStringLiteral("KraCachePath")).toString();
        return custom.isEmpty() ? kraDefaultRoot
                                : QDir::cleanPath(custom);
    }();

    const auto kraPathRow = new QWidget(this);
    kraPathRow->setContentsMargins(0, 0, 0, 0);
    const auto kraPathLayout = new QHBoxLayout(kraPathRow);
    kraPathLayout->setContentsMargins(0, 0, 0, 0);
    kraPathLayout->setMargin(0);
    const auto kraPathLabel = new QLabel(tr("KRA 缓存文件夹"), this);
    const auto kraPathEdit = new QLineEdit(this);
    kraPathEdit->setReadOnly(true);
    kraPathEdit->setText(kraCurrentRoot);
    const auto kraPathBrowse = new QPushButton(tr("浏览..."), this);
    const auto kraPathReset = new QPushButton(tr("恢复默认"), this);
    kraPathLayout->addWidget(kraPathLabel);
    kraPathLayout->addWidget(kraPathEdit, 1);
    kraPathLayout->addWidget(kraPathBrowse);
    kraPathLayout->addWidget(kraPathReset);
    mCacheLayout->addWidget(kraPathRow);

    const auto kraInfoLabel = new QLabel(tr(
            "导入 .kra 工程时解码的图层图存放于此。路径只影响之后的导入，"
            "已有缓存不会被移动。"), this);
    kraInfoLabel->setWordWrap(true);
    mCacheLayout->addWidget(kraInfoLabel);

    const auto kraBtnRow = new QWidget(this);
    kraBtnRow->setContentsMargins(0, 0, 0, 0);
    const auto kraBtnLayout = new QHBoxLayout(kraBtnRow);
    kraBtnLayout->setContentsMargins(0, 0, 0, 0);
    kraBtnLayout->setMargin(0);
    const auto openKraCacheBtn = new QPushButton(tr("打开 KRA 缓存目录"), this);
    const auto clearKraCacheBtn = new QPushButton(tr("清除 KRA 缓存"), this);
    kraBtnLayout->addWidget(openKraCacheBtn);
    kraBtnLayout->addWidget(clearKraCacheBtn);
    kraBtnLayout->addStretch();
    mCacheLayout->addWidget(kraBtnRow);

    const auto applyKraPath = [kraPathEdit, kraDefaultRoot](
            const QString& dir) {
        if (dir.isEmpty()) {
            AppSupport::setSettings(QStringLiteral("settings"),
                                    QStringLiteral("KraCachePath"),
                                    QString());
            kraPathEdit->setText(kraDefaultRoot);
        } else {
            AppSupport::setSettings(QStringLiteral("settings"),
                                    QStringLiteral("KraCachePath"), dir);
            kraPathEdit->setText(QDir::cleanPath(dir));
        }
    };
    connect(kraPathBrowse, &QPushButton::clicked, this,
            [this, applyKraPath, kraPathEdit]() {
        const QString dir = AppSupport::getExistingDirectory(
                    this, tr("选择 KRA 缓存文件夹"),
                    kraPathEdit->text());
        if (!dir.isEmpty()) { applyKraPath(dir); }
    });
    connect(kraPathReset, &QPushButton::clicked, this, [applyKraPath]() {
        applyKraPath(QString());
    });
    connect(openKraCacheBtn, &QPushButton::clicked, this,
            [kraPathEdit]() {
        QDir().mkpath(kraPathEdit->text());
        QDesktopServices::openUrl(
                    QUrl::fromLocalFile(kraPathEdit->text()));
    });
    connect(clearKraCacheBtn, &QPushButton::clicked, this, [this, kraPathEdit]() {
        // files referenced by live layers must survive the clear
        QSet<QString> referenced;
        const auto handlers = FilesHandler::sInstance->fileHandlers();
        for (const auto& h : handlers) {
            if (h && h->refCount() > 0) {
                referenced.insert(QDir::cleanPath(h->path()).toLower());
            }
        }

        const QDir root(kraPathEdit->text());
        QStringList toDelete;
        qint64 bytes = 0;
        int skipped = 0;
        const auto hashDirs = root.entryList(
                    QDir::Dirs | QDir::NoDotAndDotDot);
        for (const auto& d : hashDirs) {
            const auto pngs = QDir(root.absoluteFilePath(d)).entryInfoList(
                        {QStringLiteral("*.png")}, QDir::Files);
            for (const auto& fi : pngs) {
                if (referenced.contains(
                            QDir::cleanPath(fi.absoluteFilePath()).toLower())) {
                    skipped++;
                } else {
                    toDelete << fi.absoluteFilePath();
                    bytes += fi.size();
                }
            }
        }
        if (toDelete.isEmpty()) {
            QMessageBox::information(this, tr("Cache"),
                tr("没有可清理的 KRA 缓存%1。")
                .arg(skipped > 0 ? tr("（%1 张正被引用，已跳过）").arg(skipped)
                                 : QString()));
            return;
        }
        const auto btn = QMessageBox::question(this, tr("Cache"),
            tr("将删除 %1 张 KRA 图层图，共约 %2 MB，"
               "跳过 %3 张（当前工程正在引用）。\n\n"
               "⚠ 注意：清理只保护当前已打开工程引用的文件；"
               "其它未打开的工程若含有 .kra 导入的图层，"
               "重新打开后这些图层会断链失效。\n\n是否继续清理？")
            .arg(toDelete.count())
            .arg(QString::number(bytes / (1024.0 * 1024.0), 'f', 1))
            .arg(skipped));
        if (btn != QMessageBox::Yes) { return; }

        int failed = 0;
        for (const auto& path : toDelete) {
            if (!QFile::remove(path)) { failed++; }
        }
        // drop hash folders that became empty
        for (const auto& d : hashDirs) {
            const QString abs = root.absoluteFilePath(d);
            QDir sub(abs);
            if (sub.isEmpty()) { root.rmdir(d); }
        }
        if (failed > 0) {
            QMessageBox::information(this, tr("Cache"),
                tr("清理完成，另有 %1 张因文件被占用跳过"
                   "（关闭相关工程后可重试）。").arg(failed));
        } else {
            QMessageBox::information(this, tr("Cache"),
                tr("清理完成，共删除 %1 张，释放约 %2 MB。")
                .arg(toDelete.count())
                .arg(QString::number(bytes / (1024.0 * 1024.0), 'f', 1)));
        }
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

    if (mTheme) {
        ThemeSupport::setThemeFromId(mTheme->currentData().toString());
        ThemeSupport::saveThemeConfig();
    }

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

    if (mTheme) {
        const QString themeId = restore ? QStringLiteral("friction") : ThemeSupport::themeId();
        const int themeIndex = mTheme->findData(themeId);
        mTheme->setCurrentIndex(themeIndex < 0 ? 0 : themeIndex);
    }

    for (int i = 0; i < mImportFileDir->count(); i++) {
        if (mImportFileDir->itemData(i).toInt() == mSett.fImportFileDirOpt) {
            mImportFileDir->setCurrentIndex(i);
            return;
        }
    }
}

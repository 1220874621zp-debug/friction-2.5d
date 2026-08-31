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
*/

#include "aidepthdialog.h"

#include <QComboBox>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProgressBar>
#include <QProgressDialog>
#include <QPushButton>
#include <QVBoxLayout>

#include <utility>

#include "appsupport.h"
#include "Boxes/boundingbox.h"
#include "Boxes/containerbox.h"
#include "Boxes/imagebox.h"
#include "Boxes/imagesequencebox.h"
#include "canvas.h"
#include "exceptions.h"
#include "Private/document.h"
#include "Timeline/durationrectangle.h"

namespace {

constexpr int previewMaxSize = 300;

QImage skImageToQImage(const sk_sp<SkImage>& image, const int maxSize)
{
    if (!image) { return QImage(); }
    const auto raster = image->makeRasterImage();
    if (!raster) { return QImage(); }
    QImage q(raster->width(), raster->height(), QImage::Format_RGBA8888);
    if (q.isNull()) { return QImage(); }
    const auto info = SkImageInfo::Make(raster->width(), raster->height(),
                                        kRGBA_8888_SkColorType,
                                        kUnpremul_SkAlphaType);
    if (!raster->readPixels(info, q.bits(), q.bytesPerLine(), 0, 0)) {
        return QImage();
    }
    return q.scaled(maxSize, maxSize,
                    Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

// PNG-encodes the depth map into a cache temp file. Runs on the CPU
// PNG-encodes the image to the given path. Runs on the CPU worker
// thread; QFile is used because it copes with Unicode paths.
QString writeDepthPngTo(const QString& path, const sk_sp<SkImage>& image)
{
    if (!image || path.isEmpty()) { return QString(); }
    const auto raster = image->makeRasterImage();
    if (!raster) { return QString(); }
    SkPixmap pixmap;
    if (!raster->peekPixels(&pixmap)) { return QString(); }
    SkDynamicMemoryWStream stream;
    if (!SkEncodeImage(&stream, pixmap, SkEncodedImageFormat::kPNG, 100)) {
        return QString();
    }
    const auto data = stream.detachAsData();
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) { return QString(); }
    if (file.write(static_cast<const char*>(data->data()),
                   data->size()) != data->size()) {
        return QString();
    }
    return path;
}

QString writeDepthPng(const sk_sp<SkImage>& image)
{
    return writeDepthPngTo(AppSupport::getAppTempPath(
                QStringLiteral("ai_depth_%1.png")
                .arg(QDateTime::currentMSecsSinceEpoch())), image);
}

float median3(const float a, const float b, const float c)
{
    float x = a, y = b, z = c;
    if (x > y) { std::swap(x, y); }
    if (y > z) { std::swap(y, z); }
    if (x > y) { std::swap(x, y); }
    return y;
}

} // namespace

AiDepthDialog::AiDepthDialog(Document& doc,
                             const QList<BoundingBox*>& boxes,
                             QWidget* const parent) :
    QDialog(parent),
    mDocument(doc),
    mBoxes(boxes)
{
    setWindowTitle(tr("AI 深度估计"));
    setMinimumWidth(640);

    const auto hint = new QLabel(tr("为选中的图层生成<b>单目深度图</b>"
                                    "（Depth Anything V2，本机离线推理）："
                                    "结果作为新图片图层叠放在源图层位置，"
                                    "可用作 2.5D 视差、蒙版或调色辅助。"), this);
    hint->setWordWrap(true);

    mSourceCombo = new QComboBox(this);
    for (const auto& box : mBoxes) {
        mSourceCombo->addItem(box->prp_getName(),
                              QVariant::fromValue(static_cast<void*>(box)));
    }
    mSourceCombo->setToolTip(tr("在画布上选中的图层，任意类型均可（组、矢量、位图）"));

    mModelCombo = new QComboBox(this);
    mModelCombo->addItem(tr("小型（快，随程序内置）"));
    mModelCombo->addItem(tr("基础（更精细，需下载）"));
    mModelCombo->addItem(tr("大型（最精细，需下载）"));
    mModelCombo->setItemData(0, tr("Depth Anything V2 Small · 24.8M 参数 · "
                                   "Apache-2.0 许可"), Qt::ToolTipRole);
    mModelCombo->setItemData(1, tr("Depth Anything V2 Base · 97.5M 参数 · "
                                   "CC-BY-NC-4.0（非商业）许可，约 196 MB，"
                                   "联网下载后保存在本机"), Qt::ToolTipRole);
    mModelCombo->setItemData(2, tr("Depth Anything V2 Large · 335.3M 参数 · "
                                   "CC-BY-NC-4.0（非商业）许可，约 630 MB，"
                                   "联网下载后保存在本机；视频时序稳定性最好，"
                                   "推理也最慢"), Qt::ToolTipRole);
    connect(mModelCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &AiDepthDialog::updateModelState);

    mModelStateLabel = new QLabel(this);
    mDownloadBtn = new QPushButton(tr("下载模型"), this);
    connect(mDownloadBtn, &QPushButton::clicked,
            this, &AiDepthDialog::startDownload);

    mProgress = new QProgressBar(this);
    mProgress->setRange(0, 100);
    mProgress->setValue(0);
    mProgress->hide();

    mSizeCombo = new QComboBox(this);
    mSizeCombo->addItem(tr("392（快速）"));
    mSizeCombo->addItem(tr("518（均衡）"));
    mSizeCombo->addItem(tr("700（精细）"));
    mSizeCombo->setCurrentIndex(1);
    mSizeCombo->setToolTip(tr("推理分辨率（长边）。越高细节越多、速度越慢"));

    mOutputCombo = new QComboBox(this);
    mOutputCombo->addItem(tr("灰度（亮=近）"));
    mOutputCombo->addItem(tr("灰度（亮=远）"));
    mOutputCombo->addItem(tr("伪彩（JET）"));
    mOutputCombo->setToolTip(tr("深度图的着色方式"));

    const auto form = new QFormLayout;
    form->addRow(tr("源图层"), mSourceCombo);
    form->addRow(tr("模型"), mModelCombo);

    const auto modelRow = new QHBoxLayout;
    modelRow->addWidget(mModelStateLabel, 1);
    modelRow->addWidget(mDownloadBtn);
    form->addRow(QString(), modelRow);
    form->addRow(tr("推理尺寸"), mSizeCombo);
    form->addRow(tr("输出样式"), mOutputCombo);

    const auto paramsBox = new QGroupBox(tr("参数"), this);
    paramsBox->setLayout(form);

    mRuntimeLabel = new QLabel(this);
    mRuntimeLabel->setWordWrap(true);

    const auto previewBox = new QGroupBox(tr("预览"), this);
    mSrcPreview = new QLabel(tr("（点击\"生成预览\"）"), previewBox);
    mDepthPreview = new QLabel(tr("（暂无深度图）"), previewBox);
    for (auto* label : { mSrcPreview, mDepthPreview }) {
        label->setMinimumSize(previewMaxSize, previewMaxSize * 3 / 4);
        label->setAlignment(Qt::AlignCenter);
        label->setStyleSheet(QStringLiteral("border: 1px solid #393939;"));
    }
    const auto srcCaption = new QLabel(tr("源图层"), previewBox);
    const auto depthCaption = new QLabel(tr("深度图"), previewBox);
    const auto previewLay = new QHBoxLayout(previewBox);
    const auto col1 = new QVBoxLayout;
    col1->addWidget(mSrcPreview);
    col1->addWidget(srcCaption, 0, Qt::AlignHCenter);
    const auto col2 = new QVBoxLayout;
    col2->addWidget(mDepthPreview);
    col2->addWidget(depthCaption, 0, Qt::AlignHCenter);
    previewLay->addLayout(col1);
    previewLay->addLayout(col2);

    mStatusLine = new QLabel(this);
    mStatusLine->setWordWrap(true);

    mPreviewBtn = new QPushButton(tr("生成预览"), this);
    connect(mPreviewBtn, &QPushButton::clicked,
            this, [this]() { startPreview(false); });
    mInsertBtn = new QPushButton(tr("插入为图层"), this);
    connect(mInsertBtn, &QPushButton::clicked,
            this, [this]() { startPreview(true); });
    mBatchBtn = new QPushButton(tr("帧批量..."), this);
    mBatchBtn->setToolTip(tr("按时间轴入出点（或图层自身时长）逐帧推理，"
                             "含时序平滑，生成图片序列图层"));
    connect(mBatchBtn, &QPushButton::clicked,
            this, &AiDepthDialog::startBatch);
    mSettingsBtn = new QPushButton(tr("设置"), this);
    mSettingsBtn->setToolTip(tr("打开模型文件夹，查看手动放置模型的目录结构"));
    connect(mSettingsBtn, &QPushButton::clicked,
            this, &AiDepthDialog::openSettings);
    mCloseBtn = new QPushButton(tr("关闭"), this);
    connect(mCloseBtn, &QPushButton::clicked, this, &QDialog::reject);

    const auto btnRow = new QHBoxLayout;
    btnRow->addWidget(mPreviewBtn);
    btnRow->addWidget(mInsertBtn);
    btnRow->addWidget(mBatchBtn);
    btnRow->addStretch();
    btnRow->addWidget(mSettingsBtn);
    btnRow->addWidget(mCloseBtn);

    const auto mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(hint);
    mainLayout->addWidget(paramsBox);
    mainLayout->addWidget(mProgress);
    mainLayout->addWidget(previewBox);
    mainLayout->addWidget(mRuntimeLabel);
    mainLayout->addWidget(mStatusLine);
    mainLayout->addLayout(btnRow);

    // prime the runtime DLL load on the main thread (vtracer pattern)
    if (AiDepth::available()) {
        mRuntimeLabel->setText(tr("推理引擎：%1").arg(AiDepth::versionString()));
    } else {
        mRuntimeLabel->setText(tr("<b>未找到 onnxruntime.dll</b>，推理不可用。"
                                  "请确认它已随程序一起部署。"));
    }
    updateModelState();
    syncButtons();
    const QString pthWarning = pthHint();
    if (!pthWarning.isEmpty()) { setStatus(pthWarning); }
}

AiDepthDialog::~AiDepthDialog()
{
    if (mBatch) { mBatch->fCanceled = true; }
    if (mDownloading) { abortDownload(); }
}

QString AiDepthDialog::currentModelId() const
{
    switch (mModelCombo->currentIndex()) {
    case 1: return QStringLiteral("base");
    case 2: return QStringLiteral("large");
    default: return QStringLiteral("small");
    }
}

int AiDepthDialog::currentInputSize() const
{
    switch (mSizeCombo->currentIndex()) {
    case 0: return 392;
    case 2: return 700;
    default: return 518;
    }
}

QString AiDepthDialog::resultKey() const
{
    if (mSourceCombo->currentIndex() < 0) { return QString(); }
    return QStringLiteral("%1|%2|%3|%4")
            .arg(reinterpret_cast<qlonglong>(
                     mBoxes.at(mSourceCombo->currentIndex())))
            .arg(currentModelId())
            .arg(currentInputSize())
            .arg(mOutputCombo->currentIndex());
}

void AiDepthDialog::updateModelState()
{
    const auto id = currentModelId();
    const auto* info = AiDepth::ModelCatalog::model(id);
    if (!info) { return; }
    const QString dir = AiDepth::ModelCatalog::resolveModelDir(id);
    if (!dir.isEmpty()) {
        mModelStateLabel->setText(tr("✓ 已就绪"));
        mDownloadBtn->setEnabled(false);
        mDownloadBtn->setToolTip(QString());
    } else {
        mModelStateLabel->setText(tr("✗ 未就绪（需 %1）")
                                  .arg(AiDepth::ModelCatalog::humanSize(*info)));
        mDownloadBtn->setEnabled(true);
        mDownloadBtn->setToolTip(tr("下载到：%1")
            .arg(AiDepth::ModelCatalog::downloadDir()
                 + QLatin1Char('/') + info->fDirName));
    }
}

void AiDepthDialog::syncButtons()
{
    const bool idle = !mBusy && !mDownloading;
    mPreviewBtn->setEnabled(idle && AiDepth::available());
    mInsertBtn->setEnabled(idle && AiDepth::available());
    mSourceCombo->setEnabled(idle);
    mModelCombo->setEnabled(idle);
    mSizeCombo->setEnabled(idle);
    mOutputCombo->setEnabled(idle);
    if (mDownloading) {
        mDownloadBtn->setText(tr("取消下载"));
    } else {
        mDownloadBtn->setText(tr("下载模型"));
    }
}

void AiDepthDialog::setStatus(const QString& text)
{
    mStatusLine->setText(text);
}

void AiDepthDialog::startPreview(const bool insertAfter)
{
    if (mBusy || mDownloading) { return; }
    if (mSourceCombo->currentIndex() < 0) { return; }
    const auto box = mBoxes.at(mSourceCombo->currentIndex());
    if (!box) { return; }

    // reuse a matching preview result for direct insert
    if (insertAfter && mResultKey == resultKey() && mDepthImage) {
        insertDepthLayer();
        return;
    }

    if (!AiDepth::available()) {
        QMessageBox::warning(this, tr("AI 深度估计"),
                             tr("未找到 onnxruntime.dll，无法推理。"));
        return;
    }
    if (AiDepth::ModelCatalog::resolveModelDir(currentModelId()).isEmpty()) {
        QMessageBox::information(this, tr("AI 深度估计"),
                                 tr("所选模型尚未就绪，请先点击「下载模型」。"));
        return;
    }

    mBusy = true;
    syncButtons();
    setStatus(tr("正在渲染源图层..."));

    const auto renderData = box->queExternalRender(
                box->anim_getCurrentRelFrame(), true);
    if (!renderData) {
        mBusy = false;
        syncButtons();
        setStatus(tr("无法创建渲染任务。"));
        return;
    }
    const QPointer<AiDepthDialog> guard(this);
    renderData->addDependent({
        [guard, renderData, insertAfter]() {
            if (!guard) { return; }
            guard->mSourceImage = renderData->fRenderedImage;
            if (!guard->mSourceImage) {
                guard->mBusy = false;
                guard->syncButtons();
                guard->setStatus(tr("源图层渲染失败（内容为空？）。"));
                return;
            }
            guard->mSrcPreview->setPixmap(QPixmap::fromImage(
                skImageToQImage(guard->mSourceImage, previewMaxSize)));
            guard->startInference(insertAfter);
        },
        [guard]() {
            if (!guard) { return; }
            guard->mBusy = false;
            guard->syncButtons();
            guard->setStatus(tr("源图层渲染已取消。"));
        }
    });
}

void AiDepthDialog::startInference(const bool insertAfter)
{
    const auto state = std::make_shared<RunState>();
    state->fSrc = mSourceImage;
    state->fModelDir = AiDepth::ModelCatalog::resolveModelDir(currentModelId());
    state->fOpts.fInputSize = currentInputSize();
    state->fOpts.fOutputMode = mOutputCombo->currentIndex();

    setStatus(tr("推理中（首次运行需加载模型，可能较慢）..."));

    const QPointer<AiDepthDialog> guard(this);
    mTask = enve::make_shared<eCustomCpuTask>(
        nullptr,
        [state]() {
            state->fSt = AiDepth::runDepth(state->fSrc, state->fModelDir,
                                           state->fOpts, state->fOut,
                                           state->fErr);
            if (state->fSt == AiDepth::DepthStatus::Ok) {
                state->fPngPath = writeDepthPng(state->fOut);
                if (state->fPngPath.isEmpty()) {
                    state->fSt = AiDepth::DepthStatus::Error;
                    state->fErr = QStringLiteral("png encode failed");
                }
            }
        },
        [guard, state, insertAfter]() {
            if (!guard) { return; }
            guard->mBusy = false;
            guard->syncButtons();
            if (state->fSt != AiDepth::DepthStatus::Ok) {
                guard->setStatus(tr("推理失败：%1").arg(state->fErr));
                QMessageBox::warning(guard, tr("AI 深度估计"),
                                     tr("推理失败：%1").arg(state->fErr));
                return;
            }
            guard->mDepthImage = state->fOut;
            guard->mResultPngPath = state->fPngPath;
            guard->mResultKey = guard->resultKey();
            guard->updateDepthPreview();
            guard->setStatus(tr("深度图就绪（%1×%2）。")
                             .arg(state->fOut->width())
                             .arg(state->fOut->height()));
            if (insertAfter) { guard->insertDepthLayer(); }
        },
        [guard]() {
            if (!guard) { return; }
            guard->mBusy = false;
            guard->syncButtons();
            guard->setStatus(tr("推理已取消。"));
        });
    mTask->queTask();
}

void AiDepthDialog::updateDepthPreview()
{
    if (!mDepthImage) { return; }
    mDepthPreview->setPixmap(QPixmap::fromImage(
        skImageToQImage(mDepthImage, previewMaxSize)));
}

void AiDepthDialog::insertDepthLayer()
{
    if (mResultPngPath.isEmpty() || !mDepthImage) {
        QMessageBox::information(this, tr("AI 深度估计"),
                                 tr("请先生成预览。"));
        return;
    }
    const auto box = mBoxes.at(mSourceCombo->currentIndex());
    if (!box) { return; }

    try {
        const auto imgBox = enve::make_shared<ImageBox>();
        imgBox->setFilePath(mResultPngPath);

        const auto parentGroup = box->getParentGroup();
        if (!parentGroup) {
            QMessageBox::warning(this, tr("AI 深度估计"),
                                 tr("源图层没有父容器，无法插入。"));
            return;
        }
        parentGroup->prp_pushUndoRedoName(tr("AI 深度估计"));
        parentGroup->addContained(imgBox);

        // scale to fit the canvas (aspect preserved) and center on it
        const auto scene = *mDocument.fActiveScene;
        if (scene) {
            const qreal fit = qMin(
                        static_cast<qreal>(scene->getCanvasWidth()) / mDepthImage->width(),
                        static_cast<qreal>(scene->getCanvasHeight()) / mDepthImage->height());
            imgBox->planCenterPivotPosition();
            imgBox->startScaleTransform();
            imgBox->setScale(fit);
            imgBox->finishTransform();
            const QPointF imgCenter = imgBox->getTotalTransform().map(
                        QPointF(mDepthImage->width() / 2.0,
                                mDepthImage->height() / 2.0));
            imgBox->startPosTransform();
            imgBox->moveByAbs(QPointF(scene->getCanvasWidth() / 2.0,
                                      scene->getCanvasHeight() / 2.0)
                              - imgCenter);
            imgBox->finishTransform();
        } else {
            imgBox->planCenterPivotPosition();
        }
        imgBox->rename(tr("深度 - %1").arg(box->prp_getName()));

        mDocument.actionFinished();
        setStatus(tr("已插入深度图层（可 Ctrl+Z 撤销）。"));
    } catch(const std::exception& e) {
        gPrintExceptionCritical(e);
        QMessageBox::warning(this, tr("AI 深度估计"),
                             tr("插入图层失败，详见日志。"));
    }
}

QString AiDepthDialog::pthHint() const
{
    const auto scan = [](const QString& dir) {
        return QDir(dir).entryList({QStringLiteral("*.pth")}, QDir::Files);
    };
    const QStringList found = scan(AiDepth::ModelCatalog::downloadDir())
            + scan(AiDepth::ModelCatalog::bundledDir());
    if (found.isEmpty()) { return QString(); }
    return tr("⚠ 检测到 %1 个 .pth 文件（PyTorch 权重）：本程序需要 ONNX 格式，"
              "无法直接使用；正确的目录与文件结构见「设置」。").arg(found.count());
}

bool AiDepthDialog::computeBatchFrames(QList<int>& frames, QString& why) const
{
    const auto scene = *mDocument.fActiveScene;
    if (!scene) { why = tr("没有活动场景"); return false; }
    if (mSourceCombo->currentIndex() < 0) { why = tr("没有源图层"); return false; }
    const auto box = mBoxes.at(mSourceCombo->currentIndex());
    if (!box) { why = tr("源图层无效"); return false; }

    // timeline in/out range (scene duration), clamped to the layer's
    // own span when it has one (e.g. the video length)
    const auto sceneRange = scene->getFrameRange();
    int lo = sceneRange.fMin;
    int hi = sceneRange.fMax;
    if (const auto durRect = box->getDurationRectangle()) {
        const auto boxRange = durRect->getAbsFrameRange();
        lo = qMax(lo, boxRange.fMin);
        hi = qMin(hi, boxRange.fMax);
    }
    lo = qMax(0, lo);
    if (hi < lo) {
        why = tr("可用帧范围为空（时间轴范围与图层时长无交集）");
        return false;
    }
    for (int f = lo; f <= hi; f++) { frames << f; }
    return true;
}

void AiDepthDialog::startBatch()
{
    if (mBusy || mDownloading) { return; }
    if (!AiDepth::available()) {
        QMessageBox::warning(this, tr("AI 深度估计"),
                             tr("未找到 onnxruntime.dll，无法推理。"));
        return;
    }
    if (AiDepth::ModelCatalog::resolveModelDir(currentModelId()).isEmpty()) {
        QMessageBox::information(this, tr("AI 深度估计"),
                                 tr("所选模型尚未就绪，请先点击「下载模型」。"));
        return;
    }
    QList<int> frames;
    QString why;
    if (!computeBatchFrames(frames, why)) {
        QMessageBox::information(this, tr("AI 深度估计"), why);
        return;
    }
    qreal secPerFrame = 0.8;
    if (currentModelId() == QStringLiteral("base")) { secPerFrame = 2.2; }
    else if (currentModelId() == QStringLiteral("large")) { secPerFrame = 8.0; }
    const auto btn = QMessageBox::question(this, tr("帧批量"),
        tr("将处理 %1 帧（第 %2 ~ %3 帧，含时序平滑），预计约 %4 分钟，"
           "结果将作为图片序列图层插入。是否开始？")
        .arg(frames.count())
        .arg(frames.first()).arg(frames.last())
        .arg(int(frames.count() * secPerFrame / 60) + 1));
    if (btn != QMessageBox::Yes) { return; }

    const auto box = mBoxes.at(mSourceCombo->currentIndex());
    mBatch = std::make_shared<BatchState>();
    mBatch->fBox = box;
    mBatch->fAbsFrames = frames;
    mBatch->fOpts.fInputSize = currentInputSize();
    mBatch->fOpts.fOutputMode = mOutputCombo->currentIndex();
    mBatch->fOutDir = AppSupport::getAppTempPath(
                QStringLiteral("ai_depth_seq_%1")
                .arg(QDateTime::currentMSecsSinceEpoch()));
    QDir().mkpath(mBatch->fOutDir);

    mBusy = true;
    syncButtons();
    mBatchProgress = new QProgressDialog(
                tr("批量推理中（含时序平滑）..."), tr("取消"),
                0, frames.count(), this);
    mBatchProgress->setWindowModality(Qt::WindowModal);
    mBatchProgress->setMinimumDuration(0);
    mBatchProgress->setValue(0);
    connect(mBatchProgress, &QProgressDialog::canceled, this, [this]() {
        if (mBatch) { mBatch->fCanceled = true; }
    });
    setStatus(tr("批量推理：共 %1 帧...").arg(frames.count()));
    nextBatchFrame();
}

void AiDepthDialog::nextBatchFrame()
{
    if (!mBatch) { return; }
    if (!mBatch->fErr.isEmpty()) {
        finishBatch(false, tr("推理失败：%1").arg(mBatch->fErr));
        return;
    }
    if (mBatch->fCanceled) { finishBatch(false, tr("已取消")); return; }
    if (mBatch->fIdx >= mBatch->fAbsFrames.count()) {
        finishBatch(true, QString());
        return;
    }
    const auto box = mBatch->fBox.data();
    if (!box) { finishBatch(false, tr("源图层已失效")); return; }

    const int absFrame = mBatch->fAbsFrames.at(mBatch->fIdx);
    const int relFrame = box->prp_absFrameToRelFrame(absFrame);
    const auto renderData = box->queExternalRender(relFrame, true);
    if (!renderData) {
        finishBatch(false, tr("第 %1 帧渲染任务创建失败").arg(absFrame));
        return;
    }
    const QPointer<AiDepthDialog> guard(this);
    renderData->addDependent({
        [guard, renderData, absFrame]() {
            if (!guard || !guard->mBatch) { return; }
            guard->mBatch->fSrc = renderData->fRenderedImage;
            if (!guard->mBatch->fSrc) {
                guard->finishBatch(false,
                    guard->tr("第 %1 帧渲染失败（内容为空）").arg(absFrame));
                return;
            }
            guard->runBatchInference();
        },
        [guard]() {
            if (guard && guard->mBatch) {
                guard->finishBatch(false, guard->tr("源渲染已取消"));
            }
        }
    });
}

void AiDepthDialog::runBatchInference()
{
    const auto state = mBatch;
    const int idx = state->fIdx;
    const int absFrame = state->fAbsFrames.at(idx);
    const QString modelDir = AiDepth::ModelCatalog::resolveModelDir(
                currentModelId());
    const QPointer<AiDepthDialog> guard(this);

    mTask = enve::make_shared<eCustomCpuTask>(
        nullptr,
        [state, modelDir, idx]() {
            std::vector<float> raw;
            int w = 0, h = 0;
            QString err;
            const auto st = AiDepth::runDepthRaw(state->fSrc, modelDir,
                                                 state->fOpts.fInputSize,
                                                 raw, w, h, err);
            if (st != AiDepth::DepthStatus::Ok) {
                state->fErr = err;
                return;
            }
            const size_t n = static_cast<size_t>(w) * h;
            if (state->fH1.size() != n || state->fOutPrev.size() != n ||
                w != state->fW || h != state->fH) {
                // first frame or resolution change: reset the history
                state->fH1.clear();
                state->fH2.clear();
                state->fOutPrev.clear();
                state->fW = w;
                state->fH = h;
                state->fSrcW = state->fSrc->width();
                state->fSrcH = state->fSrc->height();
            }
            // one global value range for the whole clip (taken from the
            // first frame, padded) keeps overall brightness from pumping
            if (!state->fRangeSet) {
                float mn = raw[0];
                float mx = raw[0];
                for (const auto v : raw) {
                    if (v < mn) { mn = v; }
                    if (v > mx) { mx = v; }
                }
                const float pad = qMax(1e-4f, (mx - mn) * 0.02f);
                state->fRangeMin = mn - pad;
                state->fRangeMax = mx + pad;
                state->fRangeSet = true;
            }
            const float range = state->fRangeMax - state->fRangeMin;
            const float inv = range > 1e-6f ? 1.f / range : 0.f;
            std::vector<float> nrm(n);
            for (size_t i = 0; i < n; i++) {
                float v = (raw[i] - state->fRangeMin) * inv;
                if (v < 0.f) { v = 0.f; }
                else if (v > 1.f) { v = 1.f; }
                nrm[i] = v;
            }
            // temporal smoothing: median-3 kills single-frame spikes,
            // EMA (alpha 0.7) smooths residual jitter
            std::vector<float> smooth(n);
            if (state->fH2.size() == n) {
                for (size_t i = 0; i < n; i++) {
                    smooth[i] = median3(state->fH2[i], state->fH1[i], nrm[i]);
                }
            } else {
                smooth = nrm;
            }
            if (state->fOutPrev.size() == n) {
                const float a = 0.7f;
                for (size_t i = 0; i < n; i++) {
                    smooth[i] = a * smooth[i] + (1.f - a) * state->fOutPrev[i];
                }
            }
            state->fH2 = state->fH1;
            state->fH1 = nrm;
            state->fOutPrev = smooth;

            const auto img = AiDepth::colorizeDepth(
                        smooth, w, h, 0.f, 1.f,
                        state->fOpts.fOutputMode,
                        state->fSrcW, state->fSrcH);
            if (!img) {
                state->fErr = QStringLiteral("colorize failed");
                return;
            }
            state->fPngPath = writeDepthPngTo(
                        state->fOutDir + QStringLiteral("/depth_%1.png")
                        .arg(idx, 5, 10, QLatin1Char('0')), img);
            if (state->fPngPath.isEmpty()) {
                state->fErr = QStringLiteral("png write failed");
            }
        },
        [guard, state, absFrame]() {
            if (!guard || !guard->mBatch) { return; }
            if (!state->fErr.isEmpty()) {
                guard->finishBatch(false,
                    guard->tr("第 %1 帧推理失败：%2")
                        .arg(absFrame).arg(state->fErr));
                return;
            }
            if (guard->mBatchProgress) {
                guard->mBatchProgress->setValue(state->fIdx + 1);
            }
            state->fIdx++;
            guard->nextBatchFrame();
        },
        [guard]() {
            if (guard && guard->mBatch) {
                guard->finishBatch(false, guard->tr("推理任务已取消"));
            }
        });
    mTask->queTask();
}

void AiDepthDialog::finishBatch(const bool ok, const QString& failReason)
{
    const auto state = mBatch;
    mBatch.reset();
    if (mBatchProgress) {
        mBatchProgress->close();
        mBatchProgress->deleteLater();
        mBatchProgress = nullptr;
    }
    mBusy = false;
    syncButtons();
    if (!state) { return; }
    if (!ok) {
        setStatus(tr("批量未完成：%1。已生成 %2/%3 帧，文件保留在 %4")
                  .arg(failReason)
                  .arg(state->fIdx).arg(state->fAbsFrames.count())
                  .arg(state->fOutDir));
        return;
    }
    const auto scene = *mDocument.fActiveScene;
    const auto box = state->fBox.data();
    if (!scene || !box || !box->getParentGroup()) {
        setStatus(tr("帧图已生成于 %1，但场景/源图层已失效，未插入图层。")
                  .arg(state->fOutDir));
        return;
    }
    try {
        const auto seqBox = enve::make_shared<ImageSequenceBox>();
        seqBox->setFolderPath(state->fOutDir);
        // play the sequence exactly over the processed frame range
        seqBox->createDurationRectangle();
        if (const auto durRect = seqBox->getDurationRectangle()) {
            durRect->setMinAbsFrame(state->fAbsFrames.first());
            durRect->setFramesDuration(state->fAbsFrames.count());
        }
        const auto parentGroup = box->getParentGroup();
        parentGroup->prp_pushUndoRedoName(tr("AI 深度估计（帧批量）"));
        parentGroup->addContained(seqBox);

        // scale to fit the canvas (aspect preserved) and center on it
        const qreal fit = qMin(
                    static_cast<qreal>(scene->getCanvasWidth()) / state->fSrcW,
                    static_cast<qreal>(scene->getCanvasHeight()) / state->fSrcH);
        seqBox->planCenterPivotPosition();
        seqBox->startScaleTransform();
        seqBox->setScale(fit);
        seqBox->finishTransform();
        const QPointF center = seqBox->getTotalTransform().map(
                    QPointF(state->fSrcW / 2.0, state->fSrcH / 2.0));
        seqBox->startPosTransform();
        seqBox->moveByAbs(QPointF(scene->getCanvasWidth() / 2.0,
                                  scene->getCanvasHeight() / 2.0) - center);
        seqBox->finishTransform();
        seqBox->rename(tr("深度序列 - %1").arg(box->prp_getName()));
        mDocument.actionFinished();
        setStatus(tr("已插入深度序列图层（%1 帧，含时序平滑，可 Ctrl+Z 撤销）。")
                  .arg(state->fAbsFrames.count()));
    } catch(const std::exception& e) {
        gPrintExceptionCritical(e);
        QMessageBox::warning(this, tr("AI 深度估计"),
                             tr("插入序列图层失败，帧图保留在：%1")
                             .arg(state->fOutDir));
    }
}

void AiDepthDialog::openSettings()
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("AI 深度估计 设置"));
    dlg.setMinimumWidth(520);

    const QString dlDir = AiDepth::ModelCatalog::downloadDir();
    const QString bdDir = AiDepth::ModelCatalog::bundledDir();

    const auto hint = new QLabel(
        tr("手动下载的模型放到<b>模型文件夹</b>（在线下载也会保存到这里）。"
           "每个模型一个子文件夹，含两个同名文件、缺一不可："
           "<ul>"
           "<li><code>depth_anything_v2_small/model_fp16.onnx</code>"
           " + <code>model_fp16.onnx_data</code>（约 50 MB）</li>"
           "<li><code>depth_anything_v2_base/model_fp16.onnx</code>"
           " + <code>model_fp16.onnx_data</code>（约 196 MB）</li>"
           "</ul>"
           "文件放好后回到这里点「刷新状态」。"), &dlg);
    hint->setWordWrap(true);

    const auto dirLabel = new QLabel(tr("模型文件夹：<code>%1</code>").arg(dlDir), &dlg);
    dirLabel->setWordWrap(true);
    dirLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    QLabel* bundledLabel = nullptr;
    if (bdDir != dlDir) {
        bundledLabel = new QLabel(
                    tr("内置模型目录（随程序安装，只读）：<code>%1</code>").arg(bdDir),
                    &dlg);
        bundledLabel->setWordWrap(true);
    }

    const auto openBtn = new QPushButton(tr("打开模型文件夹"), &dlg);
    const auto refreshBtn = new QPushButton(tr("刷新状态"), &dlg);
    const auto closeBtn = new QPushButton(tr("关闭"), &dlg);
    connect(openBtn, &QPushButton::clicked, &dlg, [dlDir]() {
        QDir().mkpath(dlDir);
        QDesktopServices::openUrl(QUrl::fromLocalFile(dlDir));
    });
    connect(refreshBtn, &QPushButton::clicked, &dlg, [this, &dlg]() {
        updateModelState();
        const bool ready = !AiDepth::ModelCatalog::resolveModelDir(
                    currentModelId()).isEmpty();
        QString msg = ready ?
                    tr("当前所选模型已就绪 ✓（可切换上方模型逐个检查）") :
                    tr("当前所选模型仍未就绪：文件缺失、大小或校验不符。");
        const QString hint = pthHint();
        if (!hint.isEmpty()) { msg += QStringLiteral("\n\n") + hint; }
        QMessageBox::information(&dlg, tr("AI 深度估计"), msg);
    });
    connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::accept);

    const auto row = new QHBoxLayout;
    row->addWidget(openBtn);
    row->addWidget(refreshBtn);
    row->addStretch();
    row->addWidget(closeBtn);

    const auto lay = new QVBoxLayout(&dlg);
    lay->addWidget(hint);
    lay->addWidget(dirLabel);
    if (bundledLabel) { lay->addWidget(bundledLabel); }
    lay->addLayout(row);

    dlg.exec();
    updateModelState(); // files may have been placed while it was open
}

void AiDepthDialog::startDownload()
{
    if (mDownloading) {
        abortDownload();
        return;
    }
    if (mBusy) { return; }

    const auto id = currentModelId();
    mDlModel = AiDepth::ModelCatalog::model(id);
    if (!mDlModel) { return; }

    if (id != QStringLiteral("small")) {
        QString name = tr("基础模型");
        if (id == QStringLiteral("large")) { name = tr("大型模型"); }
        const auto btn = QMessageBox::question(
                    this, tr("许可确认"),
                    tr("%1采用 <b>CC-BY-NC-4.0</b> 许可："
                       "个人学习与非商业创作可自由使用，"
                       "<b>禁止商业用途</b>。下载即表示你接受该许可。<br><br>"
                       "需要商业用途时请改用小型模型（Apache-2.0）。是否下载？")
                    .arg(name));
        if (btn != QMessageBox::Yes) { return; }
    }

    mDlDir = AiDepth::ModelCatalog::downloadDir()
            + QLatin1Char('/') + mDlModel->fDirName;
    QDir().mkpath(mDlDir);
    mDlSource = 0;
    mDlFileIdx = 0;
    mDownloading = true;
    if (!mNet) { mNet = new QNetworkAccessManager(this); }
    mProgress->setValue(0);
    mProgress->show();
    syncButtons();
    setStatus(tr("开始下载..."));

    beginNextFile();
}

void AiDepthDialog::abortDownload()
{
    if (!mDownloading) { return; }
    if (mReply) {
        mReply->disconnect(this);
        mReply->abort();
        mReply->deleteLater();
        mReply = nullptr;
    }
    if (mDlFileIo) {
        if (mDlFileIo->isOpen()) { mDlFileIo->close(); }
        mDlFileIo->deleteLater();
        mDlFileIo = nullptr;
    }
    mDownloading = false;
    mProgress->hide();
    syncButtons();
    updateModelState();
    setStatus(tr("下载已取消（已下载部分保留，下次继续）。"));
}

void AiDepthDialog::beginNextFile()
{
    const auto& urls = mDlModel->fSources.at(mDlSource);
    const auto& file = mDlModel->fFiles.at(mDlFileIdx);
    const QString partPath = mDlDir + QLatin1Char('/')
            + file.fFileName + QStringLiteral(".part");

    qint64 offset = 0;
    {
        QFileInfo fi(partPath);
        if (fi.exists() && fi.size() <= file.fExpectedBytes) {
            offset = fi.size();
        }
    }
    mDlPartBase = offset;

    if (mDlFileIo) {
        if (mDlFileIo->isOpen()) { mDlFileIo->close(); }
        mDlFileIo->deleteLater();
    }
    mDlFileIo = new QFile(partPath, this);
    if (!mDlFileIo->open(QIODevice::WriteOnly | QIODevice::Append)) {
        nextSourceOrFinish(false);
        return;
    }

    QNetworkRequest request(urls.at(mDlFileIdx));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    if (offset > 0) {
        request.setRawHeader("Range",
            QStringLiteral("bytes=%1-").arg(offset).toLatin1());
    }

    const qint64 bytesBefore = [this]() {
        qint64 total = 0;
        for (int i = 0; i < mDlFileIdx; i++) {
            total += mDlModel->fFiles.at(i).fExpectedBytes;
        }
        return total;
    }();
    const qint64 grandTotal = AiDepth::ModelCatalog::totalBytes(*mDlModel);

    mReply = mNet->get(request);
    connect(mReply, &QNetworkReply::readyRead, this, [this]() {
        if (!mReply || !mDlFileIo || !mDlFileIo->isOpen()) { return; }
        // server ignored our Range header and sends the full body: the
        // append-mode part file must be truncated or it ends up doubled
        if (mDlPartBase > 0 && mReply->attribute(
                    QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200) {
            mDlFileIo->close();
            mDlFileIo->open(QIODevice::WriteOnly | QIODevice::Truncate);
            mDlPartBase = 0;
        }
        mDlFileIo->write(mReply->readAll());
    });
    connect(mReply, &QNetworkReply::downloadProgress, this,
            [this, bytesBefore, grandTotal](qint64, qint64 received) {
        const qint64 done = bytesBefore + mDlPartBase + received;
        if (grandTotal > 0) {
            mProgress->setValue(int(done * 100 / grandTotal));
        }
    });
    connect(mReply, &QNetworkReply::finished,
            this, &AiDepthDialog::downloadFinished);
    setStatus(tr("下载 %1 ...（源 %2/%3）")
              .arg(mDlModel->fFiles.at(mDlFileIdx).fFileName)
              .arg(mDlSource + 1)
              .arg(mDlModel->fSources.size()));
}

void AiDepthDialog::downloadFinished()
{
    if (!mDownloading || !mReply) { return; }

    const auto networkError = mReply->error();
    const int httpStatus = mReply->attribute(
                QNetworkRequest::HttpStatusCodeAttribute).toInt();
    mReply->deleteLater();
    mReply = nullptr;

    if (mDlFileIo) {
        if (mDlFileIo->isOpen()) { mDlFileIo->close(); }
        mDlFileIo->deleteLater();
        mDlFileIo = nullptr;
    }
    if (networkError == QNetworkReply::OperationCanceledError) {
        // user abort already handled
        return;
    }

    const auto& file = mDlModel->fFiles.at(mDlFileIdx);
    const QString partPath = mDlDir + QLatin1Char('/')
            + file.fFileName + QStringLiteral(".part");
    const QString finalPath = mDlDir + QLatin1Char('/') + file.fFileName;
    QFileInfo fi(partPath);

    // resume corner case: server ignored the Range header and returned
    // the full file from scratch (200), so the part file was appended
    // twice — only trust it when opened fresh (offset 0)
    const bool rangeOk = networkError == QNetworkReply::NoError &&
            (httpStatus == 200 || httpStatus == 206 || httpStatus == 0);
    if (rangeOk && fi.size() == file.fExpectedBytes && mDlPartBase <= fi.size()) {
        QCryptographicHash hash(QCryptographicHash::Sha256);
        QFile f(partPath);
        if (f.open(QIODevice::ReadOnly) && hash.addData(&f) &&
            QString::fromLatin1(hash.result().toHex()) == file.fSha256) {
            QFile::remove(finalPath);
            if (QFile::rename(partPath, finalPath)) {
                nextSourceOrFinish(true);
                return;
            }
        }
        // corrupt or checksum mismatch: drop the part and retry elsewhere
        QFile::remove(partPath);
        nextSourceOrFinish(false);
        return;
    }
    if (rangeOk && fi.size() < file.fExpectedBytes && fi.size() > 0) {
        // truncated transfer without an error state: keep the part for
        // resume and retry (same source first, it may just timeout)
        nextSourceOrFinish(false);
        return;
    }

    QFile::remove(partPath);
    nextSourceOrFinish(false);
}

void AiDepthDialog::nextSourceOrFinish(const bool fileDone)
{
    if (fileDone) {
        mDlFileIdx++;
        if (mDlFileIdx >= mDlModel->fFiles.size()) {
            mDownloading = false;
            mProgress->setValue(100);
            mProgress->hide();
            syncButtons();
            updateModelState();
            setStatus(tr("模型下载完成，可以开始推理。"));
            QMessageBox::information(this, tr("AI 深度估计"),
                                     tr("模型下载完成。"));
            return;
        }
        // next file, restart from the primary source
        mDlSource = 0;
    } else {
        mDlSource++;
        if (mDlSource >= mDlModel->fSources.size()) {
            mDownloading = false;
            mProgress->hide();
            syncButtons();
            updateModelState();
            setStatus(tr("自动下载失败，可手动下载（部分进度已保留）。"));
            showManualDownloadHelp();
            return;
        }
    }
    beginNextFile();
}

void AiDepthDialog::showManualDownloadHelp()
{
    const auto& urls = mDlModel->fSources.at(0);
    QStringList lines;
    for (int i = 0; i < mDlModel->fFiles.size(); i++) {
        lines << QStringLiteral("• %1 → %2/%3")
                .arg(urls.at(i), mDlModel->fDirName,
                     mDlModel->fFiles.at(i).fFileName);
    }
    QMessageBox box(this);
    box.setWindowTitle(tr("手动下载指引"));
    box.setIcon(QMessageBox::Information);
    box.setText(tr("自动下载失败（网络阻断）。请手动下载以下文件并放入模型目录："
                   "<br><br>%1<br><br>模型目录：<code>%2</code>")
                .arg(lines.join(QStringLiteral("<br>")))
                .arg(mDlDir));
    const auto openUrlBtn = box.addButton(tr("打开下载页"), QMessageBox::AcceptRole);
    const auto openDirBtn = box.addButton(tr("打开模型目录"), QMessageBox::ActionRole);
    box.addButton(QMessageBox::Close);
    box.exec();
    if (box.clickedButton() == openUrlBtn) {
        AppSupport::openUrl(QUrl(urls.at(urls.size() - 1)));
    } else if (box.clickedButton() == openDirBtn) {
        QDir().mkpath(mDlDir);
        QDesktopServices::openUrl(QUrl::fromLocalFile(mDlDir));
    }
}

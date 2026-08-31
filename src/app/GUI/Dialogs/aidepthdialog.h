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

#ifndef AIDEPTHDIALOG_H
#define AIDEPTHDIALOG_H

#include <QDialog>
#include <QPointer>

#include <memory>
#include <vector>

#include "Depth/aidepthprovider.h"
#include "Depth/modelcatalog.h"

#include "Tasks/updatable.h"

#include "skia/skiaincludes.h"

class BoundingBox;
class Document;
class QComboBox;
class QLabel;
class QProgressBar;
class QProgressDialog;
class QPushButton;
class QNetworkAccessManager;
class QNetworkReply;
class QFile;

class AiDepthDialog : public QDialog {
    Q_OBJECT
public:
    AiDepthDialog(Document& doc, const QList<BoundingBox*>& boxes,
                  QWidget* const parent = nullptr);
    ~AiDepthDialog();

private:
    struct RunState {
        sk_sp<SkImage> fSrc;
        sk_sp<SkImage> fOut;
        QString fModelDir;
        QString fPngPath;
        QString fErr;
        AiDepth::DepthStatus fSt = AiDepth::DepthStatus::Error;
        AiDepth::Options fOpts;
    };

    // per-frame batch pipeline: render -> raw inference -> clip-wide
    // normalization -> colorize -> png -> next frame
    struct BatchState {
        QPointer<BoundingBox> fBox;
        QList<int> fAbsFrames;
        int fIdx = 0;
        QString fOutDir;
        AiDepth::Options fOpts;
        sk_sp<SkImage> fSrc;
        float fRangeMin = 0.f;
        float fRangeMax = 1.f;
        bool fRangeSet = false;
        int fW = 0, fH = 0;
        int fSrcW = 0, fSrcH = 0;
        bool fCanceled = false;
        QString fErr;
        QString fPngPath;
    };

    QString currentModelId() const;
    int currentInputSize() const;
    QString resultKey() const;
    QString pthHint() const;
    bool computeBatchFrames(QList<int>& frames, QString& why) const;

    void updateModelState();
    void syncButtons();
    void setStatus(const QString& text);

    void startPreview(const bool insertAfter);
    void startInference(const bool insertAfter);
    void insertDepthLayer();
    void updateDepthPreview();
    void openSettings();
    void cleanupCache();

    void startBatch();
    void nextBatchFrame();
    void runBatchInference();
    void finishBatch(const bool ok, const QString& failReason);

    void startDownload();
    void abortDownload();
    void beginNextFile();
    void downloadFinished();
    void nextSourceOrFinish(const bool fileDone);
    void showManualDownloadHelp();

    Document& mDocument;
    QList<BoundingBox*> mBoxes;

    QComboBox* mSourceCombo = nullptr;
    QComboBox* mModelCombo = nullptr;
    QLabel* mModelStateLabel = nullptr;
    QPushButton* mDownloadBtn = nullptr;
    QProgressBar* mProgress = nullptr;
    QComboBox* mSizeCombo = nullptr;
    QComboBox* mOutputCombo = nullptr;
    QLabel* mRuntimeLabel = nullptr;
    QLabel* mSrcPreview = nullptr;
    QLabel* mDepthPreview = nullptr;
    QLabel* mStatusLine = nullptr;
    QPushButton* mPreviewBtn = nullptr;
    QPushButton* mInsertBtn = nullptr;
    QPushButton* mBatchBtn = nullptr;
    QPushButton* mSettingsBtn = nullptr;
    QPushButton* mCloseBtn = nullptr;

    // inference state
    sk_sp<SkImage> mSourceImage;
    sk_sp<SkImage> mDepthImage;
    QString mResultPngPath;
    QString mResultKey;
    stdsptr<eCustomCpuTask> mTask;
    bool mBusy = false;

    // batch (per-frame) state; BatchState defined above
    std::shared_ptr<BatchState> mBatch;
    QProgressDialog* mBatchProgress = nullptr;

    // download state
    QNetworkAccessManager* mNet = nullptr;
    QNetworkReply* mReply = nullptr;
    QFile* mDlFileIo = nullptr;
    const AiDepth::ModelInfo* mDlModel = nullptr;
    QString mDlDir;
    int mDlSource = 0;
    int mDlFileIdx = 0;
    qint64 mDlPartBase = 0; // bytes already on disk before this attempt
    bool mDownloading = false;
};

#endif // AIDEPTHDIALOG_H

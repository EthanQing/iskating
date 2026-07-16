#include "offlineanalysisdialog.h"

#include "analysistaskmanager.h"
#include "trainingrepository.h"

#include <QDateTime>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {

constexpr const char *kModelVersion = "personvit-msmt17-vit-base-v1";
constexpr const char *kPreprocessingVersion = "rgb-256x128-mean0.5-std0.5-l2-v1";

QTableWidgetItem *item(const QString &text, bool editable = true)
{
    auto *result = new QTableWidgetItem(text);
    if (!editable) {
        result->setFlags(result->flags() & ~Qt::ItemIsEditable);
    }
    return result;
}

QString durationText(int milliseconds)
{
    const int seconds = std::max(0, milliseconds / 1000);
    return QStringLiteral("%1:%2:%3")
        .arg(seconds / 3600, 2, 10, QLatin1Char('0'))
        .arg((seconds / 60) % 60, 2, 10, QLatin1Char('0'))
        .arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

int durationMs(const QString &text)
{
    const QStringList parts = text.trimmed().split(QLatin1Char(':'));
    if (parts.size() != 3) {
        return -1;
    }
    bool okHour = false;
    bool okMinute = false;
    bool okSecond = false;
    const int hour = parts.at(0).toInt(&okHour);
    const int minute = parts.at(1).toInt(&okMinute);
    const int second = parts.at(2).toInt(&okSecond);
    if (!okHour || !okMinute || !okSecond || hour < 0 || minute < 0 || minute > 59 || second < 0 || second > 59) {
        return -1;
    }
    return (hour * 3600 + minute * 60 + second) * 1000;
}

} // namespace

OfflineAnalysisDialog::OfflineAnalysisDialog(TrainingRepository *repository,
                                             QVector<QString> athleteIds,
                                             AnalysisTaskManager *taskManager,
                                             QWidget *parent)
    : QDialog(parent),
      m_repository(repository),
      m_athleteIds(std::move(athleteIds)),
      m_taskManager(taskManager)
{
    setWindowTitle(QStringLiteral("12 路完整帧率离线分析"));
    resize(1240, 620);
    auto *layout = new QVBoxLayout(this);
    auto *tip = new QLabel(QStringLiteral("输入必须是 Ubuntu 分析主机可访问的 nas:// URI。严格模式对每个解码帧运行 YOLO，处理可以慢于视频时长。"), this);
    tip->setWordWrap(true);
    layout->addWidget(tip);

    auto *nasForm = new QFormLayout();
    m_nasRootEdit = new QLineEdit(QSettings().value(QStringLiteral("offlineAnalysis/nasRoot")).toString(), this);
    m_nasRootEdit->setPlaceholderText(QStringLiteral("例如 Z:/iskating 或 \\\\nas\\iskating"));
    nasForm->addRow(QStringLiteral("Windows NAS 根目录"), m_nasRootEdit);
    layout->addLayout(nasForm);

    m_sourcesTable = new QTableWidget(12, 8, this);
    m_sourcesTable->setHorizontalHeaderLabels({QStringLiteral("机位"), QStringLiteral("NAS URI"), QStringLiteral("分辨率"),
                                               QStringLiteral("FPS"), QStringLiteral("时长"), QStringLiteral("源开始时间"),
                                               QStringLiteral("校正 ms"), QStringLiteral("状态/进度")});
    m_sourcesTable->verticalHeader()->hide();
    m_sourcesTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_sourcesTable->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    for (int row = 0; row < 12; ++row) {
        m_sourcesTable->setItem(row, 0, item(QStringLiteral("CAM %1").arg(row + 1, 2, 10, QLatin1Char('0')), false));
        m_sourcesTable->setItem(row, 1, item(QStringLiteral("nas://videos/camera%1.mp4").arg(row + 1, 2, 10, QLatin1Char('0'))));
        m_sourcesTable->setItem(row, 2, item(QStringLiteral("1920x1080")));
        m_sourcesTable->setItem(row, 3, item(QStringLiteral("60")));
        m_sourcesTable->setItem(row, 4, item(QStringLiteral("00:00:00")));
        m_sourcesTable->setItem(row, 5, item(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)));
        m_sourcesTable->setItem(row, 6, item(QStringLiteral("0")));
        m_sourcesTable->setItem(row, 7, item(QStringLiteral("待创建"), false));
    }
    layout->addWidget(m_sourcesTable, 1);

    auto *actions = new QHBoxLayout();
    auto *importButton = new QPushButton(QStringLiteral("导入清单"), this);
    m_createButton = new QPushButton(QStringLiteral("创建并开始分析"), this);
    m_cancelButton = new QPushButton(QStringLiteral("取消"), this);
    m_retryButton = new QPushButton(QStringLiteral("重试"), this);
    m_activateButton = new QPushButton(QStringLiteral("激活结果"), this);
    actions->addWidget(importButton);
    actions->addWidget(m_createButton);
    actions->addStretch(1);
    actions->addWidget(m_cancelButton);
    actions->addWidget(m_retryButton);
    actions->addWidget(m_activateButton);
    layout->addLayout(actions);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 1000);
    m_statusLabel = new QLabel(QStringLiteral("尚未创建分析批次。"), this);
    m_statusLabel->setWordWrap(true);
    layout->addWidget(m_progressBar);
    layout->addWidget(m_statusLabel);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(importButton, &QPushButton::clicked, this, [this]() { importManifest(); });
    connect(m_createButton, &QPushButton::clicked, this, [this]() { createBatch(); });
    connect(m_cancelButton, &QPushButton::clicked, this, [this]() { cancelRun(); });
    connect(m_retryButton, &QPushButton::clicked, this, [this]() { retryRun(); });
    connect(m_activateButton, &QPushButton::clicked, this, [this]() { activateRun(); });
    connect(&m_refreshTimer, &QTimer::timeout, this, [this]() { refreshRun(); });
    if (m_taskManager) {
        connect(m_taskManager, &AnalysisTaskManager::fullRateRunReady, this,
                [this](const OfflineAnalysisBatch &batch, const OfflineAnalysisRun &run) {
            setBatch(batch);
            setRun(run);
            QSettings().setValue(QStringLiteral("offlineAnalysis/lastBatchId"), batch.id);
        }, Qt::QueuedConnection);
        connect(m_taskManager, &AnalysisTaskManager::taskUpdated, this, [this](const QString &taskId) {
            if (m_batch.analysisTaskId != taskId) return;
            for (const AnalysisTask &task : m_taskManager->tasks()) {
                if (task.id != taskId) continue;
                m_progressBar->setValue(static_cast<int>(std::round(task.progress * 10.0)));
                m_statusLabel->setText(QStringLiteral("任务 %1 · %2 · %3%")
                                           .arg(task.id.left(8), task.status)
                                           .arg(task.progress, 0, 'f', 1));
                break;
            }
        }, Qt::QueuedConnection);
    }
    m_refreshTimer.setInterval(2000);

    const QString lastBatchId = QSettings().value(QStringLiteral("offlineAnalysis/lastBatchId")).toString();
    if (m_repository && m_repository->isOpen() && !lastBatchId.isEmpty()) {
        QString error;
        const OfflineAnalysisBatch batch = m_repository->offlineAnalysisBatch(lastBatchId, &error);
        if (!batch.id.isEmpty()) {
            setBatch(batch);
            if (!batch.runs.isEmpty()) {
                setRun(batch.runs.first());
                m_refreshTimer.start();
            }
        }
    }
    setRun(m_run);
}

void OfflineAnalysisDialog::importManifest()
{
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("选择 12 路分析清单"), QString(), QStringLiteral("JSON (*.json)"));
    if (path.isEmpty()) {
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, QStringLiteral("导入失败"), file.errorString());
        return;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    const QJsonArray sources = document.object().value(QStringLiteral("sources")).toArray();
    if (parseError.error != QJsonParseError::NoError || sources.size() != 12) {
        QMessageBox::warning(this, QStringLiteral("导入失败"), QStringLiteral("清单必须是包含 12 个 sources 的 JSON。"));
        return;
    }
    for (const QJsonValue &value : sources) {
        const QJsonObject source = value.toObject();
        const int row = source.value(QStringLiteral("cameraId")).toInt() - 1;
        if (row < 0 || row >= 12) {
            QMessageBox::warning(this, QStringLiteral("导入失败"), QStringLiteral("cameraId 必须覆盖 1-12。"));
            return;
        }
        m_sourcesTable->item(row, 1)->setText(source.value(QStringLiteral("sourceUri")).toString());
        m_sourcesTable->item(row, 2)->setText(QStringLiteral("%1x%2").arg(source.value(QStringLiteral("width")).toInt(1920))
                                                                     .arg(source.value(QStringLiteral("height")).toInt(1080)));
        m_sourcesTable->item(row, 3)->setText(QString::number(source.value(QStringLiteral("fps")).toDouble(60.0), 'f', 3));
        m_sourcesTable->item(row, 4)->setText(durationText(source.value(QStringLiteral("durationMs")).toInt()));
        m_sourcesTable->item(row, 5)->setText(source.value(QStringLiteral("sourceStartedAt")).toString());
        m_sourcesTable->item(row, 6)->setText(QString::number(source.value(QStringLiteral("manualCorrectionMs")).toInt()));
    }
}

OfflineAnalysisBatch OfflineAnalysisDialog::batchFromTable(QString *errorMessage) const
{
    OfflineAnalysisBatch batch;
    QDateTime earliest;
    for (int row = 0; row < 12; ++row) {
        OfflineAnalysisBatchSource source;
        source.cameraId = row + 1;
        source.sourceUri = m_sourcesTable->item(row, 1)->text().trimmed();
        if (!source.sourceUri.startsWith(QStringLiteral("nas://"))) {
            *errorMessage = QStringLiteral("CAM %1 不是 nas:// URI。").arg(row + 1);
            return {};
        }
        const QStringList resolution = m_sourcesTable->item(row, 2)->text().toLower().split(QLatin1Char('x'));
        bool widthOk = false;
        bool heightOk = false;
        bool fpsOk = false;
        bool correctionOk = false;
        source.width = resolution.value(0).toInt(&widthOk);
        source.height = resolution.value(1).toInt(&heightOk);
        source.fps = m_sourcesTable->item(row, 3)->text().toDouble(&fpsOk);
        source.durationMs = durationMs(m_sourcesTable->item(row, 4)->text());
        source.sourceStartedAt = QDateTime::fromString(m_sourcesTable->item(row, 5)->text(), Qt::ISODateWithMs);
        if (!source.sourceStartedAt.isValid()) {
            source.sourceStartedAt = QDateTime::fromString(m_sourcesTable->item(row, 5)->text(), Qt::ISODate);
        }
        source.manualCorrectionMs = m_sourcesTable->item(row, 6)->text().toInt(&correctionOk);
        if (!widthOk || !heightOk || !fpsOk || !correctionOk || source.width <= 0 || source.height <= 0
            || source.fps <= 0 || source.durationMs <= 0 || !source.sourceStartedAt.isValid()) {
            *errorMessage = QStringLiteral("CAM %1 的分辨率、FPS、时长、时间戳或校正值无效。").arg(row + 1);
            return {};
        }
        source.totalFrames = static_cast<qint64>(std::llround(source.fps * source.durationMs / 1000.0));
        source.fileName = source.sourceUri.section(QLatin1Char('/'), -1);
        batch.sources.append(source);
        if (!earliest.isValid() || source.sourceStartedAt < earliest) {
            earliest = source.sourceStartedAt;
        }
    }
    batch.sourceStartedAt = earliest;
    return batch;
}

void OfflineAnalysisDialog::createBatch()
{
    if (m_taskManager) {
        QSettings().setValue(QStringLiteral("offlineAnalysis/nasRoot"), m_nasRootEdit->text().trimmed());
        QString error;
        const OfflineAnalysisBatch batch = batchFromTable(&error);
        if (batch.sources.size() != 12) {
            QMessageBox::warning(this, QStringLiteral("配置无效"), error);
            return;
        }
        m_createButton->setEnabled(false);
        m_statusLabel->setText(QStringLiteral("已进入后台队列，等待创建批次和提交远端运行。"));
        m_taskManager->enqueueFullRateBatch(batch, m_athleteIds);
        return;
    }
    if (!m_repository || !m_repository->isOpen()) {
        QMessageBox::warning(this, QStringLiteral("服务不可用"), QStringLiteral("训练服务未连接。"));
        return;
    }
    QSettings().setValue(QStringLiteral("offlineAnalysis/nasRoot"), m_nasRootEdit->text().trimmed());
    QString error;
    OfflineAnalysisBatch batch = batchFromTable(&error);
    if (batch.sources.size() != 12) {
        QMessageBox::warning(this, QStringLiteral("配置无效"), error);
        return;
    }
    if (!m_repository->createOfflineAnalysisBatch(&batch, m_athleteIds, &error)) {
        QMessageBox::warning(this, QStringLiteral("创建失败"), error);
        return;
    }
    setBatch(batch);
    OfflineAnalysisRun run;
    if (!m_repository->createOfflineAnalysisRun(batch.id,
                                                QString::fromLatin1(kModelVersion),
                                                QString::fromLatin1(kPreprocessingVersion),
                                                &run,
                                                &error)) {
        QMessageBox::warning(this, QStringLiteral("启动失败"), error);
        return;
    }
    setRun(run);
    QSettings().setValue(QStringLiteral("offlineAnalysis/lastBatchId"), batch.id);
    m_refreshTimer.start();
}

void OfflineAnalysisDialog::setBatch(const OfflineAnalysisBatch &batch)
{
    m_batch = batch;
    for (const OfflineAnalysisBatchSource &source : batch.sources) {
        const int row = source.cameraId - 1;
        if (row < 0 || row >= 12) continue;
        m_sourcesTable->item(row, 1)->setText(source.sourceUri);
        m_sourcesTable->item(row, 2)->setText(QStringLiteral("%1x%2").arg(source.width).arg(source.height));
        m_sourcesTable->item(row, 3)->setText(QString::number(source.fps));
        m_sourcesTable->item(row, 4)->setText(durationText(source.durationMs));
        if (source.sourceStartedAt.isValid()) m_sourcesTable->item(row, 5)->setText(source.sourceStartedAt.toString(Qt::ISODateWithMs));
        m_sourcesTable->item(row, 6)->setText(QString::number(source.manualCorrectionMs));
    }
}

void OfflineAnalysisDialog::setRun(const OfflineAnalysisRun &run)
{
    m_run = run;
    const bool active = run.status == QStringLiteral("queued") || run.status == QStringLiteral("running")
                        || run.status == QStringLiteral("partial");
    m_createButton->setEnabled(run.id.isEmpty() || run.status == QStringLiteral("completed")
                               || run.status == QStringLiteral("failed") || run.status == QStringLiteral("cancelled"));
    m_cancelButton->setEnabled(active);
    m_retryButton->setEnabled(run.status == QStringLiteral("failed") || run.status == QStringLiteral("partial")
                              || run.status == QStringLiteral("cancelled"));
    m_activateButton->setEnabled(run.status == QStringLiteral("completed") && m_batch.activeRunId != run.id);
    m_progressBar->setValue(static_cast<int>(std::clamp(run.progress, 0.0, 1.0) * 1000.0));
    if (run.id.isEmpty()) {
        return;
    }
    const QString eta = run.estimatedRemainingSeconds >= 0
                            ? QStringLiteral("，预计剩余 %1 秒").arg(run.estimatedRemainingSeconds)
                            : QString();
    m_statusLabel->setText(QStringLiteral("运行 %1 · %2 · %3/%4 帧 · %5 FPS%6%7")
                               .arg(run.id.left(8), run.status)
                               .arg(run.processedFrames)
                               .arg(run.totalFrames)
                               .arg(run.throughputFps, 0, 'f', 1)
                               .arg(eta, run.errorMessage.isEmpty() ? QString() : QStringLiteral(" · %1").arg(run.errorMessage)));
    for (const OfflineAnalysisRunSource &source : run.sources) {
        const int row = source.cameraId - 1;
        if (row < 0 || row >= 12) continue;
        m_sourcesTable->item(row, 7)->setText(QStringLiteral("%1 · %2/%3%4")
                                                  .arg(source.status)
                                                  .arg(source.processedFrames)
                                                  .arg(source.totalFrames)
                                                  .arg(source.errorMessage.isEmpty() ? QString() : QStringLiteral(" · %1").arg(source.errorMessage)));
    }
    if (!active) {
        m_refreshTimer.stop();
    }
}

void OfflineAnalysisDialog::refreshRun()
{
    if (m_run.id.isEmpty() || !m_repository) return;
    QString error;
    const OfflineAnalysisRun run = m_repository->offlineAnalysisRun(m_run.id, &error);
    if (run.id.isEmpty()) {
        m_statusLabel->setText(QStringLiteral("刷新失败：%1").arg(error));
        return;
    }
    setRun(run);
}

void OfflineAnalysisDialog::cancelRun()
{
    QString error;
    OfflineAnalysisRun run;
    if (!m_repository->cancelOfflineAnalysisRun(m_run.id, &run, &error)) {
        QMessageBox::warning(this, QStringLiteral("取消失败"), error);
        return;
    }
    setRun(run);
}

void OfflineAnalysisDialog::retryRun()
{
    QString error;
    OfflineAnalysisRun run;
    if (!m_repository->retryOfflineAnalysisRun(m_run.id, &run, &error)) {
        QMessageBox::warning(this, QStringLiteral("重试失败"), error);
        return;
    }
    setRun(run);
    m_refreshTimer.start();
}

void OfflineAnalysisDialog::activateRun()
{
    QString error;
    OfflineAnalysisRun run;
    if (!m_repository->activateOfflineAnalysisRun(m_run.id, &run, &error)) {
        QMessageBox::warning(this, QStringLiteral("激活失败"), error);
        return;
    }
    m_batch.activeRunId = run.id;
    setRun(run);
    QMessageBox::information(this, QStringLiteral("已激活"), QStringLiteral("训练回放将使用该完整帧率分析版本。"));
}

#include "offlineanalysisdialog.h"

#include "analysistaskmanager.h"
#include "analysisuipresentation.h"
#include "animatedbutton.h"
#include "trainingrepository.h"

#include <QAbstractItemView>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QStyle>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace
{
constexpr const char *kModelVersion = "personvit-msmt17-vit-base-v1";
constexpr const char *kPreprocessingVersion = "rgb-256x128-mean0.5-std0.5-l2-v1";

AnimatedButton *button(const QString &text, const QString &variant, QWidget *parent)
{
    auto *result = new AnimatedButton(parent);
    result->setText(text);
    result->setProperty("variant", variant);
    result->setMinimumHeight(36);
    return result;
}
QLabel *title(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("sectionTitle"));
    return label;
}
QTableWidgetItem *item(const QString &text, bool editable = true)
{
    auto *result = new QTableWidgetItem(text);
    if (!editable)
        result->setFlags(result->flags() & ~Qt::ItemIsEditable);
    result->setToolTip(text);
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
    if (parts.size() != 3)
    {
        return -1;
    }
    bool okHour = false;
    bool okMinute = false;
    bool okSecond = false;
    const int hour = parts.at(0).toInt(&okHour);
    const int minute = parts.at(1).toInt(&okMinute);
    const int second = parts.at(2).toInt(&okSecond);
    if (!okHour || !okMinute || !okSecond || hour < 0 || minute < 0 || minute > 59 || second < 0 || second > 59)
    {
        return -1;
    }
    return (hour * 3600 + minute * 60 + second) * 1000;
}
void addValue(QGridLayout *grid, int row, const QString &name, QLabel **value, QWidget *parent)
{
    auto *label = new QLabel(name, parent);
    label->setObjectName(QStringLiteral("settingLabel"));
    *value = new QLabel(QStringLiteral("—"), parent);
    (*value)->setTextInteractionFlags(Qt::TextSelectableByMouse);
    (*value)->setTextFormat(Qt::PlainText);
    (*value)->setWordWrap(true);
    grid->addWidget(label, row, 0);
    grid->addWidget(*value, row, 1);
}
} // namespace

OfflineAnalysisDialog::OfflineAnalysisDialog(TrainingRepository *repository, QVector<QString> athleteIds,
                                             AnalysisTaskManager *taskManager, QWidget *parent)
    : FramelessDialog(parent), m_repository(repository), m_taskManager(taskManager), m_athleteIds(std::move(athleteIds))
{
    setDialogTitle(QStringLiteral("完整分析"));
    setMinimumSize(1000, 760);
    resize(1240, 860);
    setSizeGripEnabled(true);
    auto *root = contentLayout();
    root->setContentsMargins(18, 14, 18, 16);
    root->setSpacing(9);
    auto *description = new QLabel(QStringLiteral("配置并执行 12 路完整帧率离线分析"), this);
    description->setObjectName(QStringLiteral("pageDescription"));
    root->addWidget(description);
    root->addWidget(title(QStringLiteral("分析输入"), this));
    auto *nas = new QGridLayout;
    nas->addWidget(new QLabel(QStringLiteral("Windows NAS 根目录"), this), 0, 0);
    m_nasRootEdit = new QLineEdit(QSettings().value(QStringLiteral("offlineAnalysis/nasRoot")).toString(), this);
    m_nasRootEdit->setPlaceholderText(QStringLiteral("例如 Z:/iskating 或 \\\\nas\\iskating"));
    nas->addWidget(m_nasRootEdit, 0, 1);
    auto *nasHelp = new QLabel(QStringLiteral("用于将 Windows 路径映射到 Ubuntu 分析主机可访问的 nas:// URI。"), this);
    nasHelp->setObjectName(QStringLiteral("pageDescription"));
    nas->addWidget(nasHelp, 1, 1);
    root->addLayout(nas);

    auto *sourceHeader = new QHBoxLayout;
    sourceHeader->addWidget(title(QStringLiteral("12 路源配置"), this));
    sourceHeader->addStretch();
    auto *import = button(QStringLiteral("导入清单"), QStringLiteral("subtle"), this);
    sourceHeader->addWidget(import);
    root->addLayout(sourceHeader);
    m_inlineStatus = new QLabel(
        QStringLiteral("NAS URI 需使用 nas://；时长格式 00:05:30；源开始时间示例 2026-09-09T10:30:00.000Z。"), this);
    m_inlineStatus->setObjectName(QStringLiteral("actionStatus"));
    m_inlineStatus->setTextFormat(Qt::PlainText);
    m_inlineStatus->setWordWrap(true);
    root->addWidget(m_inlineStatus);
    m_sourcesTable = new QTableWidget(12, 8, this);
    m_sourcesTable->setHorizontalHeaderLabels(
        {QStringLiteral("机位"), QStringLiteral("NAS URI"), QStringLiteral("分辨率"), QStringLiteral("FPS"),
         QStringLiteral("时长"), QStringLiteral("源开始时间"), QStringLiteral("校正 ms"), QStringLiteral("状态")});
    m_sourcesTable->verticalHeader()->hide();
    m_sourcesTable->setWordWrap(false);
    m_sourcesTable->setMinimumHeight(160);
    m_sourcesTable->setShowGrid(false);
    m_sourcesTable->setTextElideMode(Qt::ElideMiddle);
    m_sourcesTable->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *header = m_sourcesTable->horizontalHeader();
    header->setSectionResizeMode(QHeaderView::Interactive);
    header->setSectionResizeMode(1, QHeaderView::Stretch);
    header->setSectionResizeMode(5, QHeaderView::Fixed);
    m_sourcesTable->setColumnWidth(0, 68);
    m_sourcesTable->setColumnWidth(2, 98);
    m_sourcesTable->setColumnWidth(3, 58);
    m_sourcesTable->setColumnWidth(4, 82);
    m_sourcesTable->setColumnWidth(5, 220);
    m_sourcesTable->setColumnWidth(6, 78);
    m_sourcesTable->setColumnWidth(7, 92);
    m_sourcesTable->verticalHeader()->setDefaultSectionSize(34);
    for (int row = 0; row < 12; ++row)
    {
        m_sourcesTable->setItem(row, 0, item(QStringLiteral("CAM %1").arg(row + 1, 2, 10, QLatin1Char('0')), false));
        m_sourcesTable->setItem(row, 1, item({}));
        m_sourcesTable->item(row, 1)->setData(
            Qt::ToolTipRole, QStringLiteral("例如 nas://videos/camera%1.mp4").arg(row + 1, 2, 10, QLatin1Char('0')));
        m_sourcesTable->setItem(row, 2, item({}));
        m_sourcesTable->setItem(row, 3, item({}));
        m_sourcesTable->setItem(row, 4, item({}));
        m_sourcesTable->setItem(row, 5, item({}));
        m_sourcesTable->setItem(row, 6, item(QStringLiteral("0")));
        m_sourcesTable->setItem(row, 7, item(QStringLiteral("待配置"), false));
    }
    connect(m_sourcesTable, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *changed) {
        const QSignalBlocker blocker(m_sourcesTable);
        if (changed->column() >= 1 && changed->column() <= 6)
            changed->setToolTip(changed->text());
        if (changed->column() >= 1 && changed->column() <= 6 && m_run.id.isEmpty())
            m_sourcesTable->item(changed->row(), 7)->setText(QStringLiteral("已编辑"));
    });
    root->addWidget(m_sourcesTable, 1);

    auto *lower = new QHBoxLayout;
    lower->setSpacing(18);
    auto *config = new QWidget(this);
    config->setObjectName(QStringLiteral("settingsBody"));
    auto *configLayout = new QVBoxLayout(config);
    configLayout->addWidget(title(QStringLiteral("运行配置"), config));
    auto *model = new QLabel(
        QStringLiteral("12 路 · 完整帧率 · 离线分析\n\n模型版本\n%1").arg(QString::fromLatin1(kModelVersion)), config);
    model->setWordWrap(true);
    model->setTextInteractionFlags(Qt::TextSelectableByMouse);
    configLayout->addWidget(model);
    configLayout->addStretch();
    lower->addWidget(config, 1);
    auto *status = new QWidget(this);
    status->setObjectName(QStringLiteral("settingsBody"));
    auto *statusLayout = new QVBoxLayout(status);
    statusLayout->addWidget(title(QStringLiteral("当前分析"), status));
    m_emptyRunLabel = new QLabel(QStringLiteral("尚未创建分析批次"), status);
    m_emptyRunLabel->setObjectName(QStringLiteral("pageDescription"));
    statusLayout->addWidget(m_emptyRunLabel);
    m_runDetails = new QWidget(status);
    auto *statusGrid = new QGridLayout;
    addValue(statusGrid, 0, QStringLiteral("状态"), &m_runStatus, status);
    addValue(statusGrid, 1, QStringLiteral("Batch"), &m_batchIdLabel, status);
    addValue(statusGrid, 2, QStringLiteral("Run"), &m_runIdLabel, status);
    addValue(statusGrid, 3, QStringLiteral("模型版本"), &m_modelLabel, status);
    addValue(statusGrid, 4, QStringLiteral("开始时间"), &m_startedAtLabel, status);
    addValue(statusGrid, 5, QStringLiteral("帧"), &m_framesLabel, status);
    addValue(statusGrid, 6, QStringLiteral("吞吐"), &m_throughputLabel, status);
    addValue(statusGrid, 7, QStringLiteral("预计剩余"), &m_etaLabel, status);
    for (int row = 4; row < 8; ++row)
    {
        QLayoutItem *labelItem = statusGrid->itemAtPosition(row, 0);
        QLayoutItem *valueItem = statusGrid->itemAtPosition(row, 1);
        statusGrid->removeItem(labelItem);
        statusGrid->removeItem(valueItem);
        statusGrid->addItem(labelItem, row - 4, 2);
        statusGrid->addItem(valueItem, row - 4, 3);
    }
    m_runDetails->setLayout(statusGrid);
    statusGrid->setContentsMargins(0, 0, 0, 0);
    statusLayout->addWidget(m_runDetails);
    m_progressBar = new QProgressBar(status);
    m_progressBar->setRange(0, 1000);
    m_progressBar->setTextVisible(false);
    m_progressText = new QLabel(QStringLiteral("0%"), status);
    auto *progress = new QHBoxLayout;
    progress->addWidget(m_progressBar, 1);
    progress->addWidget(m_progressText);
    statusLayout->addLayout(progress);
    m_errorText = new QPlainTextEdit(status);
    m_errorText->setReadOnly(true);
    m_errorText->setMaximumHeight(64);
    m_errorText->hide();
    statusLayout->addWidget(m_errorText);
    lower->addWidget(status, 2);
    root->addLayout(lower);
    auto *actions = new QHBoxLayout;
    m_createButton = button(QStringLiteral("创建并开始分析"), QStringLiteral("primary"), this);
    m_cancelButton = button(QStringLiteral("取消分析"), QStringLiteral("danger"), this);
    m_retryButton = button(QStringLiteral("重试"), QStringLiteral("subtle"), this);
    m_activateButton = button(QStringLiteral("激活结果"), QStringLiteral("subtle"), this);
    actions->addWidget(m_createButton);
    actions->addStretch();
    actions->addWidget(m_cancelButton);
    actions->addWidget(m_retryButton);
    actions->addWidget(m_activateButton);
    root->addLayout(actions);

    connect(import, &QPushButton::clicked, this, [this]() { importManifest(); });
    connect(m_createButton, &QPushButton::clicked, this, [this]() { createBatch(); });
    connect(m_cancelButton, &QPushButton::clicked, this, [this]() { cancelRun(); });
    connect(m_retryButton, &QPushButton::clicked, this, [this]() { retryRun(); });
    connect(m_activateButton, &QPushButton::clicked, this, [this]() { activateRun(); });
    connect(&m_refreshTimer, &QTimer::timeout, this, [this]() { refreshRun(); });
    if (m_taskManager)
    {
        connect(
            m_taskManager, &AnalysisTaskManager::fullRateRunReady, this,
            [this](const OfflineAnalysisBatch &batch, const OfflineAnalysisRun &run) {
                m_submissionPending = false;
                m_inlineStatus->setText(QStringLiteral("已创建分析批次"));
                setBatch(batch);
                setRun(run);
                QSettings().setValue(QStringLiteral("offlineAnalysis/lastBatchId"), batch.id);
            },
            Qt::QueuedConnection);
        connect(
            m_taskManager, &AnalysisTaskManager::taskUpdated, this,
            [this](const QString &id) {
                if (m_submissionPending || m_batch.analysisTaskId != id)
                    return;
                for (const auto &task : m_taskManager->tasks())
                {
                    if (task.id != id)
                        continue;
                    const double percent = AnalysisUiPresentation::taskProgressPercent(task);
                    if (std::isfinite(percent) && task.progress >= 0.0)
                    {
                        m_progressBar->setValue(qRound(percent * 10.0));
                        m_progressText->setText(QStringLiteral("%1%").arg(percent, 0, 'f', 1));
                    }
                    m_runStatus->setText(AnalysisUiPresentation::status(task.status));
                    m_runStatus->setProperty("state", AnalysisUiPresentation::semanticStatus(task.status));
                    m_runStatus->style()->unpolish(m_runStatus);
                    m_runStatus->style()->polish(m_runStatus);
                    if (!task.errorMessage.isEmpty())
                    {
                        m_errorText->setPlainText(task.errorMessage);
                        m_errorText->show();
                    }
                    break;
                }
            },
            Qt::QueuedConnection);
        connect(
            m_taskManager, &AnalysisTaskManager::taskError, this,
            [this](const QString &id, const QString &message) {
                if (!m_submissionPending)
                    return;
                bool isSubmittedBatch = id.isEmpty();
                for (const auto &task : m_taskManager->tasks())
                    if (task.id == id && !m_tasksBeforeSubmission.contains(id) &&
                        task.type == QStringLiteral("full_rate_batch"))
                        isSubmittedBatch = true;
                if (!isSubmittedBatch)
                    return;
                m_submissionPending = false;
                m_inlineStatus->setText(QStringLiteral("创建分析失败，请查看错误信息后重试。"));
                updateRunPresentation();
                m_errorText->setPlainText(message);
                m_errorText->show();
            },
            Qt::QueuedConnection);
    }
    m_refreshTimer.setInterval(2000);
    const QString last = QSettings().value(QStringLiteral("offlineAnalysis/lastBatchId")).toString();
    if (m_repository && m_repository->isOpen() && !last.isEmpty())
    {
        QString error;
        const auto batch = m_repository->offlineAnalysisBatch(last, &error);
        if (!batch.id.isEmpty())
        {
            setBatch(batch);
            if (!batch.runs.isEmpty())
                setRun(batch.runs.first());
        }
    }
    updateRunPresentation();
}

void OfflineAnalysisDialog::importManifest()
{
    const QString path =
        QFileDialog::getOpenFileName(this, QStringLiteral("选择 12 路分析清单"), {}, QStringLiteral("JSON (*.json)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        QMessageBox::warning(this, QStringLiteral("导入失败"), file.errorString());
        return;
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    const auto sources = document.object().value(QStringLiteral("sources")).toArray();
    if (parseError.error != QJsonParseError::NoError || sources.size() != 12)
    {
        QMessageBox::warning(this, QStringLiteral("导入失败"),
                             QStringLiteral("清单必须是包含 12 个 sources 的 JSON。"));
        return;
    }
    QVector<QJsonObject> byCamera(12);
    QVector<bool> seen(12, false);
    for (const auto &value : sources)
    {
        const auto source = value.toObject();
        const int row = source.value(QStringLiteral("cameraId")).toInt() - 1;
        if (row < 0 || row >= 12 || seen[row])
        {
            QMessageBox::warning(this, QStringLiteral("导入失败"), QStringLiteral("cameraId 必须唯一覆盖 1-12。"));
            return;
        }
        seen[row] = true;
        byCamera[row] = source;
    }
    for (int row = 0; row < 12; ++row)
    {
        const auto source = byCamera[row];
        m_sourcesTable->item(row, 1)->setText(source.value(QStringLiteral("sourceUri")).toString());
        m_sourcesTable->item(row, 2)->setText(QStringLiteral("%1x%2")
                                                  .arg(source.value(QStringLiteral("width")).toInt(1920))
                                                  .arg(source.value(QStringLiteral("height")).toInt(1080)));
        m_sourcesTable->item(row, 3)->setText(
            QString::number(source.value(QStringLiteral("fps")).toDouble(60), 'f', 3));
        m_sourcesTable->item(row, 4)->setText(durationText(source.value(QStringLiteral("durationMs")).toInt()));
        m_sourcesTable->item(row, 5)->setText(source.value(QStringLiteral("sourceStartedAt")).toString());
        m_sourcesTable->item(row, 6)->setText(
            QString::number(source.value(QStringLiteral("manualCorrectionMs")).toInt()));
        m_sourcesTable->item(row, 7)->setText(QStringLiteral("已导入"));
    }
    m_inlineStatus->setText(QStringLiteral("已导入 12 路源配置"));
}

OfflineAnalysisBatch OfflineAnalysisDialog::batchFromTable(QString *errorMessage) const
{
    OfflineAnalysisBatch batch;
    QDateTime earliest;
    for (int row = 0; row < 12; ++row)
    {
        OfflineAnalysisBatchSource source;
        source.cameraId = row + 1;
        source.sourceUri = m_sourcesTable->item(row, 1)->text().trimmed();
        if (!source.sourceUri.startsWith(QStringLiteral("nas://")))
        {
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
        if (!source.sourceStartedAt.isValid())
        {
            source.sourceStartedAt = QDateTime::fromString(m_sourcesTable->item(row, 5)->text(), Qt::ISODate);
        }
        source.manualCorrectionMs = m_sourcesTable->item(row, 6)->text().toInt(&correctionOk);
        if (!widthOk || !heightOk || !fpsOk || !correctionOk || source.width <= 0 || source.height <= 0 ||
            source.fps <= 0 || source.durationMs <= 0 || !source.sourceStartedAt.isValid())
        {
            *errorMessage = QStringLiteral("CAM %1 的分辨率、FPS、时长、时间戳或校正值无效。").arg(row + 1);
            return {};
        }
        source.totalFrames = static_cast<qint64>(std::llround(source.fps * source.durationMs / 1000.0));
        source.fileName = source.sourceUri.section(QLatin1Char('/'), -1);
        batch.sources.append(source);
        if (!earliest.isValid() || source.sourceStartedAt < earliest)
        {
            earliest = source.sourceStartedAt;
        }
    }
    batch.sourceStartedAt = earliest;
    return batch;
}

void OfflineAnalysisDialog::createBatch()
{
    QSettings().setValue(QStringLiteral("offlineAnalysis/nasRoot"), m_nasRootEdit->text().trimmed());
    QString error;
    OfflineAnalysisBatch batch = batchFromTable(&error);
    if (batch.sources.size() != 12)
    {
        m_inlineStatus->setText(error);
        QMessageBox::warning(this, QStringLiteral("配置无效"), error);
        return;
    }
    if (m_taskManager)
    {
        m_tasksBeforeSubmission.clear();
        for (const auto &task : m_taskManager->tasks())
            m_tasksBeforeSubmission.insert(task.id);
        m_submissionPending = true;
        updateRunPresentation();
        m_inlineStatus->setText(QStringLiteral("已进入后台队列"));
        m_taskManager->enqueueFullRateBatch(batch, m_athleteIds);
        return;
    }
    if (!m_repository || !m_repository->isOpen())
    {
        QMessageBox::warning(this, QStringLiteral("服务不可用"), QStringLiteral("训练服务未连接。"));
        return;
    }
    if (!m_repository->createOfflineAnalysisBatch(&batch, m_athleteIds, &error))
    {
        QMessageBox::warning(this, QStringLiteral("创建失败"), error);
        return;
    }
    setBatch(batch);
    OfflineAnalysisRun run;
    if (!m_repository->createOfflineAnalysisRun(batch.id, QString::fromLatin1(kModelVersion),
                                                QString::fromLatin1(kPreprocessingVersion), &run, &error))
    {
        QMessageBox::warning(this, QStringLiteral("启动失败"), error);
        return;
    }
    setRun(run);
    QSettings().setValue(QStringLiteral("offlineAnalysis/lastBatchId"), batch.id);
}

void OfflineAnalysisDialog::setBatch(const OfflineAnalysisBatch &batch)
{
    const QSignalBlocker blocker(m_sourcesTable);
    if (m_batch.id != batch.id)
    {
        m_run = {};
        m_refreshTimer.stop();
    }
    m_batch = batch;
    for (int row = 0; row < 12; ++row)
        m_sourcesTable->item(row, 7)->setText(QStringLiteral("已配置"));
    for (const auto &source : batch.sources)
    {
        const int row = source.cameraId - 1;
        if (row < 0 || row >= 12)
            continue;
        m_sourcesTable->item(row, 1)->setText(source.sourceUri);
        m_sourcesTable->item(row, 2)->setText(QStringLiteral("%1x%2").arg(source.width).arg(source.height));
        m_sourcesTable->item(row, 3)->setText(QString::number(source.fps));
        m_sourcesTable->item(row, 4)->setText(durationText(source.durationMs));
        if (source.sourceStartedAt.isValid())
            m_sourcesTable->item(row, 5)->setText(source.sourceStartedAt.toString(Qt::ISODateWithMs));
        m_sourcesTable->item(row, 6)->setText(QString::number(source.manualCorrectionMs));
        for (int column = 1; column <= 6; ++column)
            m_sourcesTable->item(row, column)->setToolTip(m_sourcesTable->item(row, column)->text());
    }
    updateRunPresentation();
}
void OfflineAnalysisDialog::setRun(const OfflineAnalysisRun &run)
{
    const QSignalBlocker blocker(m_sourcesTable);
    if (m_run.id != run.id)
    {
        for (int row = 0; row < 12; ++row)
        {
            m_sourcesTable->item(row, 7)->setText(QStringLiteral("—"));
            m_sourcesTable->item(row, 7)->setToolTip({});
        }
    }
    m_run = run;
    const bool active = run.status == QStringLiteral("queued") || run.status == QStringLiteral("running") ||
                        run.status == QStringLiteral("partial");
    for (const auto &source : run.sources)
    {
        const int row = source.cameraId - 1;
        if (row < 0 || row >= 12)
            continue;
        const QString text = QStringLiteral("%1 · %2/%3")
                                 .arg(AnalysisUiPresentation::status(source.status))
                                 .arg(source.processedFrames)
                                 .arg(source.totalFrames);
        m_sourcesTable->item(row, 7)->setText(text);
        m_sourcesTable->item(row, 7)->setToolTip(
            source.errorMessage.isEmpty() ? text : text + QStringLiteral("\n") + source.errorMessage);
    }
    if (active)
        m_refreshTimer.start();
    else
        m_refreshTimer.stop();
    updateRunPresentation();
}
void OfflineAnalysisDialog::updateRunPresentation()
{
    const bool hasBatch = !m_batch.id.isEmpty();
    m_runDetails->setVisible(hasBatch);
    m_emptyRunLabel->setVisible(!hasBatch);
    m_emptyRunLabel->setText(m_submissionPending ? QStringLiteral("已进入后台队列")
                                                 : QStringLiteral("尚未创建分析批次"));
    m_progressBar->setVisible(hasBatch || m_submissionPending);
    m_progressText->setVisible(hasBatch || m_submissionPending);
    const bool active = m_run.status == QStringLiteral("queued") || m_run.status == QStringLiteral("running") ||
                        m_run.status == QStringLiteral("partial");
    m_createButton->setEnabled(!m_submissionPending &&
                               (m_run.id.isEmpty() || m_run.status == QStringLiteral("completed") ||
                                m_run.status == QStringLiteral("failed") ||
                                m_run.status == QStringLiteral("cancelled")));
    const bool canRetry = m_run.status == QStringLiteral("failed") || m_run.status == QStringLiteral("partial") ||
                          m_run.status == QStringLiteral("cancelled");
    const bool canActivate = m_run.status == QStringLiteral("completed") && m_batch.activeRunId != m_run.id;
    m_cancelButton->setVisible(active);
    m_cancelButton->setEnabled(active);
    m_retryButton->setVisible(canRetry);
    m_retryButton->setEnabled(canRetry);
    m_activateButton->setVisible(canActivate);
    m_activateButton->setEnabled(canActivate);
    m_activateButton->setProperty("variant", QStringLiteral("secondary"));
    m_batchIdLabel->setText(m_batch.id.isEmpty() ? QStringLiteral("尚未创建分析批次") : m_batch.id);
    m_runIdLabel->setText(m_run.id.isEmpty() ? QStringLiteral("—") : m_run.id);
    m_runStatus->setText(
        m_submissionPending
            ? QStringLiteral("已进入后台队列")
            : (m_run.status == QStringLiteral("failed")
                   ? QStringLiteral("分析失败")
                   : (m_run.id.isEmpty() ? QStringLiteral("尚未创建") : AnalysisUiPresentation::status(m_run.status))));
    m_runStatus->setProperty("state", AnalysisUiPresentation::semanticStatus(m_run.status));
    m_runStatus->style()->unpolish(m_runStatus);
    m_runStatus->style()->polish(m_runStatus);
    m_modelLabel->setText(m_run.modelVersion.isEmpty() ? QStringLiteral("—") : m_run.modelVersion);
    m_modelLabel->setToolTip(m_run.modelVersion);
    if (m_run.modelVersion.size() > 22)
        m_modelLabel->setText(m_run.modelVersion.left(22) + QStringLiteral("…"));
    m_startedAtLabel->setText(m_run.startedAt.isValid()
                                  ? m_run.startedAt.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
                                  : QStringLiteral("—"));
    m_framesLabel->setText(m_run.id.isEmpty()
                               ? QStringLiteral("—")
                               : QStringLiteral("%1 / %2").arg(m_run.processedFrames).arg(m_run.totalFrames));
    m_throughputLabel->setText(m_run.id.isEmpty() ? QStringLiteral("—")
                                                  : QStringLiteral("%1 FPS").arg(m_run.throughputFps, 0, 'f', 1));
    m_etaLabel->setText(m_run.estimatedRemainingSeconds >= 0
                            ? QStringLiteral("%1 秒").arg(m_run.estimatedRemainingSeconds)
                            : QStringLiteral("—"));
    const double percent = std::clamp(m_run.progress, 0.0, 1.0) * 100.0;
    m_progressBar->setValue(qRound(percent * 10));
    m_progressText->setText(QStringLiteral("%1%").arg(percent, 0, 'f', 1));
    m_errorText->setPlainText(m_run.errorMessage);
    m_errorText->setVisible(!m_run.errorMessage.isEmpty());
}
void OfflineAnalysisDialog::refreshRun()
{
    if (m_run.id.isEmpty() || !m_repository)
        return;
    QString error;
    const auto run = m_repository->offlineAnalysisRun(m_run.id, &error);
    if (run.id.isEmpty())
    {
        m_inlineStatus->setText(QStringLiteral("运行状态暂未同步，请查看错误信息。"));
        m_errorText->setPlainText(error);
        m_errorText->show();
        return;
    }
    setRun(run);
}
void OfflineAnalysisDialog::cancelRun()
{
    if (!m_repository)
        return;
    QString error;
    OfflineAnalysisRun run;
    if (!m_repository->cancelOfflineAnalysisRun(m_run.id, &run, &error))
    {
        QMessageBox::warning(this, QStringLiteral("取消失败"), error);
        return;
    }
    setRun(run);
}
void OfflineAnalysisDialog::retryRun()
{
    if (!m_repository)
        return;
    QString error;
    OfflineAnalysisRun run;
    if (!m_repository->retryOfflineAnalysisRun(m_run.id, &run, &error))
    {
        QMessageBox::warning(this, QStringLiteral("重试失败"), error);
        return;
    }
    setRun(run);
}
void OfflineAnalysisDialog::activateRun()
{
    if (!m_repository)
        return;
    QString error;
    OfflineAnalysisRun run;
    if (!m_repository->activateOfflineAnalysisRun(m_run.id, &run, &error))
    {
        QMessageBox::warning(this, QStringLiteral("激活失败"), error);
        return;
    }
    m_batch.activeRunId = run.id;
    setRun(run);
}

#include "trainingreviewdialog.h"

#include "nvrplayback.h"
#include "trainingrepository.h"
#include "videoopenglwidget.h"

#include <QCheckBox>
#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace {

bool hasMediaUrlScheme(const QString &source)
{
    return source.trimmed().contains(QStringLiteral("://"));
}

QString displaySource(const QString &source)
{
    const QString trimmed = source.trimmed();
    if (trimmed.isEmpty()) {
        return QStringLiteral("未记录");
    }
    if (!hasMediaUrlScheme(trimmed)) {
        return QDir::toNativeSeparators(QFileInfo(trimmed).absoluteFilePath());
    }
    QUrl url = QUrl::fromEncoded(trimmed.toUtf8(), QUrl::TolerantMode);
    if (!url.password().isEmpty()) {
        url.setPassword(QStringLiteral("***"));
    }
    return url.toString(QUrl::RemoveQuery | QUrl::RemoveFragment);
}

QString formatMilliseconds(int milliseconds)
{
    const int clamped = std::max(0, milliseconds);
    const int totalSeconds = clamped / 1000;
    return QStringLiteral("%1:%2.%3")
        .arg(totalSeconds / 60, 2, 10, QLatin1Char('0'))
        .arg(totalSeconds % 60, 2, 10, QLatin1Char('0'))
        .arg((clamped % 1000) / 100, 1, 10, QLatin1Char('0'));
}

QString issueSummary(const QString &errorCodes)
{
    const QStringList issues = errorCodes.split(QStringLiteral("|"), Qt::SkipEmptyParts);
    return issues.isEmpty() ? QStringLiteral("未触发关键错误") : issues.join(QStringLiteral("、"));
}

QSpinBox *makeScoreSpinBox(QWidget *parent)
{
    auto *spin = new QSpinBox(parent);
    spin->setRange(0, 100);
    return spin;
}

QTableWidgetItem *readOnlyItem(const QString &text)
{
    auto *item = new QTableWidgetItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
}

QString cameraSettingsGroup(int cameraIndex)
{
    return QStringLiteral("cameras/camera%1").arg(cameraIndex + 1, 2, 10, QLatin1Char('0'));
}

SharedCameraSettings loadSharedCameraSettings()
{
    SharedCameraSettings settings;
    QSettings qsettings;
    qsettings.beginGroup(QStringLiteral("cameraDefaults"));
    settings.username = qsettings.value(QStringLiteral("username")).toString().trimmed();
    settings.password = qsettings.value(QStringLiteral("password")).toString();
    settings.port = qsettings.value(QStringLiteral("port"), QStringLiteral("554")).toString().trimmed();
    if (settings.port.isEmpty()) {
        settings.port = QStringLiteral("554");
    }
    settings.nvrPlaybackTemplate = qsettings.value(QStringLiteral("nvrPlaybackTemplate")).toString().trimmed();
    qsettings.endGroup();
    return settings;
}

QVector<CameraSlotSettings> loadCameraSlotSettings(int cameraCount)
{
    QVector<CameraSlotSettings> cameraSlots;
    cameraSlots.reserve(cameraCount);
    QSettings qsettings;
    for (int i = 0; i < cameraCount; ++i) {
        CameraSlotSettings slot;
        qsettings.beginGroup(cameraSettingsGroup(i));
        slot.ip = qsettings.value(QStringLiteral("ip")).toString().trimmed();
        qsettings.endGroup();
        cameraSlots.append(slot);
    }
    return cameraSlots;
}

} // namespace

TrainingReviewDialog::TrainingReviewDialog(const SessionHistoryItem &record,
                                           const ActionStandard &standard,
                                           TrainingRepository *repository,
                                           QWidget *parent)
    : QDialog(parent)
    , m_record(record)
    , m_standard(standard)
    , m_repository(repository)
{
    setWindowTitle(QStringLiteral("训练复盘校准"));
    resize(1180, 780);
    buildUi();
    loadMainVideo();
    loadRepetitions();
    loadReferenceVideo();
}

void TrainingReviewDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(14, 14, 14, 14);
    root->setSpacing(10);

    auto *title = new QLabel(QStringLiteral("%1 · %2 · %3")
                                 .arg(m_record.time, m_record.athleteName, m_record.actionName),
                             this);
    title->setWordWrap(true);
    title->setProperty("role", "sectionTitle");
    root->addWidget(title);

    auto *videoLayout = new QHBoxLayout();
    videoLayout->setSpacing(10);
    m_mainVideo = new VideoOpenGLWidget(this);
    m_mainVideo->setMinimumSize(560, 320);
    m_mainVideo->setOverlayControlsVisible(true);
    m_mainVideo->setConfigButtonVisible(false);
    m_mainVideo->setPlaceholderText(QStringLiteral("训练回放"));
    videoLayout->addWidget(m_mainVideo, 3);

    auto *referencePanel = new QVBoxLayout();
    auto *referenceTitle = new QLabel(QStringLiteral("标准参考"), this);
    referenceTitle->setProperty("role", "sectionTitle");
    m_referenceVideo = new VideoOpenGLWidget(this);
    m_referenceVideo->setMinimumSize(320, 220);
    m_referenceVideo->setOverlayControlsVisible(true);
    m_referenceVideo->setConfigButtonVisible(false);
    m_referenceVideo->setPlaceholderText(QStringLiteral("未配置标准参考视频"));
    referencePanel->addWidget(referenceTitle);
    referencePanel->addWidget(m_referenceVideo, 1);

    m_referencePathEdit = new QLineEdit(this);
    m_referencePathEdit->setText(m_standard.referenceVideoSource);
    m_referencePathEdit->setPlaceholderText(QStringLiteral("本地标准动作视频路径"));
    auto *referenceButtons = new QHBoxLayout();
    auto *chooseReferenceButton = new QPushButton(QStringLiteral("选择参考"), this);
    auto *saveReferenceButton = new QPushButton(QStringLiteral("保存参考"), this);
    chooseReferenceButton->setProperty("role", "secondaryButton");
    saveReferenceButton->setProperty("role", "secondaryButton");
    referenceButtons->addWidget(chooseReferenceButton);
    referenceButtons->addWidget(saveReferenceButton);
    m_referenceNotesEdit = new QPlainTextEdit(this);
    m_referenceNotesEdit->setPlainText(m_standard.referenceNotes);
    m_referenceNotesEdit->setPlaceholderText(QStringLiteral("标准参考说明、教练观察或对比重点"));
    m_referenceNotesEdit->setMaximumHeight(74);
    referencePanel->addWidget(m_referencePathEdit);
    referencePanel->addLayout(referenceButtons);
    referencePanel->addWidget(m_referenceNotesEdit);
    videoLayout->addLayout(referencePanel, 2);
    root->addLayout(videoLayout, 2);

    auto *controlLayout = new QHBoxLayout();
    controlLayout->setSpacing(6);
    auto *playButton = new QPushButton(QStringLiteral("播放"), this);
    auto *pauseButton = new QPushButton(QStringLiteral("暂停"), this);
    auto *prevButton = new QPushButton(QStringLiteral("上一段"), this);
    auto *nextButton = new QPushButton(QStringLiteral("下一段"), this);
    auto *keyFrameButton = new QPushButton(QStringLiteral("关键帧"), this);
    auto *stepButton = new QPushButton(QStringLiteral("逐帧"), this);
    auto *slowQuarterButton = new QPushButton(QStringLiteral("0.25x"), this);
    auto *slowHalfButton = new QPushButton(QStringLiteral("0.5x"), this);
    auto *normalButton = new QPushButton(QStringLiteral("1x"), this);
    for (auto *button : {playButton, pauseButton, prevButton, nextButton, keyFrameButton, stepButton,
                         slowQuarterButton, slowHalfButton, normalButton}) {
        button->setProperty("role", "secondaryButton");
        controlLayout->addWidget(button);
    }
    m_positionLabel = new QLabel(QStringLiteral("00:00.0 / --:--"), this);
    m_positionLabel->setProperty("role", "muted");
    controlLayout->addStretch(1);
    controlLayout->addWidget(m_positionLabel);
    root->addLayout(controlLayout);

    auto *bodyLayout = new QHBoxLayout();
    bodyLayout->setSpacing(10);
    m_table = new QTableWidget(this);
    m_table->setColumnCount(7);
    m_table->setHorizontalHeaderLabels({QStringLiteral("#"),
                                        QStringLiteral("来源"),
                                        QStringLiteral("时间"),
                                        QStringLiteral("AI分"),
                                        QStringLiteral("有效分"),
                                        QStringLiteral("错误"),
                                        QStringLiteral("备注")});
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->verticalHeader()->hide();
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    bodyLayout->addWidget(m_table, 3);

    auto *formBox = new QGroupBox(QStringLiteral("人工复核"), this);
    auto *form = new QFormLayout(formBox);
    m_startSpinBox = new QSpinBox(formBox);
    m_endSpinBox = new QSpinBox(formBox);
    m_startSpinBox->setRange(0, 24 * 60 * 60 * 1000);
    m_endSpinBox->setRange(0, 24 * 60 * 60 * 1000);
    m_startSpinBox->setSingleStep(100);
    m_endSpinBox->setSingleStep(100);
    m_validCheckBox = new QCheckBox(QStringLiteral("有效动作"), formBox);
    m_scoreSpinBox = makeScoreSpinBox(formBox);
    m_detectionSpinBox = makeScoreSpinBox(formBox);
    m_symmetrySpinBox = makeScoreSpinBox(formBox);
    m_balanceSpinBox = makeScoreSpinBox(formBox);
    m_stabilitySpinBox = makeScoreSpinBox(formBox);
    m_depthSpinBox = makeScoreSpinBox(formBox);
    m_errorLineEdit = new QLineEdit(formBox);
    m_errorLineEdit->setPlaceholderText(QStringLiteral("多个错误用 | 分隔"));
    m_feedbackEdit = new QPlainTextEdit(formBox);
    m_feedbackEdit->setMaximumHeight(70);
    m_noteEdit = new QPlainTextEdit(formBox);
    m_noteEdit->setMaximumHeight(70);
    form->addRow(QStringLiteral("开始 ms"), m_startSpinBox);
    form->addRow(QStringLiteral("结束 ms"), m_endSpinBox);
    form->addRow(QStringLiteral("有效性"), m_validCheckBox);
    form->addRow(QStringLiteral("总分"), m_scoreSpinBox);
    form->addRow(QStringLiteral("关键点"), m_detectionSpinBox);
    form->addRow(QStringLiteral("对称"), m_symmetrySpinBox);
    form->addRow(QStringLiteral("重心"), m_balanceSpinBox);
    form->addRow(QStringLiteral("稳定"), m_stabilitySpinBox);
    form->addRow(QStringLiteral("3D"), m_depthSpinBox);
    form->addRow(QStringLiteral("错误项"), m_errorLineEdit);
    form->addRow(QStringLiteral("反馈"), m_feedbackEdit);
    form->addRow(QStringLiteral("备注"), m_noteEdit);
    auto *formButtons = new QHBoxLayout();
    auto *saveReviewButton = new QPushButton(QStringLiteral("保存复核"), formBox);
    auto *manualButton = new QPushButton(QStringLiteral("新增手动动作"), formBox);
    saveReviewButton->setProperty("role", "secondaryButton");
    manualButton->setProperty("role", "secondaryButton");
    formButtons->addWidget(saveReviewButton);
    formButtons->addWidget(manualButton);
    form->addRow(formButtons);
    bodyLayout->addWidget(formBox, 2);
    root->addLayout(bodyLayout, 3);

    m_statusLabel = new QLabel(QStringLiteral("视频：%1").arg(displaySource(videoSource())), this);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setProperty("role", "muted");
    root->addWidget(m_statusLabel);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    buttons->button(QDialogButtonBox::Close)->setText(QStringLiteral("关闭"));
    root->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(playButton, &QPushButton::clicked, this, [this]() { m_mainVideo->setPlaying(true); });
    connect(pauseButton, &QPushButton::clicked, this, [this]() { m_mainVideo->pausePlayback(); });
    connect(prevButton, &QPushButton::clicked, this, [this]() { selectRepetition(std::max(0, m_currentRow - 1)); });
    connect(nextButton, &QPushButton::clicked, this, [this]() {
        selectRepetition(std::min(static_cast<int>(m_repetitions.size()) - 1, m_currentRow + 1));
    });
    connect(keyFrameButton, &QPushButton::clicked, this, [this]() {
        if (m_currentRow >= 0 && m_currentRow < m_repetitions.size()) {
            seekToRepetition(m_repetitions.at(m_currentRow), true);
        }
    });
    connect(stepButton, &QPushButton::clicked, this, [this]() { m_mainVideo->stepForward(); });
    connect(slowQuarterButton, &QPushButton::clicked, this, [this]() { m_mainVideo->setPlaybackRate(0.25); });
    connect(slowHalfButton, &QPushButton::clicked, this, [this]() { m_mainVideo->setPlaybackRate(0.5); });
    connect(normalButton, &QPushButton::clicked, this, [this]() { m_mainVideo->setPlaybackRate(1.0); });
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int) { selectRepetition(row); });
    connect(m_table, &QTableWidget::currentCellChanged, this, [this](int row, int, int, int) {
        if (row >= 0 && row != m_currentRow) {
            selectRepetition(row);
        }
    });
    connect(saveReviewButton, &QPushButton::clicked, this, [this]() { saveCurrentReview(); });
    connect(manualButton, &QPushButton::clicked, this, [this]() { createManualRepetition(); });
    connect(chooseReferenceButton, &QPushButton::clicked, this, [this]() { chooseReferenceVideo(); });
    connect(saveReferenceButton, &QPushButton::clicked, this, [this]() { saveReference(); });

    m_positionTimer = new QTimer(this);
    m_positionTimer->setInterval(250);
    connect(m_positionTimer, &QTimer::timeout, this, [this]() { refreshPositionLabel(); });
    m_positionTimer->start();
}

void TrainingReviewDialog::loadRepetitions()
{
    m_repetitions = m_repository ? m_repository->reviewedRepetitionsForSession(m_record.id) : QVector<ActionRepetition>();
    populateTable();
    if (!m_repetitions.isEmpty()) {
        selectRepetition(0);
    }
}

void TrainingReviewDialog::populateTable()
{
    m_table->setRowCount(m_repetitions.size());
    for (int row = 0; row < m_repetitions.size(); ++row) {
        const ActionRepetition &rep = m_repetitions.at(row);
        m_table->setItem(row, 0, readOnlyItem(QString::number(row + 1)));
        m_table->setItem(row, 1, readOnlyItem(rep.source == QStringLiteral("coach") ? QStringLiteral("教练") : QStringLiteral("AI")));
        m_table->setItem(row, 2, readOnlyItem(QStringLiteral("%1-%2")
                                                  .arg(formatMilliseconds(rep.effectiveStartedMs()),
                                                       formatMilliseconds(rep.effectiveEndedMs()))));
        m_table->setItem(row, 3, readOnlyItem(QString::number(rep.score)));
        m_table->setItem(row, 4, readOnlyItem(QStringLiteral("%1 / %2")
                                                  .arg(rep.effectiveValid() ? QStringLiteral("有效") : QStringLiteral("无效"))
                                                  .arg(rep.effectiveScore())));
        m_table->setItem(row, 5, readOnlyItem(issueSummary(rep.effectiveErrorCodes())));
        m_table->setItem(row, 6, readOnlyItem(rep.coachNote.trimmed()));
    }
    m_table->resizeColumnsToContents();
}

void TrainingReviewDialog::selectRepetition(int row)
{
    if (row < 0 || row >= m_repetitions.size()) {
        return;
    }
    m_currentRow = row;
    m_table->selectRow(row);
    const ActionRepetition &rep = m_repetitions.at(row);
    m_startSpinBox->setValue(rep.effectiveStartedMs());
    m_endSpinBox->setValue(rep.effectiveEndedMs());
    m_validCheckBox->setChecked(rep.effectiveValid());
    m_scoreSpinBox->setValue(std::clamp(rep.effectiveScore(), 0, 100));
    m_detectionSpinBox->setValue(std::clamp(rep.effectiveDetectionScore(), 0, 100));
    m_symmetrySpinBox->setValue(std::clamp(rep.effectiveSymmetryScore(), 0, 100));
    m_balanceSpinBox->setValue(std::clamp(rep.effectiveBalanceScore(), 0, 100));
    m_stabilitySpinBox->setValue(std::clamp(rep.effectiveStabilityScore(), 0, 100));
    m_depthSpinBox->setValue(std::clamp(rep.effectiveDepthScore(), 0, 100));
    m_errorLineEdit->setText(rep.effectiveErrorCodes());
    m_feedbackEdit->setPlainText(rep.effectiveFeedback());
    m_noteEdit->setPlainText(rep.coachNote);
    seekToRepetition(rep, false);
}

void TrainingReviewDialog::seekToRepetition(const ActionRepetition &repetition, bool keyFrame)
{
    const int target = keyFrame && repetition.keyFrameMs > 0
                           ? repetition.keyFrameMs
                           : repetition.videoClipStartMs;
    const int startMs = std::max(0, target);
    const QString indexedLocalFile = localVideoFileForRepetition(repetition);
    if (!indexedLocalFile.isEmpty()) {
        if (m_loadedMainVideoSource != indexedLocalFile || !m_loadedMainVideoIsLocal) {
            m_mainVideo->playFile(indexedLocalFile, startMs);
            m_loadedMainVideoSource = indexedLocalFile;
            m_loadedMainVideoIsLocal = true;
        } else if (m_mainVideo->isSeekable()) {
            m_mainVideo->seekTo(startMs);
        } else {
            m_mainVideo->playFile(indexedLocalFile, startMs);
        }
        m_statusLabel->setText(QStringLiteral("已按视频 #%1 精确定位到片段起点：%2。")
                                   .arg(repetition.videoIndex)
                                   .arg(formatMilliseconds(startMs)));
        return;
    }

    const TrainingVideoFile *indexedVideo = videoFileForRepetition(repetition);
    QString missingHint;
    if (indexedVideo && !indexedVideo->filePath.trimmed().isEmpty()) {
        missingHint = QStringLiteral("索引视频文件已清理或移动，请恢复文件后可精确定位；");
    }

    const NvrPlaybackResult nvr = buildNvrPlaybackUrl(loadSharedCameraSettings(),
                                                      loadCameraSlotSettings(12),
                                                      m_record,
                                                      startMs,
                                                      repetition.videoClipEndMs);
    if (!nvr.url.trimmed().isEmpty()) {
        m_mainVideo->playMainUrlWithFallback(nvr.url.trimmed(), m_record.videoFallbackSource);
        m_loadedMainVideoSource = nvr.url.trimmed();
        m_loadedMainVideoIsLocal = false;
        m_statusLabel->setText(QStringLiteral("%1已打开 NVR 片段窗口：%2-%3；RTSP 回放无法保证精确 seek。")
                                   .arg(missingHint,
                                        formatMilliseconds(nvr.startOffsetMs),
                                        formatMilliseconds(nvr.endOffsetMs)));
    } else if (hasLocalVideo()) {
        loadMainVideo(startMs);
        m_statusLabel->setText(QStringLiteral("%1已按本地视频定位到片段起点：%2。")
                                   .arg(missingHint, formatMilliseconds(startMs)));
    } else {
        const QString source = videoSource();
        if (!source.trimmed().isEmpty()) {
            m_mainVideo->playMainUrlWithFallback(source, m_record.videoFallbackSource);
            m_loadedMainVideoSource = source;
            m_loadedMainVideoIsLocal = false;
        }
        m_statusLabel->setText(QStringLiteral("%1RTSP/网络记录无法精确定位，仅显示片段起点时间：%2。")
                                   .arg(missingHint, formatMilliseconds(startMs)));
    }
}

void TrainingReviewDialog::saveCurrentReview()
{
    if (!m_repository || m_currentRow < 0 || m_currentRow >= m_repetitions.size()) {
        return;
    }
    ActionRepetition rep = formRepetition();
    rep.id = m_repetitions.at(m_currentRow).id;
    rep.sessionId = m_record.id;
    rep.reviewerCoachId = m_record.coachId;
    QString error;
    if (!m_repository->saveRepetitionReview(rep, &error)) {
        QMessageBox::warning(this, QStringLiteral("保存失败"), error);
        return;
    }
    loadRepetitions();
    m_statusLabel->setText(QStringLiteral("已保存人工复核。"));
}

void TrainingReviewDialog::createManualRepetition()
{
    if (!m_repository) {
        return;
    }
    ActionRepetition rep = formRepetition();
    rep.reviewerCoachId = m_record.coachId;
    QString error;
    if (!m_repository->createManualRepetition(m_record.id,
                                              m_record.actionStandardId,
                                              m_record.standardVersion,
                                              &rep,
                                              &error)) {
        QMessageBox::warning(this, QStringLiteral("新增失败"), error);
        return;
    }
    loadRepetitions();
    m_statusLabel->setText(QStringLiteral("已新增手动动作。"));
}

void TrainingReviewDialog::chooseReferenceVideo()
{
    const QString path = QFileDialog::getOpenFileName(this,
                                                      QStringLiteral("选择标准参考视频"),
                                                      QDir::homePath(),
                                                      QStringLiteral("视频文件 (*.mp4 *.mov *.avi *.mkv *.m4v *.wmv *.flv *.webm);;所有文件 (*.*)"));
    if (path.trimmed().isEmpty()) {
        return;
    }
    m_referencePathEdit->setText(QFileInfo(path).absoluteFilePath());
    loadReferenceVideo();
}

void TrainingReviewDialog::saveReference()
{
    if (!m_repository) {
        return;
    }
    m_standard.referenceVideoSource = m_referencePathEdit->text().trimmed();
    m_standard.referenceNotes = m_referenceNotesEdit->toPlainText().trimmed();
    QString error;
    if (!m_repository->saveActionStandard(&m_standard, &error)) {
        QMessageBox::warning(this, QStringLiteral("保存失败"), error);
        return;
    }
    m_statusLabel->setText(QStringLiteral("已保存标准参考，动作标准版本更新为 v%1。").arg(m_standard.version));
    loadReferenceVideo();
}

void TrainingReviewDialog::refreshPositionLabel()
{
    const qint64 position = m_mainVideo ? m_mainVideo->positionMs() : -1;
    const qint64 duration = m_mainVideo ? m_mainVideo->durationMs() : -1;
    const QString durationText = duration >= 0 ? formatMilliseconds(static_cast<int>(duration)) : QStringLiteral("--:--");
    m_positionLabel->setText(QStringLiteral("%1 / %2")
                                 .arg(formatMilliseconds(static_cast<int>(std::max<qint64>(0, position))))
                                 .arg(durationText));
}

void TrainingReviewDialog::loadMainVideo(int offsetMs)
{
    const QString source = videoSource();
    if (source.trimmed().isEmpty()) {
        return;
    }
    if (videoSourceIsUrl()) {
        m_mainVideo->playMainUrlWithFallback(source, m_record.videoFallbackSource);
        m_loadedMainVideoSource = source;
        m_loadedMainVideoIsLocal = false;
    } else {
        const QFileInfo fileInfo(source);
        if (!fileInfo.exists() || !fileInfo.isFile()) {
            m_mainVideo->setPlaceholderText(QStringLiteral("训练视频文件不存在"));
            return;
        }
        m_mainVideo->playFile(fileInfo.absoluteFilePath(), offsetMs);
        m_loadedMainVideoSource = fileInfo.absoluteFilePath();
        m_loadedMainVideoIsLocal = true;
    }
}

void TrainingReviewDialog::loadReferenceVideo()
{
    const QString source = m_referencePathEdit ? m_referencePathEdit->text().trimmed() : m_standard.referenceVideoSource;
    if (source.isEmpty()) {
        m_referenceVideo->stopPlayback();
        m_referenceVideo->setPlaceholderText(QStringLiteral("未配置标准参考视频"));
        return;
    }
    if (hasMediaUrlScheme(source)) {
        m_referenceVideo->playMainUrlWithFallback(source, QString());
    } else {
        const QFileInfo fileInfo(source);
        if (!fileInfo.exists() || !fileInfo.isFile()) {
            m_referenceVideo->stopPlayback();
            m_referenceVideo->setPlaceholderText(QStringLiteral("参考视频文件不存在"));
            return;
        }
        m_referenceVideo->playFile(fileInfo.absoluteFilePath());
    }
}

ActionRepetition TrainingReviewDialog::formRepetition() const
{
    ActionRepetition rep;
    rep.sessionId = m_record.id;
    rep.actionStandardId = m_record.actionStandardId;
    rep.standardVersion = m_record.standardVersion;
    rep.manualStartedMs = std::min(m_startSpinBox->value(), m_endSpinBox->value());
    rep.manualEndedMs = std::max(m_startSpinBox->value(), m_endSpinBox->value());
    rep.manualValid = m_validCheckBox->isChecked() ? 1 : 0;
    rep.manualScore = m_scoreSpinBox->value();
    rep.manualDetectionScore = m_detectionSpinBox->value();
    rep.manualSymmetryScore = m_symmetrySpinBox->value();
    rep.manualBalanceScore = m_balanceSpinBox->value();
    rep.manualStabilityScore = m_stabilitySpinBox->value();
    rep.manualDepthScore = m_depthSpinBox->value();
    rep.manualErrorCodes = m_errorLineEdit->text().trimmed();
    rep.manualFeedback = m_feedbackEdit->toPlainText().trimmed();
    rep.coachNote = m_noteEdit->toPlainText().trimmed();
    rep.keyFrameMs = rep.manualStartedMs;
    rep.videoClipStartMs = std::max(0, rep.manualStartedMs - 1500);
    rep.videoClipEndMs = rep.manualEndedMs + 1500;
    if (m_currentRow >= 0 && m_currentRow < m_repetitions.size()) {
        const ActionRepetition &current = m_repetitions.at(m_currentRow);
        rep.videoFileId = current.videoFileId;
        rep.videoIndex = current.videoIndex > 0 ? current.videoIndex : 1;
    } else if (!m_record.videoFiles.isEmpty()) {
        rep.videoFileId = m_record.videoFiles.first().id;
        rep.videoIndex = m_record.videoFiles.first().videoIndex > 0 ? m_record.videoFiles.first().videoIndex : 1;
    }
    return rep;
}

const TrainingVideoFile *TrainingReviewDialog::videoFileForRepetition(const ActionRepetition &repetition) const
{
    for (const TrainingVideoFile &file : m_record.videoFiles) {
        if (!repetition.videoFileId.trimmed().isEmpty() && file.id == repetition.videoFileId) {
            return &file;
        }
    }
    for (const TrainingVideoFile &file : m_record.videoFiles) {
        if (repetition.videoIndex > 0 && file.videoIndex == repetition.videoIndex) {
            return &file;
        }
    }
    return nullptr;
}

QString TrainingReviewDialog::localVideoFileForRepetition(const ActionRepetition &repetition) const
{
    const TrainingVideoFile *file = videoFileForRepetition(repetition);
    if (!file || file->filePath.trimmed().isEmpty()) {
        return {};
    }
    const QFileInfo fileInfo(file->filePath.trimmed());
    return fileInfo.exists() && fileInfo.isFile() ? fileInfo.absoluteFilePath() : QString();
}

QString TrainingReviewDialog::videoSource() const
{
    for (const TrainingVideoFile &file : m_record.videoFiles) {
        if (file.filePath.trimmed().isEmpty()) {
            continue;
        }
        const QFileInfo fileInfo(file.filePath.trimmed());
        if (fileInfo.exists() && fileInfo.isFile()) {
            return fileInfo.absoluteFilePath();
        }
    }
    return !m_record.videoSource.trimmed().isEmpty()
               ? m_record.videoSource.trimmed()
               : m_record.videoFallbackSource.trimmed();
}

bool TrainingReviewDialog::videoSourceIsUrl() const
{
    return hasMediaUrlScheme(videoSource());
}

bool TrainingReviewDialog::hasLocalVideo() const
{
    const QString source = videoSource();
    return !source.trimmed().isEmpty() && !hasMediaUrlScheme(source);
}

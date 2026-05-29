#include "mainwindow.h"
#include "actionstandardscorer.h"
#include "handanalysismanager.h"
#include "iconutils.h"
#include "posestandardnessscorer.h"
#include "skeletonviewwidget.h"
#include "trainingrepository.h"
#include "trajectorywidget.h"
#include "ui_mainwindow.h"
#include "videoopenglwidget.h"

#include <QAction>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QDebug>
#include <QRandomGenerator>
#include <QSettings>
#include <QSignalBlocker>
#include <QSize>
#include <QSizePolicy>
#include <QShortcut>
#include <QSpinBox>
#include <QStringConverter>
#include <QStringList>
#include <QStyle>
#include <QTextStream>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

namespace {

constexpr int kCapturePage = 0;
constexpr int kHistoryPage = 1;
constexpr int kSuggestionPage = 2;
constexpr int kDefaultFps = 30;
constexpr int kDefaultPreviewStreamFps = 30;
constexpr int kDefaultMainStreamFps = 120;
constexpr int kDefaultRtspPort = 554;
constexpr int kMaxVisibleHistoryItems = 10;
constexpr const char *kPreviousWindowStateProperty = "previousWindowStateBeforeFullScreen";
constexpr const char *kMutedInactiveColor = "#8c8c8c";
constexpr const char *kHoverActionColor = "#3b8dff";

enum TrajectoryMode {
    TrajectoryNormal = 0,   // 只占用三维轨迹原本所在区域。
    TrajectoryExpanded = 1, // 占用自身区域和上方 12 路视频区域。
    TrajectoryMinimized = 2 // 只保留“三维轨迹”标题栏和右侧符号。
};

QString cameraSettingsGroup(int cameraIndex)
{
    return QStringLiteral("cameras/camera%1").arg(cameraIndex + 1, 2, 10, QLatin1Char('0'));
}

QString defaultCameraChannelName(int cameraIndex)
{
    return QStringLiteral("CAM %1").arg(cameraIndex + 1, 2, 10, QLatin1Char('0'));
}

QString configuredCameraChannelName(int cameraIndex, const QString &ip)
{
    const QString base = defaultCameraChannelName(cameraIndex);
    const QString trimmedIp = ip.trimmed();
    return trimmedIp.isEmpty() ? base : QStringLiteral("%1 - %2").arg(base, trimmedIp);
}

QString normalizedStoredPath(const QString &path)
{
    QString normalized = path.trimmed();
    while (normalized.startsWith(QLatin1Char('/'))) {
        normalized.remove(0, 1);
    }
    return normalized;
}

QString normalizedRtspPath(const QString &path)
{
    const QString normalized = normalizedStoredPath(path);
    return normalized.isEmpty() ? QString() : QStringLiteral("/") + normalized;
}

QString composeLegacyStreamUrl(const QString &ipOrUrl, const QString &port, const QString &path)
{
    const QString trimmedPath = path.trimmed();
    const QString trimmedIp = ipOrUrl.trimmed();
    const QString trimmedPort = port.trimmed();

    if (trimmedPath.contains(QStringLiteral("://"))) {
        return trimmedPath;
    }

    if (trimmedIp.contains(QStringLiteral("://"))) {
        QString url = trimmedIp;
        if (!trimmedPath.isEmpty()) {
            if (!url.endsWith(QLatin1Char('/')) && !trimmedPath.startsWith(QLatin1Char('/'))) {
                url += QLatin1Char('/');
            } else if (url.endsWith(QLatin1Char('/')) && trimmedPath.startsWith(QLatin1Char('/'))) {
                url.chop(1);
            }
            url += trimmedPath;
        }
        return url;
    }

    if (trimmedIp.isEmpty()) {
        return trimmedPath;
    }

    QString url = QStringLiteral("rtsp://") + trimmedIp;
    if (!trimmedPort.isEmpty()) {
        url += QStringLiteral(":") + trimmedPort;
    }
    if (!trimmedPath.isEmpty()) {
        if (!trimmedPath.startsWith(QLatin1Char('/'))) {
            url += QLatin1Char('/');
        }
        url += trimmedPath;
    }
    return url;
}

struct ParsedRtspUrl
{
    bool valid = false;
    QString username;
    QString password;
    QString host;
    QString port;
    QString path;
};

ParsedRtspUrl parseRtspUrl(const QString &source)
{
    ParsedRtspUrl parsed;
    const QString trimmedSource = source.trimmed();
    if (trimmedSource.isEmpty()) {
        return parsed;
    }

    const QUrl url = QUrl::fromEncoded(trimmedSource.toUtf8(), QUrl::TolerantMode);
    if (!url.isValid() || url.scheme().compare(QStringLiteral("rtsp"), Qt::CaseInsensitive) != 0) {
        return parsed;
    }
    if (url.host().trimmed().isEmpty()) {
        return parsed;
    }

    parsed.valid = true;
    parsed.username = url.userName(QUrl::FullyDecoded);
    parsed.password = url.password(QUrl::FullyDecoded);
    parsed.host = url.host().trimmed();
    if (url.port() > 0) {
        parsed.port = QString::number(url.port());
    }
    parsed.path = normalizedStoredPath(url.path());
    return parsed;
}

QString extractHostFromLegacyValue(const QString &source)
{
    const QString trimmed = source.trimmed();
    if (trimmed.isEmpty()) {
        return QString();
    }

    const ParsedRtspUrl parsed = parseRtspUrl(trimmed);
    return parsed.valid ? parsed.host : trimmed;
}

QString composeCameraUrl(const SharedCameraSettings &sharedSettings, const QString &ip, bool mainStream)
{
    const QString trimmedIp = ip.trimmed();
    if (trimmedIp.isEmpty()) {
        return QString();
    }

    QUrl url;
    url.setScheme(QStringLiteral("rtsp"));
    if (!sharedSettings.username.trimmed().isEmpty()) {
        url.setUserName(sharedSettings.username.trimmed());
    }
    if (!sharedSettings.password.isEmpty()) {
        url.setPassword(sharedSettings.password);
    }
    url.setHost(trimmedIp);

    const QString normalizedPort = sharedSettings.port.trimmed();
    if (normalizedPort.isEmpty()) {
        url.setPort(kDefaultRtspPort);
    } else {
        bool ok = false;
        const int port = normalizedPort.toInt(&ok);
        if (ok && port > 0) {
            url.setPort(port);
        }
    }

    const QString selectedPath = mainStream && !sharedSettings.mainPath.trimmed().isEmpty()
                                     ? sharedSettings.mainPath
                                     : sharedSettings.previewPath;
    url.setPath(normalizedRtspPath(selectedPath));
    return url.toString(QUrl::FullyEncoded);
}

bool hasStructuredCameraDefaults(QSettings &settings)
{
    return settings.contains(QStringLiteral("cameraDefaults/username"))
           || settings.contains(QStringLiteral("cameraDefaults/password"))
           || settings.contains(QStringLiteral("cameraDefaults/port"))
           || settings.contains(QStringLiteral("cameraDefaults/previewPath"))
           || settings.contains(QStringLiteral("cameraDefaults/mainPath"));
}

QString safeUrlForLog(const QString &source)
{
    QUrl url = QUrl::fromEncoded(source.toUtf8(), QUrl::TolerantMode);
    if (!url.password().isEmpty()) {
        url.setPassword(QStringLiteral("***"));
        return url.toString(QUrl::FullyEncoded);
    }
    return source;
}

QString displayMediaSource(const QString &source)
{
    const QString trimmed = source.trimmed();
    if (trimmed.isEmpty()) {
        return QStringLiteral("未记录");
    }

    QUrl url = QUrl::fromEncoded(trimmed.toUtf8(), QUrl::TolerantMode);
    if (!url.isValid() || url.scheme().isEmpty()) {
        return trimmed;
    }
    if (!url.password().isEmpty()) {
        url.setPassword(QStringLiteral("***"));
    }
    return url.toString(QUrl::RemoveQuery | QUrl::RemoveFragment);
}

QString issueSummary(const QString &errorCodes)
{
    const QStringList issues = errorCodes.split(QStringLiteral("|"), Qt::SkipEmptyParts);
    return issues.isEmpty() ? QStringLiteral("未触发关键错误") : issues.join(QStringLiteral("、"));
}

QString qualityLabel(int score, bool valid)
{
    if (valid) {
        return QStringLiteral("达标");
    }
    if (score >= 70) {
        return QStringLiteral("接近");
    }
    return QStringLiteral("待纠正");
}

QString repetitionReportLine(const ActionRepetition &repetition, int index, const std::function<QString(int)> &formatMs)
{
    return QStringLiteral("%1. %2-%3  %4分  %5  错误：%6  反馈：%7")
        .arg(index + 1)
        .arg(formatMs(repetition.startedMs))
        .arg(formatMs(repetition.endedMs))
        .arg(repetition.score)
        .arg(qualityLabel(repetition.score, repetition.valid))
        .arg(issueSummary(repetition.errorCodes))
        .arg(repetition.feedback.trimmed().isEmpty() ? QStringLiteral("无") : repetition.feedback.trimmed());
}

QString topbarModuleHtml(const QString &icon, const QString &title, const QString &value)
{
    return QStringLiteral(
               "<table cellspacing='0' cellpadding='0'>"
               "<tr>"
               "<td rowspan='2' style='padding-right:8px; color:#9fb6d8; font-size:19px; font-weight:800;'>%1</td>"
               "<td style='color:#7fb6ff; font-size:12px; font-weight:700;'>%2</td>"
               "</tr>"
               "<tr>"
               "<td style='color:#8c8c8c; font-size:13px; font-weight:500;'>%3</td>"
               "</tr>"
               "</table>")
        .arg(icon, title, value);
}

// 清空布局中的子项；保留该工具函数供动态重建列表类界面时复用。
void clearLayout(QLayout *layout)
{
    if (!layout) {
        return;
    }

    while (QLayoutItem *item = layout->takeAt(0)) {
        if (QWidget *widget = item->widget()) {
            delete widget;
        } else if (QLayout *childLayout = item->layout()) {
            clearLayout(childLayout);
            delete childLayout;
        }

        delete item;
    }
}

// 设置控件的样式角色属性，配合 QSS 中的属性选择器刷新外观。
void setRole(QWidget *widget, const char *role)
{
    if (widget) {
        widget->setProperty("role", role);
    }
}

// 递归显示/隐藏布局内容；隐藏侧栏时同时压缩 spacer，确保布局真正收窄。
void setLayoutItemsVisible(QLayout *layout, bool visible)
{
    if (!layout) {
        return;
    }

    for (int i = 0; i < layout->count(); ++i) {
        QLayoutItem *item = layout->itemAt(i);
        if (!item) {
            continue;
        }

        if (auto *childWidget = item->widget()) {
            childWidget->setVisible(visible);
        } else if (auto *childLayout = item->layout()) {
            setLayoutItemsVisible(childLayout, visible);
        } else if (auto *spacer = item->spacerItem()) {
            spacer->changeSize(visible ? 20 : 0,
                               visible ? 40 : 0,
                               visible ? QSizePolicy::Minimum : QSizePolicy::Fixed,
                               visible ? QSizePolicy::Expanding : QSizePolicy::Fixed);
        }
    }
}

} // namespace

// 初始化主窗口：装配 UI、视频控件、顶部状态栏、快捷键、右侧动作按钮和默认布局状态。
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    m_cameraButtons = {ui->cameraButton01, ui->cameraButton02, ui->cameraButton03, ui->cameraButton04,
                       ui->cameraButton05, ui->cameraButton06, ui->cameraButton07, ui->cameraButton08,
                       ui->cameraButton09, ui->cameraButton10, ui->cameraButton11, ui->cameraButton12};

    auto setTopbarModule = [](QLabel *label, const QString &icon, const QString &title, const QString &value) {
        label->setTextFormat(Qt::RichText);
        label->setText(QStringLiteral(
                           "<table cellspacing='0' cellpadding='0'>"
                           "<tr>"
                           "<td rowspan='2' style='padding-right:8px; color:#9fb6d8; font-size:19px; font-weight:800;'>%1</td>"
                           "<td style='color:#7fb6ff; font-size:12px; font-weight:700;'>%2</td>"
                           "</tr>"
                           "<tr>"
                           "<td style='color:#8c8c8c; font-size:13px; font-weight:500;'>%3</td>"
                           "</tr>"
                           "</table>")
                           .arg(icon, title, value));
    };

    setTopbarModule(ui->sessionRoundLabel,
                    QStringLiteral("⛸"),
                    QStringLiteral("训练轮次 Session"),
                    QStringLiteral("自由滑训练-第3次"));
    setTopbarModule(ui->sessionTimeLabel,
                    QStringLiteral("🕒"),
                    QStringLiteral("日期/时间 Date/Time"),
                    QStringLiteral("2026-05-20 16:28:34"));
    setTopbarModule(ui->systemStatusLabel,
                    QStringLiteral("⚙"),
                    QStringLiteral("系统状态 System Status"),
                    QStringLiteral("运行中（正常）"));
    setTopbarModule(ui->modelStatusLabel,
                    QStringLiteral("AI"),
                    QStringLiteral("模型状态 Status"),
                    QStringLiteral("人体姿态 AI 初始化中"));
    setTopbarModule(ui->storageStatusLabel,
                    QStringLiteral("DB"),
                    QStringLiteral("存储 Storage"),
                    QStringLiteral("1.82T/4.00TB"));

    m_poseStandardnessScorer = std::make_unique<PoseStandardnessScorer>();
    m_actionStandardScorer = std::make_unique<ActionStandardScorer>();
    m_actionRepetitionTracker = std::make_unique<ActionRepetitionTracker>();
    m_trainingRepository = std::make_unique<TrainingRepository>();
    m_handAnalysisManager = std::make_unique<HandAnalysisManager>(this);
    m_handAnalysisManager->setResultCallback([this](const PoseFrameResult &poseFrame) {
        ui->mainImageLabel->setPoseFrame(poseFrame);
        if (m_skeletonView) {
            m_skeletonView->setPoseFrame(poseFrame);
        }
        if (m_trajectoryWidget) {
            m_trajectoryWidget->setPoseFrame(poseFrame);
        }
        if (m_poseStandardnessScorer) {
            const PoseStandardnessResult baseStandardness = m_poseStandardnessScorer->scoreFrame(poseFrame);
            const ActionStandard standard = selectedActionStandard();
            ActionAssessment assessment;
            if (m_actionStandardScorer && !standard.id.isEmpty()) {
                assessment = m_actionStandardScorer->score(baseStandardness, standard);
                m_realtimeScore = assessment.score;
                m_detectionScore = assessment.detectionScore;
                m_symmetryScore = assessment.symmetryScore;
                m_balanceScore = assessment.balanceScore;
                m_stabilityScore = assessment.stabilityScore;
                m_depthScore = assessment.depthScore;
                m_feedbackText = assessment.feedback;
            } else {
                assessment.score = baseStandardness.score;
                assessment.detectionScore = baseStandardness.detectionScore;
                assessment.symmetryScore = baseStandardness.symmetryScore;
                assessment.balanceScore = baseStandardness.balanceScore;
                assessment.stabilityScore = baseStandardness.stabilityScore;
                assessment.depthScore = baseStandardness.depthScore;
                assessment.feedback = baseStandardness.feedback;
                assessment.valid = baseStandardness.valid;
                m_realtimeScore = baseStandardness.score;
                m_detectionScore = baseStandardness.detectionScore;
                m_symmetryScore = baseStandardness.symmetryScore;
                m_balanceScore = baseStandardness.balanceScore;
                m_stabilityScore = baseStandardness.stabilityScore;
                m_depthScore = baseStandardness.depthScore;
                m_feedbackText = baseStandardness.feedback;
            }
            if (m_isRecording && !m_isPaused && m_actionRepetitionTracker) {
                ActionRepetition repetition;
                if (m_actionRepetitionTracker->update(poseFrame,
                                                      assessment,
                                                      QDateTime::currentMSecsSinceEpoch(),
                                                      m_recordingStartedAtMsec,
                                                      &repetition)) {
                    recordCompletedRepetition(repetition);
                }
            }
            refreshStats();
        }
    });
    m_handAnalysisManager->setStatusCallback([this](const QString &statusText) {
        refreshModelStatus(statusText);
    });

    // 在“隐藏侧栏”按钮旁边动态增加全屏按钮，避免修改 .ui 后生成头文件不同步。
    m_fullScreenButton = new QPushButton(ui->toggleSidebarButton->parentWidget());
    m_fullScreenButton->setObjectName(QStringLiteral("fullScreenButton"));
    m_fullScreenButton->setProperty("role", "plain");
    m_fullScreenButton->setFocusPolicy(Qt::NoFocus);
    ui->toggleSidebarButton->setFocusPolicy(Qt::NoFocus);
    ui->toggleSidebarButton->installEventFilter(this);
    m_fullScreenButton->installEventFilter(this);
    refreshSidebarButton();
    refreshFullScreenButton();
    const int sidebarButtonIndex = ui->topbarLayout->indexOf(ui->toggleSidebarButton);
    if (sidebarButtonIndex >= 0) {
        ui->topbarLayout->insertWidget(sidebarButtonIndex + 1, m_fullScreenButton);
    } else {
        ui->topbarLayout->addWidget(m_fullScreenButton);
    }

    ui->mainImageLabel->setPlaceholderText(QStringLiteral("主视频\n未播放"));
    ui->mainImageLabel->setOverlayControlsVisible(false);
    ui->mainImageLabel->setStreamChangedHandler([this](VideoOpenGLWidget *) {
        if (m_handAnalysisManager) {
            m_handAnalysisManager->setActiveStream(m_selectedCamera, ui->mainImageLabel->activeStream());
        }
    });
    for (int i = 0; i < m_cameraButtons.size(); ++i) {
        auto *cameraWidget = m_cameraButtons.at(i);
        const QString cameraName = defaultCameraChannelName(i);
        cameraWidget->setChannelName(cameraName);
        cameraWidget->setPlaceholderText(cameraName);
        cameraWidget->setOverlayControlsVisible(true);
        cameraWidget->setConfigButtonVisible(false);
        cameraWidget->setDoubleClickHandler([this, i](VideoOpenGLWidget *) {
            qDebug() << "[MainWindow] camera double clicked, play in main view"
                     << m_cameraButtons.at(i)->channelName()
                     << safeUrlForLog(m_cameraButtons.at(i)->mainUrl());
            showCameraInMainView(i);
        });
    }
    loadCameraSettings();
    initializeTrainingRepository();

    applyStyleSheet();
    installStaticImages();
    installSkeletonView();
    installTrajectoryWidget();
    installMetricBars();
    installTrainingContextPanel();

    auto *fullScreenShortcut = new QShortcut(QKeySequence(Qt::Key_F11), this);
    fullScreenShortcut->setContext(Qt::WindowShortcut);
    connect(fullScreenShortcut, &QShortcut::activated, this, [this]() { toggleFullScreen(); });

    auto *escapeShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    escapeShortcut->setContext(Qt::WindowShortcut);
    connect(escapeShortcut, &QShortcut::activated, this, [this]() { exitFullScreenMode(); });

    auto makeButtonAction = [this](QPushButton *button, const QString &label, const QString &iconPath) {
        auto *action = new QAction(makeNormalizedTintedSvgIcon(iconPath,
                                                               QColor(QString::fromLatin1(kMutedInactiveColor)),
                                                               38,
                                                               30),
                                   label,
                                   this);
        action->setToolTip(label);
        action->setStatusTip(label);

        button->setText(QString());
        button->setIcon(action->icon());
        button->setIconSize(QSize(38, 38));
        button->setToolTip(label);
        button->setStatusTip(label);
        button->setAccessibleName(label);
        button->setProperty("showTipTextOnHover", true);
        button->installEventFilter(this);

        connect(button, &QPushButton::clicked, action, &QAction::trigger);
        return action;
    };

    auto *startAction = makeButtonAction(ui->startCaptureButton, QStringLiteral("开始采集"), QStringLiteral(":/icons/start_cap.svg"));
    auto *pauseAction = makeButtonAction(ui->pauseCaptureButton, QStringLiteral("暂停"), QStringLiteral(":/icons/suspend.svg"));
    auto *stopAction = makeButtonAction(ui->stopCaptureButton, QStringLiteral("停止"), QStringLiteral(":/icons/stop.svg"));
    auto *saveAction = makeButtonAction(ui->saveRecordButton, QStringLiteral("保存记录"), QStringLiteral(":/icons/save.svg"));
    auto *settingsAction = makeButtonAction(ui->settingsButton, QStringLiteral("系统设置"), QStringLiteral(":/icons/settings.svg"));

    connect(startAction, &QAction::triggered, this, [this]() { startCapture(); });
    connect(pauseAction, &QAction::triggered, this, [this]() { pauseCapture(); });
    connect(stopAction, &QAction::triggered, this, [this]() { stopCapture(); });
    connect(saveAction, &QAction::triggered, this, [this]() { saveRecord(); });
    connect(settingsAction, &QAction::triggered, this, [this]() { openSystemSettings(); });

    QWidget *trajectoryView = m_trajectoryWidget
                                  ? static_cast<QWidget *>(m_trajectoryWidget)
                                  : static_cast<QWidget *>(ui->trajectoryViewFrame);
    m_trajectoryViewNormalMinSize = trajectoryView->minimumSize();
    m_trajectoryViewNormalMaxSize = trajectoryView->maximumSize();
    m_trajectoryCardNormalMinSize = ui->trajectoryCard->minimumSize();
    m_trajectoryCardNormalMaxSize = ui->trajectoryCard->maximumSize();
    m_middleLayoutNormalSpacing = ui->middleLayout->spacing();
    m_middleLayoutNormalStretch0 = ui->middleLayout->stretch(0);
    m_middleLayoutNormalStretch1 = ui->middleLayout->stretch(1);
    m_cameraGridNormalSpacing = ui->cameraGridLayout->spacing();
    // 卡片标题按内容宽度显示，避免标题背景在纵向布局里被拉满整行。
    ui->poseLayout->setAlignment(ui->poseTitleLabel, Qt::AlignLeft);
    ui->metricsLayout->setAlignment(Qt::AlignTop);
    ui->metricsLayout->setAlignment(ui->metricsTitleLabel, Qt::AlignLeft);
    ui->metricsCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    if (auto *captureLayout = qobject_cast<QVBoxLayout *>(ui->capturePage->layout())) {
        captureLayout->setStretch(0, 1);
        captureLayout->setStretch(1, 0);
    }
    m_timer.setInterval(1000);
    connect(&m_timer, &QTimer::timeout, this, [this]() { tick(); });

    setupUiState();
    setupConnections();
    setTrajectoryMode(TrajectoryNormal);
    refreshStats();
}

// 释放由 Qt Designer 生成的界面对象。
MainWindow::~MainWindow()
{
    if (m_handAnalysisManager) {
        m_handAnalysisManager->stop();
        m_handAnalysisManager.reset();
    }
    saveCameraSettings();
    delete ui;
}

// 监听图标按钮的悬停状态：右上角按钮悬停时点亮，右侧控制按钮悬停时显示提示文字。
bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == ui->toggleSidebarButton
        && (event->type() == QEvent::Enter || event->type() == QEvent::Leave)) {
        refreshSidebarButton();
    } else if (watched == m_fullScreenButton
               && (event->type() == QEvent::Enter || event->type() == QEvent::Leave)) {
        refreshFullScreenButton();
    } else if ((event->type() == QEvent::Enter || event->type() == QEvent::Leave)
               && watched->property("showTipTextOnHover").toBool()) {
        if (auto *button = qobject_cast<QPushButton *>(watched)) {
            button->setText(event->type() == QEvent::Enter ? button->accessibleName() : QString());
        }
    }

    return QMainWindow::eventFilter(watched, event);
}

// 监听窗口状态变化：程序启动全屏、F11 切换或 Esc 退出后，同步刷新右上角全屏按钮。
void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange) {
        refreshFullScreenButton();
    }
}

// 初始化运行时 UI 状态；当前界面主要在构造函数中完成初始化，保留该入口便于后续扩展。
void MainWindow::setupUiState()
{
    m_navButtons = {ui->navCaptureButton, ui->navHistoryButton, ui->navSuggestionButton};
    for (auto *button : m_navButtons) {
        setRole(button, "nav");
        button->setFocusPolicy(Qt::NoFocus);
    }

    m_summaryValues = {
        ui->summarySessionsValue,
        ui->summaryActionsValue,
        ui->summaryAvgValue,
        ui->summaryBestValue
    };

    if (ui->precisionComboBox && ui->precisionComboBox->count() == 0) {
        ui->precisionComboBox->addItem(precisionLabel(QStringLiteral("fast")), QStringLiteral("fast"));
        ui->precisionComboBox->addItem(precisionLabel(QStringLiteral("balanced")), QStringLiteral("balanced"));
        ui->precisionComboBox->addItem(precisionLabel(QStringLiteral("high")), QStringLiteral("high"));
    }

    if (ui->fpsComboBox && ui->fpsComboBox->count() == 0) {
        for (int fps : {15, 25, 30, 50, 60, 90, 120}) {
            ui->fpsComboBox->addItem(QStringLiteral("%1 FPS").arg(fps), fps);
        }
    }
    applyCapturePreferencesToUi();
    reloadTrainingContext();
    ui->settingsBox->hide();
    if (m_trainingRepository && !m_trainingRepository->isOpen() && !m_trainingRepository->lastError().isEmpty()) {
        ui->saveTipLabel->setText(QStringLiteral("训练数据库初始化失败：%1").arg(m_trainingRepository->lastError()));
        ui->saveTipLabel->show();
    }

    loadTrainingRecords();
    const bool hasDatabaseError = m_trainingRepository && !m_trainingRepository->isOpen() && !m_trainingRepository->lastError().isEmpty();
    if (!m_lastSavedAt.isEmpty()) {
        ui->saveTipLabel->setText(QStringLiteral("最近保存：%1").arg(m_lastSavedAt));
        ui->saveTipLabel->show();
    } else if (!hasDatabaseError) {
        ui->saveTipLabel->clear();
        ui->saveTipLabel->hide();
    }

    m_activePage = kCapturePage;
    ui->pages->setCurrentIndex(kCapturePage);
    refreshNavButtons();
    refreshCameraButtons();
    refreshHistory();
    refreshSuggestions();
}

// 绑定页面上的交互信号：侧栏开关、全屏切换、三维轨迹三态切换等。
void MainWindow::setupConnections()
{
    connect(ui->navCaptureButton, &QPushButton::clicked, this, [this]() {
        switchPage(kCapturePage);
    });
    connect(ui->navHistoryButton, &QPushButton::clicked, this, [this]() {
        switchPage(kHistoryPage);
    });
    connect(ui->navSuggestionButton, &QPushButton::clicked, this, [this]() {
        switchPage(kSuggestionPage);
    });
    connect(ui->toggleSidebarButton, &QPushButton::clicked, this, [this]() {
        toggleSidebar();
    });
    if (m_fullScreenButton) {
        connect(m_fullScreenButton, &QPushButton::clicked, this, [this]() {
            toggleFullScreen();
        });
    }
    connect(ui->expandTrajectoryButton, &QPushButton::clicked, this, [this]() {
        cycleTrajectoryMode();
    });
    connect(ui->collapseTrajectoryButton, &QPushButton::clicked, this, [this]() {
        cycleTrajectoryMode();
    });
}

// 将资源文件中的静态示例图片贴到界面对应占位控件上。
void MainWindow::installStaticImages()
{
    // 运动员 3D 姿态区只需要 VideoOpenGLWidget 统一重绘背景，不再默认贴 pose 图片或视频占位图。
    ui->poseImageLabelA->setPlaceholderText(QString());
    ui->poseImageLabelA->setPlaceholderIconVisible(false);
    ui->poseImageLabelA->setOverlayControlsVisible(false);
    ui->poseImageLabelB->setPlaceholderText(QString());
    ui->poseImageLabelB->setPlaceholderIconVisible(false);
    ui->poseImageLabelB->setOverlayControlsVisible(false);
}

// 将实时 AI 骨架结果显示到下方“运动员3D骨架”窗口；主视频叠加仍走 D3D surface。
void MainWindow::installSkeletonView()
{
    if (m_skeletonView) {
        return;
    }

    ui->poseTitleLabel->setText(QStringLiteral("运动员3D骨架"));
    ui->poseImageLayout->removeWidget(ui->poseImageLabelA);
    ui->poseImageLayout->removeWidget(ui->poseImageLabelB);
    ui->poseImageLabelA->hide();
    ui->poseImageLabelB->hide();

    m_skeletonView = new SkeletonViewWidget(ui->poseCard);
    ui->poseImageLayout->insertWidget(0, m_skeletonView, 1);
}

void MainWindow::installTrajectoryWidget()
{
    if (m_trajectoryWidget || !ui->trajectoryCardLayout || !ui->trajectoryViewFrame) {
        return;
    }

    ui->trajectoryCardLayout->removeWidget(ui->trajectoryViewFrame);
    ui->trajectoryViewFrame->hide();

    m_trajectoryWidget = new TrajectoryWidget(ui->trajectoryCard);
    m_trajectoryWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    ui->trajectoryCardLayout->insertWidget(1, m_trajectoryWidget, 1);
}

// 在训练统计卡片里装配分项评分条，替代原来的单行文字指标。
void MainWindow::installMetricBars()
{
    if (!ui->metricsLayout || !ui->saveTipLabel || !m_metricBars.isEmpty()) {
        return;
    }

    ui->saveTipLabel->hide();

    auto *metricContainer = new QWidget(ui->metricsCard);
    metricContainer->setObjectName(QStringLiteral("metricBarsContainer"));
    auto *metricLayout = new QVBoxLayout(metricContainer);
    metricLayout->setContentsMargins(0, 0, 0, 0);
    metricLayout->setSpacing(4);

    const QVector<QString> metricNames = {
        QStringLiteral("关键点"),
        QStringLiteral("对称"),
        QStringLiteral("重心"),
        QStringLiteral("稳定"),
        QStringLiteral("3D")
    };

    for (const QString &metricName : metricNames) {
        auto *row = new QWidget(metricContainer);
        row->setObjectName(QStringLiteral("metricBarRow"));
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(6);

        auto *nameLabel = new QLabel(metricName, row);
        nameLabel->setProperty("role", "metricName");
        nameLabel->setMinimumWidth(38);
        nameLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

        auto *bar = new QProgressBar(row);
        bar->setRange(0, 100);
        bar->setValue(0);
        bar->setTextVisible(false);
        bar->setProperty("role", "metricBar");
        bar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        auto *valueLabel = new QLabel(QStringLiteral("0"), row);
        valueLabel->setProperty("role", "metricValue");
        valueLabel->setMinimumWidth(24);
        valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

        rowLayout->addWidget(nameLabel);
        rowLayout->addWidget(bar, 1);
        rowLayout->addWidget(valueLabel);
        metricLayout->addWidget(row);

        m_metricBars.append(bar);
        m_metricValueLabels.append(valueLabel);
    }

    const int scoreBarIndex = ui->metricsLayout->indexOf(ui->scoreProgressBar);
    ui->metricsLayout->insertWidget(scoreBarIndex >= 0 ? scoreBarIndex : ui->metricsLayout->count(),
                                    metricContainer);
}

void MainWindow::installTrainingContextPanel()
{
    if (m_trainingContextPanel || !ui->capturePage || !ui->capturePage->layout()) {
        return;
    }

    auto *captureLayout = qobject_cast<QVBoxLayout *>(ui->capturePage->layout());
    if (!captureLayout) {
        return;
    }

    m_trainingContextPanel = new QFrame(ui->capturePage);
    m_trainingContextPanel->setObjectName(QStringLiteral("trainingContextPanel"));
    setRole(m_trainingContextPanel, "trainingContextPanel");
    auto *panelLayout = new QVBoxLayout(m_trainingContextPanel);
    panelLayout->setContentsMargins(12, 10, 12, 10);
    panelLayout->setSpacing(8);

    auto *titleRow = new QHBoxLayout();
    titleRow->setContentsMargins(0, 0, 0, 0);
    titleRow->setSpacing(8);
    auto *titleLabel = new QLabel(QStringLiteral("训练上下文"), m_trainingContextPanel);
    titleLabel->setProperty("role", "sectionTitle");
    m_standardDetailLabel = new QLabel(QStringLiteral("动作标准库初始化中"), m_trainingContextPanel);
    m_standardDetailLabel->setProperty("role", "muted");
    m_standardDetailLabel->setWordWrap(true);
    titleRow->addWidget(titleLabel, 0);
    titleRow->addWidget(m_standardDetailLabel, 1);
    panelLayout->addLayout(titleRow);

    auto *grid = new QGridLayout();
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(6);

    m_athleteComboBox = new QComboBox(m_trainingContextPanel);
    m_coachComboBox = new QComboBox(m_trainingContextPanel);
    m_actionStandardComboBox = new QComboBox(m_trainingContextPanel);
    m_siteLineEdit = new QLineEdit(m_trainingContextPanel);
    m_trainingPhaseComboBox = new QComboBox(m_trainingContextPanel);
    m_goalLineEdit = new QLineEdit(m_trainingContextPanel);
    m_targetRepsSpinBox = new QSpinBox(m_trainingContextPanel);
    m_targetScoreSpinBox = new QSpinBox(m_trainingContextPanel);
    m_setCountSpinBox = new QSpinBox(m_trainingContextPanel);
    m_restSecondsSpinBox = new QSpinBox(m_trainingContextPanel);

    m_siteLineEdit->setPlaceholderText(QStringLiteral("训练场地"));
    m_goalLineEdit->setPlaceholderText(QStringLiteral("本次训练目标"));
    m_trainingPhaseComboBox->addItems({QStringLiteral("热身"), QStringLiteral("基础训练"), QStringLiteral("专项训练"), QStringLiteral("复盘测试")});
    m_targetRepsSpinBox->setRange(1, 999);
    m_targetScoreSpinBox->setRange(1, 100);
    m_setCountSpinBox->setRange(1, 20);
    m_restSecondsSpinBox->setRange(0, 600);
    m_restSecondsSpinBox->setSingleStep(15);

    auto *addAthleteButton = new QPushButton(QStringLiteral("新增运动员"), m_trainingContextPanel);
    auto *addCoachButton = new QPushButton(QStringLiteral("新增教练"), m_trainingContextPanel);
    addAthleteButton->setProperty("role", "secondaryButton");
    addCoachButton->setProperty("role", "secondaryButton");

    grid->addWidget(new QLabel(QStringLiteral("运动员"), m_trainingContextPanel), 0, 0);
    grid->addWidget(m_athleteComboBox, 0, 1);
    grid->addWidget(addAthleteButton, 0, 2);
    grid->addWidget(new QLabel(QStringLiteral("教练"), m_trainingContextPanel), 0, 3);
    grid->addWidget(m_coachComboBox, 0, 4);
    grid->addWidget(addCoachButton, 0, 5);

    grid->addWidget(new QLabel(QStringLiteral("动作"), m_trainingContextPanel), 1, 0);
    grid->addWidget(m_actionStandardComboBox, 1, 1, 1, 2);
    grid->addWidget(new QLabel(QStringLiteral("场地"), m_trainingContextPanel), 1, 3);
    grid->addWidget(m_siteLineEdit, 1, 4, 1, 2);

    grid->addWidget(new QLabel(QStringLiteral("阶段"), m_trainingContextPanel), 2, 0);
    grid->addWidget(m_trainingPhaseComboBox, 2, 1);
    grid->addWidget(new QLabel(QStringLiteral("目标"), m_trainingContextPanel), 2, 2);
    grid->addWidget(m_goalLineEdit, 2, 3, 1, 3);

    grid->addWidget(new QLabel(QStringLiteral("次数"), m_trainingContextPanel), 3, 0);
    grid->addWidget(m_targetRepsSpinBox, 3, 1);
    grid->addWidget(new QLabel(QStringLiteral("目标分"), m_trainingContextPanel), 3, 2);
    grid->addWidget(m_targetScoreSpinBox, 3, 3);
    grid->addWidget(new QLabel(QStringLiteral("组数"), m_trainingContextPanel), 3, 4);
    grid->addWidget(m_setCountSpinBox, 3, 5);

    grid->addWidget(new QLabel(QStringLiteral("休息秒"), m_trainingContextPanel), 4, 0);
    grid->addWidget(m_restSecondsSpinBox, 4, 1);
    m_trainingTargetLabel = new QLabel(QStringLiteral("目标完成度：0/0"), m_trainingContextPanel);
    m_trainingTargetLabel->setProperty("role", "muted");
    grid->addWidget(m_trainingTargetLabel, 4, 2, 1, 4);

    panelLayout->addLayout(grid);
    captureLayout->insertWidget(0, m_trainingContextPanel);

    connect(addAthleteButton, &QPushButton::clicked, this, [this]() { addAthleteFromDialog(); });
    connect(addCoachButton, &QPushButton::clicked, this, [this]() { addCoachFromDialog(); });
    connect(m_actionStandardComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        refreshTrainingContextDetails();
    });
    connect(m_athleteComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        refreshTrainingContextDetails();
    });
    connect(m_targetRepsSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, [this]() { refreshStats(); });
    connect(m_targetScoreSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, [this]() { refreshStats(); });
}

// 从资源系统读取 QSS，统一应用暗色仪表盘主题样式。
void MainWindow::applyStyleSheet()
{
    QFile qssFile(QStringLiteral(":/styles/iskating.qss"));
    if (qssFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        setStyleSheet(QString::fromUtf8(qssFile.readAll()));
    }
}

// 从 QSettings 读取 12 路摄像头配置；预览使用子码流，主视图使用主码流，兼容旧版 url/IP 配置。
void MainWindow::loadCameraSettings()
{
    m_sharedCameraSettings = {};
    m_sharedCameraSettings.port = QString::number(kDefaultRtspPort);
    m_sharedCameraSettings.previewFps = kDefaultPreviewStreamFps;
    m_sharedCameraSettings.mainFps = kDefaultMainStreamFps;
    m_cameraSlotSettings = QVector<CameraSlotSettings>(m_cameraButtons.size());
    m_capturePreferenceSettings = {};

    QSettings settings;
    settings.beginGroup(QStringLiteral("capture"));
    const int legacyCaptureFps = settings.value(QStringLiteral("fps"), kDefaultFps).toInt();
    m_capturePreferenceSettings.modelPrecision = settings.value(QStringLiteral("modelPrecision"),
                                                                QStringLiteral("balanced")).toString().trimmed();
    settings.endGroup();
    if (m_capturePreferenceSettings.modelPrecision.isEmpty()) {
        m_capturePreferenceSettings.modelPrecision = QStringLiteral("balanced");
    }

    if (hasStructuredCameraDefaults(settings)) {
        settings.beginGroup(QStringLiteral("cameraDefaults"));
        m_sharedCameraSettings.username = settings.value(QStringLiteral("username")).toString().trimmed();
        m_sharedCameraSettings.password = settings.value(QStringLiteral("password")).toString();
        m_sharedCameraSettings.port = settings.value(QStringLiteral("port"), QString::number(kDefaultRtspPort)).toString().trimmed();
        if (m_sharedCameraSettings.port.isEmpty()) {
            m_sharedCameraSettings.port = QString::number(kDefaultRtspPort);
        }
        m_sharedCameraSettings.previewPath = normalizedStoredPath(settings.value(QStringLiteral("previewPath")).toString());
        m_sharedCameraSettings.previewFps = settings.value(QStringLiteral("previewFps"),
                                                           kDefaultPreviewStreamFps).toInt();
        m_sharedCameraSettings.mainPath = normalizedStoredPath(settings.value(QStringLiteral("mainPath")).toString());
        m_sharedCameraSettings.mainFps = settings.value(QStringLiteral("mainFps"),
                                                        legacyCaptureFps > kDefaultPreviewStreamFps
                                                            ? legacyCaptureFps
                                                            : kDefaultMainStreamFps).toInt();
        settings.endGroup();

        for (int i = 0; i < m_cameraButtons.size(); ++i) {
            settings.beginGroup(cameraSettingsGroup(i));
            m_cameraSlotSettings[i].ip = settings.value(QStringLiteral("ip")).toString().trimmed();
            settings.endGroup();
        }
    } else {
        bool sharedInitialized = false;
        for (int i = 0; i < m_cameraButtons.size(); ++i) {
            settings.beginGroup(cameraSettingsGroup(i));
            const QString previewUrl = settings.value(QStringLiteral("previewUrl")).toString().trimmed();
            const QString mainUrl = settings.value(QStringLiteral("mainUrl")).toString().trimmed();
            const QString legacyUrl = settings.value(QStringLiteral("url")).toString().trimmed();
            const QString legacyIp = settings.value(QStringLiteral("ip")).toString().trimmed();
            const QString legacyPort = settings.value(QStringLiteral("port")).toString().trimmed();
            const QString legacyPath = settings.value(QStringLiteral("path")).toString().trimmed();
            settings.endGroup();

            const QString previewSource = !previewUrl.isEmpty()
                                              ? previewUrl
                                              : (!legacyUrl.isEmpty() ? legacyUrl
                                                                      : composeLegacyStreamUrl(legacyIp, legacyPort, legacyPath));
            const QString mainSource = !mainUrl.isEmpty() ? mainUrl : previewSource;

            if (!previewSource.isEmpty()) {
                m_cameraSlotSettings[i].ip = extractHostFromLegacyValue(previewSource);
            } else {
                m_cameraSlotSettings[i].ip = extractHostFromLegacyValue(legacyIp);
            }

            if (!sharedInitialized) {
                const ParsedRtspUrl previewParsed = parseRtspUrl(previewSource);
                const ParsedRtspUrl mainParsed = parseRtspUrl(mainSource);
                const ParsedRtspUrl baseParsed = previewParsed.valid ? previewParsed : mainParsed;
                if (baseParsed.valid) {
                    m_sharedCameraSettings.username = baseParsed.username;
                    m_sharedCameraSettings.password = baseParsed.password;
                    m_sharedCameraSettings.port = baseParsed.port.isEmpty()
                                                      ? QString::number(kDefaultRtspPort)
                                                      : baseParsed.port;
                    m_sharedCameraSettings.previewPath = previewParsed.valid
                                                             ? previewParsed.path
                                                             : normalizedStoredPath(legacyPath);
                    m_sharedCameraSettings.previewFps = kDefaultPreviewStreamFps;
                    m_sharedCameraSettings.mainPath = mainParsed.valid ? mainParsed.path : QString();
                    m_sharedCameraSettings.mainFps = legacyCaptureFps > kDefaultPreviewStreamFps
                                                        ? legacyCaptureFps
                                                        : kDefaultMainStreamFps;
                    sharedInitialized = true;
                } else if (!legacyIp.isEmpty() || !legacyPort.isEmpty() || !legacyPath.isEmpty()) {
                    m_sharedCameraSettings.port = legacyPort.isEmpty()
                                                      ? QString::number(kDefaultRtspPort)
                                                      : legacyPort;
                    m_sharedCameraSettings.previewPath = normalizedStoredPath(legacyPath);
                    m_sharedCameraSettings.previewFps = kDefaultPreviewStreamFps;
                    m_sharedCameraSettings.mainPath.clear();
                    m_sharedCameraSettings.mainFps = legacyCaptureFps > kDefaultPreviewStreamFps
                                                        ? legacyCaptureFps
                                                        : kDefaultMainStreamFps;
                    sharedInitialized = true;
                }
            }
        }
    }

    if (m_sharedCameraSettings.previewFps <= 0) {
        m_sharedCameraSettings.previewFps = kDefaultPreviewStreamFps;
    }
    if (m_sharedCameraSettings.mainFps <= 0) {
        m_sharedCameraSettings.mainFps = legacyCaptureFps > kDefaultPreviewStreamFps
                                             ? legacyCaptureFps
                                             : kDefaultMainStreamFps;
    }
    m_capturePreferenceSettings.fps = m_sharedCameraSettings.mainFps;

    applyCameraSettingsToWidgets(false);
}

// 程序退出时保存全部摄像头配置，确保未触发单路保存的变更也会落盘。
void MainWindow::saveCameraSettings()
{
    m_capturePreferenceSettings = capturePreferenceSettingsFromUi();
    persistSystemSettings();
}

// 保存指定摄像头配置：名称、预览子码流、主画面码流；同时保留旧字段以兼容已有代码。
void MainWindow::saveCameraSetting(int cameraIndex)
{
    if (cameraIndex < 0 || cameraIndex >= m_cameraButtons.size()) {
        return;
    }

    saveCameraSettings();
}

void MainWindow::persistSystemSettings() const
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("cameraDefaults"));
    settings.setValue(QStringLiteral("username"), m_sharedCameraSettings.username.trimmed());
    settings.setValue(QStringLiteral("password"), m_sharedCameraSettings.password);
    settings.setValue(QStringLiteral("port"),
                      m_sharedCameraSettings.port.trimmed().isEmpty()
                          ? QString::number(kDefaultRtspPort)
                          : m_sharedCameraSettings.port.trimmed());
    settings.setValue(QStringLiteral("previewPath"), normalizedStoredPath(m_sharedCameraSettings.previewPath));
    settings.setValue(QStringLiteral("previewFps"), m_sharedCameraSettings.previewFps > 0
                                                      ? m_sharedCameraSettings.previewFps
                                                      : kDefaultPreviewStreamFps);
    settings.setValue(QStringLiteral("mainPath"), normalizedStoredPath(m_sharedCameraSettings.mainPath));
    settings.setValue(QStringLiteral("mainFps"), m_sharedCameraSettings.mainFps > 0
                                                   ? m_sharedCameraSettings.mainFps
                                                   : kDefaultMainStreamFps);
    settings.endGroup();

    settings.beginGroup(QStringLiteral("capture"));
    settings.setValue(QStringLiteral("modelPrecision"), m_capturePreferenceSettings.modelPrecision.trimmed().isEmpty()
                                                            ? QStringLiteral("balanced")
                                                            : m_capturePreferenceSettings.modelPrecision.trimmed());
    settings.setValue(QStringLiteral("fps"), m_sharedCameraSettings.mainFps > 0
                                                ? m_sharedCameraSettings.mainFps
                                                : kDefaultMainStreamFps);
    settings.endGroup();

    const QString normalizedPort = m_sharedCameraSettings.port.trimmed().isEmpty()
                                       ? QString::number(kDefaultRtspPort)
                                       : m_sharedCameraSettings.port.trimmed();
    const QString normalizedPreviewPath = normalizedStoredPath(m_sharedCameraSettings.previewPath);
    for (int i = 0; i < m_cameraButtons.size(); ++i) {
        const QString ip = i < m_cameraSlotSettings.size() ? m_cameraSlotSettings.at(i).ip.trimmed() : QString();
        const QString previewUrl = composeCameraUrl(m_sharedCameraSettings, ip, false);
        const QString mainUrl = composeCameraUrl(m_sharedCameraSettings, ip, true);
        settings.beginGroup(cameraSettingsGroup(i));
        settings.setValue(QStringLiteral("name"), configuredCameraChannelName(i, ip));
        settings.setValue(QStringLiteral("previewUrl"), previewUrl);
        settings.setValue(QStringLiteral("mainUrl"), mainUrl);
        settings.setValue(QStringLiteral("url"), previewUrl);
        settings.setValue(QStringLiteral("ip"), ip);
        settings.setValue(QStringLiteral("port"), normalizedPort);
        settings.setValue(QStringLiteral("path"), normalizedPreviewPath);
        settings.endGroup();
    }
    settings.sync();
}

CapturePreferenceSettings MainWindow::capturePreferenceSettingsFromUi() const
{
    CapturePreferenceSettings settings = m_capturePreferenceSettings;
    if (ui->precisionComboBox) {
        settings.modelPrecision = ui->precisionComboBox->currentData().toString().trimmed();
    }
    if (settings.modelPrecision.isEmpty()) {
        settings.modelPrecision = QStringLiteral("balanced");
    }

    if (ui->fpsComboBox) {
        settings.fps = m_sharedCameraSettings.mainFps > 0 ? m_sharedCameraSettings.mainFps : ui->fpsComboBox->currentData().toInt();
    }
    if (settings.fps <= 0) {
        settings.fps = kDefaultMainStreamFps;
    }
    return settings;
}

void MainWindow::applyCapturePreferencesToUi()
{
    if (ui->precisionComboBox) {
        const QString precisionValue = m_capturePreferenceSettings.modelPrecision.trimmed().isEmpty()
                                           ? QStringLiteral("balanced")
                                           : m_capturePreferenceSettings.modelPrecision.trimmed();
        const int precisionIndex = ui->precisionComboBox->findData(precisionValue);
        ui->precisionComboBox->setCurrentIndex(precisionIndex >= 0 ? precisionIndex : 1);
    }

    if (ui->fpsComboBox) {
        const int fpsValue = m_sharedCameraSettings.mainFps > 0 ? m_sharedCameraSettings.mainFps : kDefaultMainStreamFps;
        const int fpsIndex = ui->fpsComboBox->findData(fpsValue);
        ui->fpsComboBox->setCurrentIndex(fpsIndex >= 0 ? fpsIndex : ui->fpsComboBox->findData(kDefaultMainStreamFps));
    }
}

void MainWindow::applyCameraSettingsToWidgets(bool restorePlayback)
{
    if (m_cameraSlotSettings.size() < m_cameraButtons.size()) {
        m_cameraSlotSettings.resize(m_cameraButtons.size());
    }

    QVector<bool> previewWasPlaying;
    previewWasPlaying.reserve(m_cameraButtons.size());
    for (auto *cameraWidget : m_cameraButtons) {
        previewWasPlaying.append(restorePlayback && cameraWidget && cameraWidget->isPlaying());
    }
    const bool mainWasPlaying = restorePlayback && ui->mainImageLabel && ui->mainImageLabel->isPlaying();
    const bool shouldResumeStreams = restorePlayback && m_isRecording && !m_isPaused;

    for (int i = 0; i < m_cameraButtons.size(); ++i) {
        auto *cameraWidget = m_cameraButtons.at(i);
        const QString ip = m_cameraSlotSettings.at(i).ip.trimmed();
        const QString channelName = configuredCameraChannelName(i, ip);
        const QString previewUrl = composeCameraUrl(m_sharedCameraSettings, ip, false);
        const QString mainUrl = composeCameraUrl(m_sharedCameraSettings, ip, true);
        cameraWidget->setChannelName(channelName);
        cameraWidget->setPlaceholderText(channelName);
        cameraWidget->setStreamUrls(previewUrl, mainUrl);

        if (!restorePlayback) {
            continue;
        }

        if (previewUrl.trimmed().isEmpty()) {
            cameraWidget->stopPlayback();
        } else if (shouldResumeStreams || previewWasPlaying.value(i)) {
            cameraWidget->playDefaultVideo();
        }
    }

    if (!m_cameraButtons.isEmpty()) {
        int selectedIndex = std::max(0, m_selectedCamera - 1);
        if (selectedIndex >= m_cameraButtons.size()) {
            selectedIndex = m_cameraButtons.size() - 1;
        }
        showCameraInMainView(selectedIndex, shouldResumeStreams || mainWasPlaying);
    }
}

void MainWindow::openSystemSettings()
{
    SystemSettingsDialog dialog(m_cameraButtons.size(), this);
    dialog.setSharedCameraSettings(m_sharedCameraSettings);
    dialog.setCameraSlotSettings(m_cameraSlotSettings);
    dialog.setCapturePreferenceSettings(m_capturePreferenceSettings);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    m_sharedCameraSettings = dialog.sharedCameraSettings();
    m_cameraSlotSettings = dialog.cameraSlotSettings();
    m_capturePreferenceSettings = dialog.capturePreferenceSettings();
    m_capturePreferenceSettings.fps = m_sharedCameraSettings.mainFps;
    applyCapturePreferencesToUi();
    applyCameraSettingsToWidgets(true);
    saveCameraSettings();
}

void MainWindow::loadTrainingRecords()
{
    m_records.clear();
    if (m_trainingRepository && m_trainingRepository->isOpen()) {
        m_records = m_trainingRepository->recentSessions(200);
    }
    m_lastSavedAt = m_records.isEmpty() ? QString() : m_records.first().time;
}

void MainWindow::initializeTrainingRepository()
{
    if (!m_trainingRepository) {
        return;
    }

    QString errorMessage;
    if (!m_trainingRepository->open(&errorMessage)) {
        ui->storageStatusLabel->setText(topbarModuleHtml(QStringLiteral("DB"),
                                                         QStringLiteral("存储 Storage"),
                                                         QStringLiteral("数据库不可用")));
        ui->saveTipLabel->setText(QStringLiteral("训练数据库初始化失败：%1").arg(errorMessage));
        ui->saveTipLabel->show();
        qWarning() << "[MainWindow] training database open failed" << errorMessage;
        return;
    }

    ui->storageStatusLabel->setText(topbarModuleHtml(QStringLiteral("DB"),
                                                     QStringLiteral("存储 Storage"),
                                                     QStringLiteral("SQLite 已就绪")));
    reloadTrainingContext();
}

void MainWindow::reloadTrainingContext()
{
    if (!m_trainingRepository || !m_trainingRepository->isOpen()) {
        return;
    }

    const QString previousAthleteId = selectedAthleteId();
    const QString previousCoachId = selectedCoachId();
    const QString previousActionId = selectedActionStandard().id;

    m_athletes = m_trainingRepository->athletes();
    m_coaches = m_trainingRepository->coaches();
    m_actionStandards = m_trainingRepository->actionStandards();

    if (m_athleteComboBox) {
        QSignalBlocker blocker(m_athleteComboBox);
        m_athleteComboBox->clear();
        for (const AthleteProfile &athlete : std::as_const(m_athletes)) {
            m_athleteComboBox->addItem(athlete.name, athlete.id);
        }
        const int index = m_athleteComboBox->findData(previousAthleteId);
        if (index >= 0) {
            m_athleteComboBox->setCurrentIndex(index);
        }
    }

    if (m_coachComboBox) {
        QSignalBlocker blocker(m_coachComboBox);
        m_coachComboBox->clear();
        for (const CoachProfile &coach : std::as_const(m_coaches)) {
            m_coachComboBox->addItem(coach.name, coach.id);
        }
        const int index = m_coachComboBox->findData(previousCoachId);
        if (index >= 0) {
            m_coachComboBox->setCurrentIndex(index);
        }
    }

    if (m_actionStandardComboBox) {
        QSignalBlocker blocker(m_actionStandardComboBox);
        m_actionStandardComboBox->clear();
        for (const ActionStandard &standard : std::as_const(m_actionStandards)) {
            m_actionStandardComboBox->addItem(QStringLiteral("%1 · %2").arg(standard.categoryName, standard.name),
                                              standard.id);
        }
        const int index = m_actionStandardComboBox->findData(previousActionId);
        if (index >= 0) {
            m_actionStandardComboBox->setCurrentIndex(index);
        }
    }

    refreshTrainingContextDetails();
}

void MainWindow::refreshTrainingContextDetails()
{
    const ActionStandard standard = selectedActionStandard();
    if (!standard.id.isEmpty()) {
        if (m_targetRepsSpinBox && m_targetRepsSpinBox->value() <= 1) {
            m_targetRepsSpinBox->setValue(std::max(1, standard.targetReps));
        } else if (m_targetRepsSpinBox && !m_isRecording && m_actionCount == 0) {
            m_targetRepsSpinBox->setValue(std::max(1, standard.targetReps));
        }
        if (m_targetScoreSpinBox && !m_isRecording) {
            m_targetScoreSpinBox->setValue(std::clamp(standard.targetScore, 1, 100));
        }
        if (m_setCountSpinBox && !m_isRecording) {
            m_setCountSpinBox->setValue(std::max(1, standard.setCount));
        }
        if (m_restSecondsSpinBox && !m_isRecording) {
            m_restSecondsSpinBox->setValue(std::max(0, standard.restSeconds));
        }
        if (m_standardDetailLabel) {
            m_standardDetailLabel->setText(QStringLiteral("v%1 · %2 · %3 · %4")
                                               .arg(standard.version)
                                               .arg(standard.level)
                                               .arg(standard.purpose)
                                               .arg(standard.keyPoints));
        }
        if (m_actionRepetitionTracker) {
            m_actionRepetitionTracker->reset(standard);
        }
    } else if (m_standardDetailLabel) {
        m_standardDetailLabel->setText(QStringLiteral("暂无可用动作标准"));
    }
    refreshStats();
}

void MainWindow::addAthleteFromDialog()
{
    if (!m_trainingRepository || !m_trainingRepository->isOpen()) {
        return;
    }

    bool ok = false;
    const QString name = QInputDialog::getText(this,
                                               QStringLiteral("新增运动员"),
                                               QStringLiteral("运动员姓名"),
                                               QLineEdit::Normal,
                                               QString(),
                                               &ok);
    if (!ok || name.trimmed().isEmpty()) {
        return;
    }

    QString athleteId;
    QString errorMessage;
    if (!m_trainingRepository->createAthlete(name, &athleteId, &errorMessage)) {
        QMessageBox::warning(this, QStringLiteral("新增失败"), errorMessage);
        return;
    }
    reloadTrainingContext();
    if (m_athleteComboBox) {
        const int index = m_athleteComboBox->findData(athleteId);
        if (index >= 0) {
            m_athleteComboBox->setCurrentIndex(index);
        }
    }
}

void MainWindow::addCoachFromDialog()
{
    if (!m_trainingRepository || !m_trainingRepository->isOpen()) {
        return;
    }

    bool ok = false;
    const QString name = QInputDialog::getText(this,
                                               QStringLiteral("新增教练"),
                                               QStringLiteral("教练姓名"),
                                               QLineEdit::Normal,
                                               QString(),
                                               &ok);
    if (!ok || name.trimmed().isEmpty()) {
        return;
    }

    QString coachId;
    QString errorMessage;
    if (!m_trainingRepository->createCoach(name, &coachId, &errorMessage)) {
        QMessageBox::warning(this, QStringLiteral("新增失败"), errorMessage);
        return;
    }
    reloadTrainingContext();
    if (m_coachComboBox) {
        const int index = m_coachComboBox->findData(coachId);
        if (index >= 0) {
            m_coachComboBox->setCurrentIndex(index);
        }
    }
}

ActionStandard MainWindow::selectedActionStandard() const
{
    if (!m_actionStandardComboBox) {
        return {};
    }
    const QString selectedId = m_actionStandardComboBox->currentData().toString();
    for (const ActionStandard &standard : m_actionStandards) {
        if (standard.id == selectedId) {
            return standard;
        }
    }
    return {};
}

QString MainWindow::selectedAthleteId() const
{
    return m_athleteComboBox ? m_athleteComboBox->currentData().toString() : QString();
}

QString MainWindow::selectedCoachId() const
{
    return m_coachComboBox ? m_coachComboBox->currentData().toString() : QString();
}

void MainWindow::resetCurrentTrainingSession()
{
    m_durationSec = 0;
    m_actionCount = 0;
    m_validActionCount = 0;
    m_bestActionScore = 0;
    m_actionScoreTotal = 0;
    m_currentRepetitions.clear();
    m_previousKneeBend = 0.0;
    m_actionArmed = false;
    m_lastActionMsec = 0;
    m_recordingStartedAtMsec = QDateTime::currentMSecsSinceEpoch();
    m_recordingStartedAt = QDateTime::currentDateTime();
    if (m_actionRepetitionTracker) {
        m_actionRepetitionTracker->reset(selectedActionStandard());
    }
}

void MainWindow::recordCompletedRepetition(const ActionRepetition &repetition)
{
    m_currentRepetitions.append(repetition);
    m_actionCount = m_currentRepetitions.size();
    m_validActionCount = 0;
    m_bestActionScore = 0;
    m_actionScoreTotal = 0;
    for (const ActionRepetition &item : std::as_const(m_currentRepetitions)) {
        if (item.valid) {
            ++m_validActionCount;
        }
        m_bestActionScore = std::max(m_bestActionScore, item.score);
        m_actionScoreTotal += item.score;
    }
    refreshStats();
}

// 将指定摄像头主码流显示到主视图；小窗预览仍保持子码流。
void MainWindow::showCameraInMainView(int cameraIndex, bool autoPlay)
{
    if (cameraIndex < 0 || cameraIndex >= m_cameraButtons.size()) {
        return;
    }

    auto *cameraWidget = m_cameraButtons.at(cameraIndex);
    m_selectedCamera = cameraIndex + 1;
    const QString source = cameraWidget->mainUrl();
    clearRealtimePose();
    if (source.trimmed().isEmpty()) {
        ui->mainImageLabel->stopPlayback();
        ui->mainImageLabel->setPlaceholderText(QStringLiteral("主视频\n未配置"));
        if (m_handAnalysisManager) {
            m_handAnalysisManager->setPaused(true);
            m_handAnalysisManager->setActiveStream(0, {});
        }
    } else if (!autoPlay) {
        ui->mainImageLabel->stopPlayback();
        ui->mainImageLabel->setPlaceholderText(cameraWidget->channelName());
        if (m_handAnalysisManager) {
            m_handAnalysisManager->setPaused(true);
            m_handAnalysisManager->setActiveStream(0, {});
        }
    } else {
        ui->mainImageLabel->setPlaceholderText(cameraWidget->channelName());
        ui->mainImageLabel->playMainUrlWithFallback(source, cameraWidget->previewUrl());
        if (m_handAnalysisManager) {
            m_handAnalysisManager->setPaused(m_isPaused);
            m_handAnalysisManager->setActiveStream(m_selectedCamera, ui->mainImageLabel->activeStream());
        }
    }

    ui->focusTitleLabel->setText(QStringLiteral("当前来源：%1").arg(cameraWidget->channelName()));
    refreshCameraButtons();
}

// 切换主页面堆栈；pageIndex 对应实时采集、历史分析和纠正建议等页面。
void MainWindow::switchPage(int pageIndex)
{
    if (!ui->pages || pageIndex < 0 || pageIndex >= ui->pages->count()) {
        return;
    }

    ui->pages->setCurrentIndex(pageIndex);
    m_activePage = pageIndex;
    refreshNavButtons();

    if (pageIndex == kHistoryPage) {
        refreshHistory();
    } else if (pageIndex == kSuggestionPage) {
        refreshSuggestions();
    }
}

// 显示或隐藏左侧侧栏：折叠布局中的控件和间距，避免隐藏后仍占用宽度。
void MainWindow::toggleSidebar()
{
    m_sidebarVisible = !m_sidebarVisible;

    if (!m_sidebarMetricsCaptured) {
        m_sidebarLayoutMargins = ui->sidebarLayout->contentsMargins();
        m_sidebarLayoutSpacing = ui->sidebarLayout->spacing();
        m_sidebarMetricsCaptured = true;
    }

    setLayoutItemsVisible(ui->sidebarLayout, m_sidebarVisible);
    ui->sidebarLayout->setContentsMargins(m_sidebarVisible ? m_sidebarLayoutMargins : QMargins(0, 0, 0, 0));
    ui->sidebarLayout->setSpacing(m_sidebarVisible ? m_sidebarLayoutSpacing : 0);

    if (auto *sidebarWidget = ui->sidebarLayout->parentWidget();
        sidebarWidget && sidebarWidget->objectName() == QLatin1String("sidebar")) {
        if (!sidebarWidget->property("normalMinimumWidth").isValid()) {
            sidebarWidget->setProperty("normalMinimumWidth", sidebarWidget->minimumWidth());
            sidebarWidget->setProperty("normalMaximumWidth", sidebarWidget->maximumWidth());
        }

        if (m_sidebarVisible) {
            sidebarWidget->setMinimumWidth(sidebarWidget->property("normalMinimumWidth").toInt());
            sidebarWidget->setMaximumWidth(sidebarWidget->property("normalMaximumWidth").toInt());
        } else {
            sidebarWidget->setMinimumWidth(0);
            sidebarWidget->setMaximumWidth(0);
        }
    }

    if (auto *sideBarAfter = findChild<QWidget *>(QStringLiteral("sideBarAfter"))) {
        sideBarAfter->setVisible(m_sidebarVisible);
    }

    refreshSidebarButton();

    ui->sidebarLayout->invalidate();
    if (ui->sidebarLayout->parentWidget() && ui->sidebarLayout->parentWidget()->layout()) {
        ui->sidebarLayout->parentWidget()->layout()->invalidate();
        ui->sidebarLayout->parentWidget()->layout()->activate();
    }
}

// 切换全屏状态：未全屏时记录原窗口状态并进入全屏，已全屏时恢复原状态。
void MainWindow::toggleFullScreen()
{
    if (isFullScreen()) {
        exitFullScreenMode();
        return;
    }

    setProperty(kPreviousWindowStateProperty,
                static_cast<int>((windowState() & ~Qt::WindowFullScreen).toInt()));
    setWindowState(windowState() | Qt::WindowFullScreen);
    refreshFullScreenButton();
}

// 退出全屏：Esc 快捷键和全屏按钮都会复用这里，保证恢复逻辑一致。
void MainWindow::exitFullScreenMode()
{
    if (!isFullScreen()) {
        refreshFullScreenButton();
        return;
    }

    const auto previousState = static_cast<Qt::WindowStates>(
        property(kPreviousWindowStateProperty).toInt());
    setWindowState(previousState & ~Qt::WindowFullScreen);
    refreshFullScreenButton();
}

// 记录当前选择的摄像头编号，并在后续采集/保存时作为当前通道使用。
void MainWindow::selectCamera(int cameraId)
{
    if (cameraId < 1 || cameraId > m_cameraButtons.size()) {
        return;
    }

    showCameraInMainView(cameraId - 1);
}

// 兼容旧的二态调用：true 表示扩展到上方 12 路视频区域，false 表示恢复正常区域。
void MainWindow::setTrajectoryExpanded(bool expanded)
{
    setTrajectoryMode(expanded ? TrajectoryExpanded : TrajectoryNormal);
}

// 按“正常区域 -> 扩展区域 -> 最小化 -> 正常区域”的顺序循环切换三维轨迹显示状态。
void MainWindow::cycleTrajectoryMode()
{
    const int currentMode = m_trajectoryMode < 0 ? TrajectoryNormal : m_trajectoryMode;
    setTrajectoryMode((currentMode + 1) % 3);
}

// 设置三维轨迹的显示模式：正常状态严格恢复启动时布局参数，扩展/最小化只做临时调整。
void MainWindow::setTrajectoryMode(int mode)
{
    mode = ((mode % 3) + 3) % 3;
    if (m_trajectoryMode == mode) {
        refreshTrajectoryModeButton();
        return;
    }

    m_trajectoryMode = mode;
    QWidget *trajectoryView = m_trajectoryWidget
                                  ? static_cast<QWidget *>(m_trajectoryWidget)
                                  : static_cast<QWidget *>(ui->trajectoryViewFrame);

    const bool expanded = (mode == TrajectoryExpanded);
    const bool minimized = (mode == TrajectoryMinimized);

    ui->middleLayout->setSpacing(expanded ? 0 : m_middleLayoutNormalSpacing);
    ui->middleLayout->setStretch(0, expanded ? 0 : m_middleLayoutNormalStretch0);
    ui->middleLayout->setStretch(1, expanded ? 1 : m_middleLayoutNormalStretch1);
    ui->cameraGridLayout->setSpacing(expanded ? 0 : m_cameraGridNormalSpacing);
    for (auto *cameraWidget : m_cameraButtons) {
        cameraWidget->setVisible(!expanded);
        cameraWidget->updateGeometry();
    }
    trajectoryView->setVisible(!minimized);
    ui->legendFrame->setVisible(expanded && !minimized);
    ui->expandTrajectoryButton->setVisible(true);
    ui->collapseTrajectoryButton->setVisible(false);

    if (expanded) {
        ui->trajectoryCard->setMinimumSize(m_trajectoryCardNormalMinSize);
        ui->trajectoryCard->setMaximumSize(m_trajectoryCardNormalMaxSize);
        ui->trajectoryCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        trajectoryView->setMinimumSize(m_trajectoryViewNormalMinSize);
        trajectoryView->setMaximumSize(m_trajectoryViewNormalMaxSize);
        trajectoryView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    } else if (minimized) {
        const int minimizedHeight = std::max(42, ui->trajectoryTitleLabel->sizeHint().height() + 30);
        ui->trajectoryCard->setMinimumSize(QSize(m_trajectoryCardNormalMinSize.width(), minimizedHeight));
        ui->trajectoryCard->setMaximumSize(QSize(QWIDGETSIZE_MAX, minimizedHeight));
        ui->trajectoryCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        trajectoryView->setMinimumSize(QSize(0, 0));
        trajectoryView->setMaximumSize(QSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX));
        trajectoryView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    } else {
        ui->trajectoryCard->setMinimumSize(m_trajectoryCardNormalMinSize);
        ui->trajectoryCard->setMaximumSize(m_trajectoryCardNormalMaxSize);
        ui->trajectoryCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        trajectoryView->setMinimumSize(m_trajectoryViewNormalMinSize);
        trajectoryView->setMaximumSize(m_trajectoryViewNormalMaxSize);
        trajectoryView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    }

    refreshTrajectoryModeButton();
    ui->trajectoryCard->updateGeometry();
    trajectoryView->updateGeometry();
    ui->middleLayout->invalidate();
    if (ui->capturePage->layout()) {
        ui->capturePage->layout()->invalidate();
        ui->capturePage->layout()->activate();
    }
}

// 刷新三维轨迹右侧三角形符号和提示，符号表示下一次点击将进入的状态。
void MainWindow::refreshTrajectoryModeButton()
{
    QString text;
    QString tip;
    switch (m_trajectoryMode) {
    case TrajectoryExpanded:
        text = QStringLiteral("▼");
        tip = QStringLiteral("最小化三维轨迹，只保留标题栏");
        break;
    case TrajectoryMinimized:
        text = QStringLiteral("▲");
        tip = QStringLiteral("恢复三维轨迹到当前区域");
        break;
    case TrajectoryNormal:
    default:
        text = QStringLiteral("▲");
        tip = QStringLiteral("展开三维轨迹，占用上方12路视频区域");
        break;
    }

    ui->expandTrajectoryButton->setText(text);
    ui->expandTrajectoryButton->setToolTip(tip);
    ui->expandTrajectoryButton->setStatusTip(tip);
    ui->collapseTrajectoryButton->setText(text);
    ui->collapseTrajectoryButton->setToolTip(tip);
    ui->collapseTrajectoryButton->setStatusTip(tip);
}

// 开始采集：主视图接入第一路视频流，12 路预览分别接入各自配置的真实视频流。
void MainWindow::startCapture()
{
    qDebug() << "[MainWindow] startCapture clicked";
    if (!m_trainingRepository || !m_trainingRepository->isOpen()) {
        ui->saveTipLabel->setText(QStringLiteral("训练数据库未就绪，无法开始标准闭环采集。"));
        ui->saveTipLabel->show();
        return;
    }
    if (selectedAthleteId().isEmpty()) {
        ui->saveTipLabel->setText(QStringLiteral("请先选择或新增运动员。"));
        ui->saveTipLabel->show();
        return;
    }
    if (selectedActionStandard().id.isEmpty()) {
        ui->saveTipLabel->setText(QStringLiteral("请先选择训练动作标准。"));
        ui->saveTipLabel->show();
        return;
    }
    if (!m_isRecording) {
        resetCurrentTrainingSession();
    }
    m_isRecording = true;
    m_isPaused = false;
    if (!m_timer.isActive()) {
        m_timer.start();
    }
    if (m_handAnalysisManager) {
        m_handAnalysisManager->setPaused(false);
    }
    if (!m_cameraButtons.isEmpty()) {
        qDebug() << "[MainWindow] main view stream" << safeUrlForLog(m_cameraButtons.first()->mainUrl());
        showCameraInMainView(0);
    }
    for (auto *videoWidget : m_cameraButtons) {
        qDebug() << "[MainWindow] camera stream"
                 << videoWidget->channelName()
                 << safeUrlForLog(videoWidget->previewUrl());
        videoWidget->playDefaultVideo();
    }
}

// 暂停采集：暂停主视图和全部摄像头预览的播放器状态。
void MainWindow::pauseCapture()
{
    qDebug() << "[MainWindow] pauseCapture clicked";
    m_isPaused = true;
    m_timer.stop();
    if (m_handAnalysisManager) {
        m_handAnalysisManager->setPaused(true);
    }
    clearRealtimePose();
    ui->mainImageLabel->pausePlayback();
    for (auto *videoWidget : m_cameraButtons) {
        videoWidget->pausePlayback();
    }
}

// 停止采集：停止主视图和全部摄像头预览，并恢复未播放占位状态。
void MainWindow::stopCapture()
{
    qDebug() << "[MainWindow] stopCapture clicked";
    m_isRecording = false;
    m_isPaused = false;
    m_timer.stop();
    m_previousKneeBend = 0.0;
    m_actionArmed = false;
    m_lastActionMsec = 0;
    if (m_handAnalysisManager) {
        m_handAnalysisManager->setPaused(true);
        m_handAnalysisManager->setActiveStream(0, {});
    }
    clearRealtimePose();
    ui->mainImageLabel->stopPlayback();
    for (auto *videoWidget : m_cameraButtons) {
        videoWidget->stopPlayback();
    }
}

// 保存当前训练记录；后续可在此持久化训练时长、动作数量和模型评分。
void MainWindow::saveRecord()
{
    if (m_durationSec <= 0) {
        ui->saveTipLabel->setText(QStringLiteral("暂无可保存的训练记录，请先开始采集并累计训练时长。"));
        ui->saveTipLabel->show();
        return;
    }

    if (!m_trainingRepository || !m_trainingRepository->isOpen()) {
        ui->saveTipLabel->setText(QStringLiteral("训练数据库未就绪，无法保存记录。"));
        ui->saveTipLabel->show();
        return;
    }

    const QString athleteId = selectedAthleteId();
    const QString coachId = selectedCoachId();
    const ActionStandard standard = selectedActionStandard();
    if (athleteId.isEmpty() || standard.id.isEmpty()) {
        ui->saveTipLabel->setText(QStringLiteral("保存前需要选择运动员和动作标准。"));
        ui->saveTipLabel->show();
        return;
    }

    QString planId;
    QString taskId;
    QString errorMessage;
    const int targetReps = m_targetRepsSpinBox ? m_targetRepsSpinBox->value() : standard.targetReps;
    const int targetScore = m_targetScoreSpinBox ? m_targetScoreSpinBox->value() : standard.targetScore;
    const int setCount = m_setCountSpinBox ? m_setCountSpinBox->value() : standard.setCount;
    const int restSeconds = m_restSecondsSpinBox ? m_restSecondsSpinBox->value() : standard.restSeconds;
    const QString site = m_siteLineEdit ? m_siteLineEdit->text().trimmed() : QString();
    const QString trainingPhase = m_trainingPhaseComboBox ? m_trainingPhaseComboBox->currentText() : QString();
    const QString goal = m_goalLineEdit ? m_goalLineEdit->text().trimmed() : QString();

    if (!m_trainingRepository->ensureDailyTask(athleteId,
                                               coachId,
                                               standard.id,
                                               standard.version,
                                               targetReps,
                                               targetScore,
                                               setCount,
                                               restSeconds,
                                               site,
                                               trainingPhase,
                                               goal,
                                               &planId,
                                               &taskId,
                                               &errorMessage)) {
        QMessageBox::warning(this, QStringLiteral("保存失败"), errorMessage);
        return;
    }

    TrainingSession session;
    session.athleteId = athleteId;
    session.coachId = coachId;
    session.planId = planId;
    session.taskId = taskId;
    session.actionStandardId = standard.id;
    session.standardVersion = standard.version;
    session.startedAt = m_recordingStartedAt.isValid() ? m_recordingStartedAt : QDateTime::currentDateTime().addSecs(-m_durationSec);
    session.savedAt = QDateTime::currentDateTime();
    session.durationSec = m_durationSec;
    session.totalReps = m_actionCount;
    session.validReps = m_validActionCount;
    session.averageScore = m_actionCount > 0
                               ? std::clamp((m_actionScoreTotal + m_actionCount / 2) / std::max(1, m_actionCount), 0, 100)
                               : m_realtimeScore;
    session.bestScore = m_bestActionScore > 0 ? m_bestActionScore : m_realtimeScore;
    session.camera = m_selectedCamera;
    session.modelPrecision = ui->precisionComboBox
                                 ? ui->precisionComboBox->currentData().toString()
                                 : QStringLiteral("balanced");
    if (session.modelPrecision.isEmpty()) {
        session.modelPrecision = QStringLiteral("balanced");
    }
    session.fps = m_sharedCameraSettings.mainFps > 0 ? m_sharedCameraSettings.mainFps : kDefaultMainStreamFps;
    if (session.fps <= 0) {
        session.fps = kDefaultMainStreamFps;
    }
    session.detectionScore = m_detectionScore;
    session.symmetryScore = m_symmetryScore;
    session.balanceScore = m_balanceScore;
    session.stabilityScore = m_stabilityScore;
    session.depthScore = m_depthScore;
    session.site = site;
    session.trainingPhase = trainingPhase;
    session.goal = goal;
    session.targetReps = targetReps;
    session.targetScore = targetScore;
    session.setCount = setCount;
    session.restSeconds = restSeconds;
    if (m_selectedCamera > 0 && m_selectedCamera <= m_cameraButtons.size()) {
        const VideoOpenGLWidget *cameraWidget = m_cameraButtons.at(m_selectedCamera - 1);
        session.videoSource = cameraWidget->mainUrl().trimmed();
        session.videoFallbackSource = cameraWidget->previewUrl().trimmed();
        session.videoCameraName = cameraWidget->channelName();
    }
    session.feedback = m_feedbackText.trimmed().isEmpty() ? QStringLiteral("等待姿态") : m_feedbackText.trimmed();

    QVector<ActionRepetition> repetitions = m_currentRepetitions;
    for (ActionRepetition &repetition : repetitions) {
        repetition.sessionId = session.id;
        repetition.actionStandardId = standard.id;
        repetition.standardVersion = standard.version;
        repetition.videoClipStartMs = std::max(0, repetition.startedMs - 1500);
        repetition.videoClipEndMs = std::max(repetition.endedMs + 1500, repetition.videoClipStartMs);
    }
    if (!m_trainingRepository->saveTrainingSession(&session, repetitions, &errorMessage)) {
        QMessageBox::warning(this, QStringLiteral("保存失败"), errorMessage);
        return;
    }

    loadTrainingRecords();
    m_lastSavedAt = session.savedAt.toString(QStringLiteral("yyyy-MM-dd hh:mm:ss"));
    ui->saveTipLabel->setText(QStringLiteral("最近保存：%1").arg(m_lastSavedAt));
    ui->saveTipLabel->show();

    refreshHistory();
    refreshSuggestions();
}

// 训练计时器回调：用于累计训练时长、刷新统计数据和实时反馈。
void MainWindow::tick()
{
    if (!m_isRecording || m_isPaused) {
        return;
    }
    ++m_durationSec;
    refreshStats();
}

void MainWindow::updateActionCounter(const PoseFrameResult &poseFrame)
{
    if (!m_isRecording || m_isPaused || poseFrame.instances.isEmpty()) {
        return;
    }

    const PoseInstance *person = nullptr;
    for (const PoseInstance &instance : poseFrame.instances) {
        if (!person || instance.confidence > person->confidence) {
            person = &instance;
        }
    }
    if (!person || person->keypoints.size() <= 16) {
        return;
    }

    auto keypoint = [person](int index) -> const PoseKeypoint * {
        if (index < 0 || index >= person->keypoints.size()) {
            return nullptr;
        }
        const PoseKeypoint &kp = person->keypoints.at(index);
        return kp.valid ? &kp : nullptr;
    };

    const PoseKeypoint *leftHip = keypoint(11);
    const PoseKeypoint *rightHip = keypoint(12);
    const PoseKeypoint *leftKnee = keypoint(13);
    const PoseKeypoint *rightKnee = keypoint(14);
    if (!leftHip || !rightHip || !leftKnee || !rightKnee) {
        return;
    }

    const qreal hipY = (leftHip->imagePoint.y() + rightHip->imagePoint.y()) * 0.5;
    const qreal kneeY = (leftKnee->imagePoint.y() + rightKnee->imagePoint.y()) * 0.5;
    const qreal bodyScale = std::max<qreal>(1.0, person->box.height());
    const qreal kneeBend = (kneeY - hipY) / bodyScale;

    if (kneeBend > 0.28) {
        m_actionArmed = true;
    }
    if (m_actionArmed && kneeBend < 0.18) {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - m_lastActionMsec > 900) {
            ++m_actionCount;
            m_lastActionMsec = now;
            refreshStats();
        }
        m_actionArmed = false;
    }
    m_previousKneeBend = kneeBend;
}

// 根据当前页面刷新左侧导航按钮的选中/普通状态。
void MainWindow::refreshNavButtons()
{
    for (int i = 0; i < m_navButtons.size(); ++i) {
        if (QPushButton *button = m_navButtons.at(i)) {
            button->setProperty("active", i == m_activePage);
            repolish(button);
        }
    }
}

// 根据当前选择的摄像头刷新 12 路预览控件的选中状态。
void MainWindow::refreshCameraButtons()
{
    for (int i = 0; i < m_cameraButtons.size(); ++i) {
        auto *cameraWidget = m_cameraButtons.at(i);
        cameraWidget->setProperty("selected", i + 1 == m_selectedCamera);
        repolish(cameraWidget);
    }
}

// 刷新动作计数、训练时长、实时得分等统计卡片。
void MainWindow::refreshStats()
{
    const QVector<int> metricScores = {
        m_detectionScore,
        m_symmetryScore,
        m_balanceScore,
        m_stabilityScore,
        m_depthScore
    };

    const int targetReps = m_targetRepsSpinBox ? m_targetRepsSpinBox->value() : 0;
    ui->actionValueLabel->setText(targetReps > 0
                                      ? QStringLiteral("%1/%2").arg(m_validActionCount).arg(targetReps)
                                      : QString::number(m_actionCount));
    ui->durationValueLabel->setText(formatTime(m_durationSec));
    ui->scoreValueLabel->setText(QStringLiteral("%1/100 · %2").arg(m_realtimeScore).arg(m_feedbackText));
    ui->scoreProgressBar->setValue(std::clamp(m_realtimeScore, 0, 100));
    if (m_trainingTargetLabel) {
        const int targetScore = m_targetScoreSpinBox ? m_targetScoreSpinBox->value() : 0;
        const int averageActionScore = m_actionCount > 0
                                           ? (m_actionScoreTotal + m_actionCount / 2) / std::max(1, m_actionCount)
                                           : 0;
        m_trainingTargetLabel->setText(QStringLiteral("目标完成度：有效 %1/%2 · 总动作 %3 · 均分 %4/%5 · 最好 %6")
                                           .arg(m_validActionCount)
                                           .arg(targetReps)
                                           .arg(m_actionCount)
                                           .arg(averageActionScore)
                                           .arg(targetScore)
                                           .arg(m_bestActionScore));
    }

    const int metricCount = std::min({metricScores.size(), m_metricBars.size(), m_metricValueLabels.size()});
    for (int i = 0; i < metricCount; ++i) {
        const int value = std::clamp(metricScores.at(i), 0, 100);
        m_metricBars.at(i)->setValue(value);
        m_metricValueLabels.at(i)->setText(QString::number(value));
    }
}

// 重新生成训练历史列表和概要统计卡片。
void MainWindow::refreshHistory()
{
    if (!ui->historyListLayout) {
        return;
    }

    int totalActions = 0;
    int totalScore = 0;
    int bestScore = 0;
    for (const SessionHistoryItem &record : std::as_const(m_records)) {
        totalActions += record.validReps;
        totalScore += record.score;
        bestScore = std::max(bestScore, record.score);
    }

    const int sessionCount = m_records.size();
    const int averageScore = sessionCount > 0 ? (totalScore + sessionCount / 2) / sessionCount : 0;
    if (m_summaryValues.size() >= 4) {
        m_summaryValues.at(0)->setText(QString::number(sessionCount));
        m_summaryValues.at(1)->setText(QString::number(totalActions));
        m_summaryValues.at(2)->setText(QString::number(averageScore));
        m_summaryValues.at(3)->setText(QString::number(bestScore));
    }

    clearLayout(ui->historyListLayout);

    if (m_records.isEmpty()) {
        auto *emptyLabel = new QLabel(QStringLiteral("暂无训练历史记录。\n开始一次训练并点击“保存记录”后，这里会显示最近训练数据。"),
                                      ui->historyPage);
        emptyLabel->setProperty("role", "emptyBox");
        emptyLabel->setAlignment(Qt::AlignCenter);
        emptyLabel->setWordWrap(true);
        ui->historyListLayout->addWidget(emptyLabel);
        return;
    }

    const int visibleCount = std::min(kMaxVisibleHistoryItems, static_cast<int>(m_records.size()));
    for (int i = 0; i < visibleCount; ++i) {
        const SessionHistoryItem &record = m_records.at(i);
        const QVector<ActionRepetition> repetitions = m_trainingRepository && m_trainingRepository->isOpen()
                                                          ? m_trainingRepository->repetitionsForSession(record.id)
                                                          : QVector<ActionRepetition>();

        auto *card = new QFrame(ui->historyPage);
        setRole(card, "historyItem");
        auto *cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(14, 14, 14, 14);
        cardLayout->setSpacing(10);

        auto *headerLayout = new QHBoxLayout();
        headerLayout->setContentsMargins(0, 0, 0, 0);
        headerLayout->setSpacing(8);

        auto *timeLabel = new QLabel(QStringLiteral("%1 · %2 · %3")
                                         .arg(record.time, record.athleteName, record.actionName),
                                     card);
        timeLabel->setWordWrap(true);

        auto *scoreTag = new QLabel(QStringLiteral("%1 分").arg(record.score), card);
        scoreTag->setProperty("role", "scoreTag");
        scoreTag->setAlignment(Qt::AlignCenter);

        auto *playButton = new QPushButton(QStringLiteral("回看视频"), card);
        playButton->setProperty("role", "secondaryButton");
        playButton->setEnabled(!record.videoSource.trimmed().isEmpty()
                               || !record.videoFallbackSource.trimmed().isEmpty());
        connect(playButton, &QPushButton::clicked, this, [this, record]() {
            openSessionVideo(record);
        });

        auto *commentButton = new QPushButton(QStringLiteral("教练批注"), card);
        commentButton->setProperty("role", "secondaryButton");
        connect(commentButton, &QPushButton::clicked, this, [this, record]() {
            editCoachComment(record.id);
        });

        auto *exportButton = new QPushButton(QStringLiteral("导出报告"), card);
        exportButton->setProperty("role", "secondaryButton");
        connect(exportButton, &QPushButton::clicked, this, [this, record]() {
            exportTrainingReport(record.id);
        });

        headerLayout->addWidget(timeLabel, 1);
        headerLayout->addWidget(playButton, 0, Qt::AlignRight | Qt::AlignTop);
        headerLayout->addWidget(commentButton, 0, Qt::AlignRight | Qt::AlignTop);
        headerLayout->addWidget(exportButton, 0, Qt::AlignRight | Qt::AlignTop);
        headerLayout->addWidget(scoreTag, 0, Qt::AlignRight | Qt::AlignTop);
        cardLayout->addLayout(headerLayout);

        auto *metaLabel = new QLabel(
            QStringLiteral("时长 %1   有效/总动作 %2/%3   目标 %4 次/%5 分   最佳 %6 分   机位 CAM %7   精度 %8   主码流 %9 FPS")
                .arg(formatTime(record.duration))
                .arg(record.validReps)
                .arg(record.totalReps)
                .arg(record.targetReps)
                .arg(record.targetScore)
                .arg(record.bestScore)
                .arg(pad(record.camera))
                .arg(precisionLabel(record.modelPrecision))
                .arg(record.fps),
            card);
        metaLabel->setProperty("role", "muted");
        metaLabel->setWordWrap(true);
        cardLayout->addWidget(metaLabel);

        auto *feedbackLabel = new QLabel(QStringLiteral("标准 v%1 · %2 · %3\n场地：%4   阶段：%5   目标：%6\n视频：%7\n反馈：%8")
                                             .arg(record.standardVersion)
                                             .arg(record.actionCategory)
                                             .arg(record.coachName.isEmpty() ? QStringLiteral("未指定教练") : record.coachName)
                                             .arg(record.site.isEmpty() ? QStringLiteral("未填写") : record.site)
                                             .arg(record.trainingPhase.isEmpty() ? QStringLiteral("未填写") : record.trainingPhase)
                                             .arg(record.goal.isEmpty() ? QStringLiteral("未填写") : record.goal)
                                             .arg(displayMediaSource(record.videoSource))
                                             .arg(record.feedback),
                                         card);
        feedbackLabel->setWordWrap(true);
        cardLayout->addWidget(feedbackLabel);

        if (repetitions.isEmpty()) {
            auto *emptyReviewLabel = new QLabel(QStringLiteral("本次尚未保存动作实例。复盘会先展示 session 摘要，后续采集到动作计数后会自动列出动作明细、最好/最差动作和错误时间轴。"),
                                                card);
            emptyReviewLabel->setProperty("role", "muted");
            emptyReviewLabel->setWordWrap(true);
            cardLayout->addWidget(emptyReviewLabel);
        } else {
            const auto bestIt = std::max_element(repetitions.cbegin(),
                                                 repetitions.cend(),
                                                 [](const ActionRepetition &lhs, const ActionRepetition &rhs) {
                                                     return lhs.score < rhs.score;
                                                 });
            const auto worstIt = std::min_element(repetitions.cbegin(),
                                                  repetitions.cend(),
                                                  [](const ActionRepetition &lhs, const ActionRepetition &rhs) {
                                                      return lhs.score < rhs.score;
                                                  });

            const ActionRepetition best = bestIt != repetitions.cend() ? *bestIt : ActionRepetition();
            const ActionRepetition worst = worstIt != repetitions.cend() ? *worstIt : ActionRepetition();
            const int bestIndex = bestIt != repetitions.cend()
                                      ? static_cast<int>(std::distance(repetitions.cbegin(), bestIt))
                                      : 0;
            const int worstIndex = worstIt != repetitions.cend()
                                       ? static_cast<int>(std::distance(repetitions.cbegin(), worstIt))
                                       : 0;
            auto *reviewLabel = new QLabel(QStringLiteral("代表动作：最好 #%1 %2-%3 · %4 分 · %5\n待纠正：最差 #%6 %7-%8 · %9 分 · %10")
                                               .arg(bestIndex + 1)
                                               .arg(formatMilliseconds(best.startedMs))
                                               .arg(formatMilliseconds(best.endedMs))
                                               .arg(best.score)
                                               .arg(best.feedback.trimmed().isEmpty() ? QStringLiteral("反馈为空") : best.feedback)
                                               .arg(worstIndex + 1)
                                               .arg(formatMilliseconds(worst.startedMs))
                                               .arg(formatMilliseconds(worst.endedMs))
                                               .arg(worst.score)
                                               .arg(worst.feedback.trimmed().isEmpty() ? issueSummary(worst.errorCodes) : worst.feedback),
                                           card);
            reviewLabel->setProperty("role", "reviewSummary");
            reviewLabel->setWordWrap(true);
            cardLayout->addWidget(reviewLabel);

            QStringList timelineLines;
            for (int repIndex = 0; repIndex < repetitions.size() && timelineLines.size() < 5; ++repIndex) {
                const ActionRepetition &repetition = repetitions.at(repIndex);
                if (repetition.valid && repetition.errorCodes.trimmed().isEmpty()) {
                    continue;
                }
                timelineLines.append(QStringLiteral("#%1 关键帧 %2 · %3 · %4")
                                         .arg(repIndex + 1)
                                         .arg(formatMilliseconds(repetition.keyFrameMs > 0 ? repetition.keyFrameMs : repetition.startedMs))
                                         .arg(issueSummary(repetition.errorCodes))
                                         .arg(repetition.feedback.trimmed().isEmpty()
                                                  ? qualityLabel(repetition.score, repetition.valid)
                                                  : repetition.feedback));
            }
            auto *timelineLabel = new QLabel(timelineLines.isEmpty()
                                                 ? QStringLiteral("关键错误时间轴：本次动作未触发明显错误点。")
                                                 : QStringLiteral("关键错误时间轴：\n%1").arg(timelineLines.join(QLatin1Char('\n'))),
                                             card);
            timelineLabel->setProperty("role", "muted");
            timelineLabel->setWordWrap(true);
            cardLayout->addWidget(timelineLabel);

            auto *detailTitle = new QLabel(QStringLiteral("动作明细"), card);
            detailTitle->setProperty("role", "sectionTitle");
            cardLayout->addWidget(detailTitle);

            const int detailCount = std::min(8, static_cast<int>(repetitions.size()));
            for (int repIndex = 0; repIndex < detailCount; ++repIndex) {
                const ActionRepetition repetition = repetitions.at(repIndex);
                auto *row = new QWidget(card);
                auto *rowLayout = new QHBoxLayout(row);
                rowLayout->setContentsMargins(0, 0, 0, 0);
                rowLayout->setSpacing(8);

                auto *detailLabel = new QLabel(repetitionReportLine(repetition,
                                                                    repIndex,
                                                                    [this](int ms) { return formatMilliseconds(ms); }),
                                               row);
                detailLabel->setWordWrap(true);
                detailLabel->setProperty("role", "muted");

                auto *clipButton = new QPushButton(QStringLiteral("定位片段"), row);
                clipButton->setProperty("role", "secondaryButton");
                clipButton->setEnabled(!record.videoSource.trimmed().isEmpty()
                                       || !record.videoFallbackSource.trimmed().isEmpty());
                connect(clipButton, &QPushButton::clicked, this, [this, record, repetition]() {
                    openSessionVideo(record, repetition.videoClipStartMs);
                });

                rowLayout->addWidget(detailLabel, 1);
                rowLayout->addWidget(clipButton, 0, Qt::AlignRight | Qt::AlignTop);
                cardLayout->addWidget(row);
            }
            if (repetitions.size() > detailCount) {
                auto *moreLabel = new QLabel(QStringLiteral("还有 %1 个动作实例，可通过导出报告查看完整明细。")
                                                 .arg(repetitions.size() - detailCount),
                                             card);
                moreLabel->setProperty("role", "muted");
                cardLayout->addWidget(moreLabel);
            }
        }

        auto *coachCommentLabel = new QLabel(QStringLiteral("教练批注：%1")
                                                 .arg(record.coachComment.trimmed().isEmpty()
                                                          ? QStringLiteral("未填写")
                                                          : record.coachComment.trimmed()),
                                             card);
        coachCommentLabel->setWordWrap(true);
        coachCommentLabel->setProperty("role", "muted");
        cardLayout->addWidget(coachCommentLabel);

        ui->historyListLayout->addWidget(card);
    }
}

// 刷新动作纠正建议列表。
void MainWindow::refreshSuggestions()
{
    if (!ui->suggestionListLayout) {
        return;
    }

    clearLayout(ui->suggestionListLayout);

    if (m_records.isEmpty()) {
        auto *emptyLabel = new QLabel(QStringLiteral("暂无可生成建议的训练记录。\n保存至少一条训练记录后，这里会自动给出动作纠正与下次训练建议。"),
                                      ui->suggestionPage);
        emptyLabel->setProperty("role", "emptyBox");
        emptyLabel->setAlignment(Qt::AlignCenter);
        emptyLabel->setWordWrap(true);
        ui->suggestionListLayout->addWidget(emptyLabel);
        return;
    }

    const SessionHistoryItem &latest = m_records.first();
    int totalScore = 0;
    for (const SessionHistoryItem &record : std::as_const(m_records)) {
        totalScore += record.score;
    }
    const int averageScore = (totalScore + m_records.size() / 2) / m_records.size();

    auto addSuggestionCard = [this](const QString &title, const QString &tag, const QString &body) {
        auto *card = new QFrame(ui->suggestionPage);
        setRole(card, "suggestionCard");

        auto *cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(14, 14, 14, 14);
        cardLayout->setSpacing(10);

        auto *headerLayout = new QHBoxLayout();
        headerLayout->setContentsMargins(0, 0, 0, 0);
        headerLayout->setSpacing(8);

        auto *titleLabel = new QLabel(title, card);
        titleLabel->setProperty("role", "sectionTitle");

        headerLayout->addWidget(titleLabel, 1);
        if (!tag.isEmpty()) {
            auto *tagLabel = new QLabel(tag, card);
            tagLabel->setProperty("role", "scoreTag");
            tagLabel->setAlignment(Qt::AlignCenter);
            headerLayout->addWidget(tagLabel, 0, Qt::AlignRight | Qt::AlignTop);
        }
        cardLayout->addLayout(headerLayout);

        auto *bodyLabel = new QLabel(body, card);
        bodyLabel->setWordWrap(true);
        cardLayout->addWidget(bodyLabel);

        ui->suggestionListLayout->addWidget(card);
    };

    QString summaryText;
    if (latest.score >= 90) {
        summaryText = QStringLiteral("最新训练中“%1”整体表现优秀，可以在保持稳定的前提下继续打磨动作细节。").arg(latest.actionName);
    } else if (latest.score >= 75) {
        summaryText = QStringLiteral("最新训练中“%1”整体较稳定，已经具备继续提分的基础，适合做针对性微调。").arg(latest.actionName);
    } else if (latest.score >= 60) {
        summaryText = QStringLiteral("最新训练中“%1”基础动作已建立，但还需要更有针对性的纠正训练来提升完成度。").arg(latest.actionName);
    } else {
        summaryText = QStringLiteral("最新训练中“%1”得分偏低，建议先放慢节奏，优先保证动作质量和姿态完整性。").arg(latest.actionName);
    }
    addSuggestionCard(QStringLiteral("综合结论"),
                      QStringLiteral("%1 分").arg(latest.score),
                      QStringLiteral("%1 运动员：%2。当前反馈：%3")
                          .arg(summaryText, latest.athleteName, latest.feedback));

    struct MetricAdvice {
        QString name;
        int value = 0;
        QString advice;
    };

    QVector<MetricAdvice> metrics = {
        {QStringLiteral("关键点"), latest.detectionScore,
         QStringLiteral("优先检查站位是否完整入镜，并保持机位稳定，确保关键点持续可见。")},
        {QStringLiteral("对称"), latest.symmetryScore,
         QStringLiteral("加强左右发力和肢体展开的一致性，减少单侧代偿和发力不均。")},
        {QStringLiteral("重心"), latest.balanceScore,
         QStringLiteral("训练时注意核心收紧与落刃重心控制，减少重心前后漂移。")},
        {QStringLiteral("稳定"), latest.stabilityScore,
         QStringLiteral("先放慢节奏，减少多余摆动，稳定住主干后再逐步提速。")},
        {QStringLiteral("3D"), latest.depthScore,
         QStringLiteral("调整相机角度并保持身体朝向清晰，提升立体姿态的可辨识度。")}
    };

    MetricAdvice weakestMetric = metrics.first();
    for (const MetricAdvice &metric : std::as_const(metrics)) {
        if (metric.value < weakestMetric.value) {
            weakestMetric = metric;
        }
    }
    addSuggestionCard(QStringLiteral("重点纠正"),
                      QStringLiteral("%1 %2 分").arg(weakestMetric.name).arg(weakestMetric.value),
                      QStringLiteral("问题部位：%1。触发原因：%2 分低于动作标准预期。建议动作：%3 优先级：P1。")
                          .arg(weakestMetric.name)
                          .arg(weakestMetric.name)
                          .arg(weakestMetric.advice));

    QString nextTrainingText;
    if (latest.duration < 300) {
        nextTrainingText = QStringLiteral("本次有效训练时长为 %1，建议下次先把单次有效训练稳定提升到 5 分钟以上，再观察动作质量变化。")
                               .arg(formatTime(latest.duration));
    } else if (latest.targetReps > 0 && latest.validReps < latest.targetReps) {
        nextTrainingText = QStringLiteral("本次有效动作 %1/%2，建议下次先把同一动作做到足量达标，再增加节奏或难度。")
                               .arg(latest.validReps)
                               .arg(latest.targetReps);
    } else if (averageScore - latest.score >= 8) {
        nextTrainingText = QStringLiteral("最新得分比历史均分低 %1 分，建议下次放慢节奏，优先做纠正训练，再逐步恢复速度。")
                               .arg(averageScore - latest.score);
    } else {
        nextTrainingText = QStringLiteral("当前训练节奏较稳，建议保持现有强度，继续围绕“%1”细化动作，冲击更高分。")
                               .arg(weakestMetric.name);
    }
    addSuggestionCard(QStringLiteral("下次训练建议"),
                      QStringLiteral("均分 %1").arg(averageScore),
                      nextTrainingText);
}

void MainWindow::openSessionVideo(const SessionHistoryItem &record, int offsetMs)
{
    const QString source = !record.videoSource.trimmed().isEmpty()
                               ? record.videoSource.trimmed()
                               : record.videoFallbackSource.trimmed();
    if (source.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("暂无视频引用"), QStringLiteral("这条训练记录没有保存可回看的视频引用。"));
        return;
    }

    ui->mainImageLabel->setPlaceholderText(record.videoCameraName.trimmed().isEmpty()
                                               ? QStringLiteral("训练回看")
                                               : record.videoCameraName);
    ui->mainImageLabel->playMainUrlWithFallback(source, record.videoFallbackSource);
    if (m_handAnalysisManager) {
        m_handAnalysisManager->setPaused(true);
        m_handAnalysisManager->setActiveStream(0, {});
    }
    switchPage(kCapturePage);

    const QString offsetTip = offsetMs > 0
                                  ? QStringLiteral("。片段起点 %1，请在播放器中按该时间回看。").arg(formatMilliseconds(offsetMs))
                                  : QString();
    ui->saveTipLabel->setText(QStringLiteral("正在回看：%1%2").arg(displayMediaSource(source), offsetTip));
    ui->saveTipLabel->show();
}

void MainWindow::editCoachComment(const QString &sessionId)
{
    if (!m_trainingRepository || !m_trainingRepository->isOpen()) {
        QMessageBox::warning(this, QStringLiteral("数据库未就绪"), QStringLiteral("训练数据库未就绪，无法保存教练批注。"));
        return;
    }

    SessionHistoryItem record;
    for (const SessionHistoryItem &item : std::as_const(m_records)) {
        if (item.id == sessionId) {
            record = item;
            break;
        }
    }
    if (record.id.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("记录不存在"), QStringLiteral("未找到对应训练记录。"));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("教练批注"));
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);

    auto *titleLabel = new QLabel(QStringLiteral("%1 · %2 · %3")
                                      .arg(record.time, record.athleteName, record.actionName),
                                  &dialog);
    titleLabel->setWordWrap(true);
    layout->addWidget(titleLabel);

    auto *editor = new QPlainTextEdit(&dialog);
    editor->setPlainText(record.coachComment);
    editor->setPlaceholderText(QStringLiteral("记录教练观察、人工纠正、下次训练重点"));
    editor->setMinimumSize(520, 180);
    layout->addWidget(editor);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Save)->setText(QStringLiteral("保存"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    QString errorMessage;
    if (!m_trainingRepository->saveCoachComment(sessionId, editor->toPlainText(), &errorMessage)) {
        QMessageBox::warning(this, QStringLiteral("保存失败"), errorMessage);
        return;
    }

    loadTrainingRecords();
    refreshHistory();
    refreshSuggestions();
}

void MainWindow::exportTrainingReport(const QString &sessionId)
{
    SessionHistoryItem record;
    for (const SessionHistoryItem &item : std::as_const(m_records)) {
        if (item.id == sessionId) {
            record = item;
            break;
        }
    }
    if (record.id.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("记录不存在"), QStringLiteral("未找到对应训练记录。"));
        return;
    }

    const QString defaultFileName = QStringLiteral("iSkating-%1-%2.md")
                                        .arg(record.athleteName)
                                        .arg(record.time.left(10));
    const QString filePath = QFileDialog::getSaveFileName(this,
                                                          QStringLiteral("导出训练报告"),
                                                          QDir::home().absoluteFilePath(defaultFileName),
                                                          QStringLiteral("Markdown (*.md);;Text (*.txt)"));
    if (filePath.trimmed().isEmpty()) {
        return;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, QStringLiteral("导出失败"), file.errorString());
        return;
    }

    const QVector<ActionRepetition> repetitions = m_trainingRepository && m_trainingRepository->isOpen()
                                                      ? m_trainingRepository->repetitionsForSession(record.id)
                                                      : QVector<ActionRepetition>();
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << "# iSkating 训练复盘报告\n\n";
    out << "- 时间：" << record.time << "\n";
    out << "- 运动员：" << record.athleteName << "\n";
    out << "- 教练：" << (record.coachName.isEmpty() ? QStringLiteral("未指定") : record.coachName) << "\n";
    out << "- 动作：" << record.actionCategory << " / " << record.actionName << " v" << record.standardVersion << "\n";
    out << "- 场地/阶段/目标：" << record.site << " / " << record.trainingPhase << " / " << record.goal << "\n";
    out << "- 时长：" << formatTime(record.duration) << "\n";
    out << "- 完成度：" << record.validReps << "/" << record.totalReps << "，目标 "
        << record.targetReps << " 次/" << record.targetScore << " 分\n";
    out << "- 平均/最佳分：" << record.score << "/" << record.bestScore << "\n";
    out << "- 视频：" << displayMediaSource(record.videoSource) << "\n\n";
    out << "## 综合反馈\n\n" << record.feedback << "\n\n";
    out << "## 教练批注\n\n"
        << (record.coachComment.trimmed().isEmpty() ? QStringLiteral("未填写") : record.coachComment.trimmed())
        << "\n\n";
    out << "## 动作明细\n\n";
    if (repetitions.isEmpty()) {
        out << "本次未保存动作实例。\n";
    } else {
        for (int i = 0; i < repetitions.size(); ++i) {
            out << "- " << repetitionReportLine(repetitions.at(i),
                                                i,
                                                [this](int ms) { return formatMilliseconds(ms); })
                << "\n";
        }
    }

    ui->saveTipLabel->setText(QStringLiteral("训练报告已导出：%1").arg(filePath));
    ui->saveTipLabel->show();
}

// 根据侧栏显示状态刷新顶部侧栏按钮：默认仅显示灰色图标，悬停时显示文字并变为蓝色。
void MainWindow::refreshSidebarButton()
{
    const bool hovered = ui->toggleSidebarButton->underMouse();
    const QString label = m_sidebarVisible ? QStringLiteral("隐藏侧栏") : QStringLiteral("显示侧栏");
    ui->toggleSidebarButton->setText(hovered ? label : QString());
    ui->toggleSidebarButton->setIcon(makeNormalizedTintedSvgIcon(QStringLiteral(":/icons/sidebar.svg"),
                                                                 QColor(QString::fromLatin1(hovered ? kHoverActionColor : kMutedInactiveColor)),
                                                                 22,
                                                                 18));
    ui->toggleSidebarButton->setIconSize(QSize(22, 22));
    ui->toggleSidebarButton->setMinimumWidth(40);
    ui->toggleSidebarButton->setToolTip(label);
    ui->toggleSidebarButton->setStatusTip(label);
    ui->toggleSidebarButton->setAccessibleName(label);
}

// 根据当前窗口状态刷新顶部全屏按钮：默认仅显示灰色图标，悬停时显示文字并变为蓝色。
void MainWindow::refreshFullScreenButton()
{
    if (!m_fullScreenButton) {
        return;
    }

    const bool hovered = m_fullScreenButton->underMouse();
    const bool fullScreen = isFullScreen();
    const QString label = fullScreen ? QStringLiteral("退出全屏") : QStringLiteral("F11全屏");
    const QString iconPath = fullScreen
                                 ? QStringLiteral(":/icons/exit_fullscreen.svg")
                                 : QStringLiteral(":/icons/fullscreen.svg");

    m_fullScreenButton->setText(hovered ? label : QString());
    m_fullScreenButton->setIcon(makeNormalizedTintedSvgIcon(iconPath,
                                                            QColor(QString::fromLatin1(hovered ? kHoverActionColor : kMutedInactiveColor)),
                                                            22,
                                                            18));
    m_fullScreenButton->setIconSize(QSize(22, 22));
    m_fullScreenButton->setMinimumWidth(40);
    m_fullScreenButton->setToolTip(fullScreen
                                       ? QStringLiteral("退出全屏显示（Esc）")
                                       : QStringLiteral("进入全屏显示（F11）"));
    m_fullScreenButton->setStatusTip(m_fullScreenButton->toolTip());
    m_fullScreenButton->setAccessibleName(label);
}

void MainWindow::refreshModelStatus(const QString &statusText)
{
    if (!ui || !ui->modelStatusLabel) {
        return;
    }
    ui->modelStatusLabel->setTextFormat(Qt::RichText);
    ui->modelStatusLabel->setText(topbarModuleHtml(QStringLiteral("AI"),
                                                   QStringLiteral("模型状态 Status"),
                                                   statusText));
}

void MainWindow::clearRealtimePose()
{
    ui->mainImageLabel->setPoseFrame({});
    if (m_skeletonView) {
        m_skeletonView->clearPoseFrame();
    }
    if (m_trajectoryWidget) {
        m_trajectoryWidget->clearPoseFrame();
    }
    m_realtimeScore = 0;
    m_detectionScore = 0;
    m_symmetryScore = 0;
    m_balanceScore = 0;
    m_stabilityScore = 0;
    m_depthScore = 0;
    m_feedbackText = QStringLiteral("等待姿态");
    refreshStats();
}

// 重新应用指定控件的 QSS，用于动态属性变化后立即刷新外观。
void MainWindow::repolish(QWidget *widget) const
{
    if (!widget || !widget->style()) {
        return;
    }

    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

// 将整数补齐为两位文本，常用于摄像头编号或时间格式。
QString MainWindow::pad(int num) const
{
    return QStringLiteral("%1").arg(num, 2, 10, QLatin1Char('0'));
}

// 将秒数格式化为 mm:ss，显示在训练时长统计中。
QString MainWindow::formatTime(int seconds) const
{
    return QStringLiteral("%1:%2")
        .arg(seconds / 60, 2, 10, QLatin1Char('0'))
        .arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

QString MainWindow::formatMilliseconds(int milliseconds) const
{
    const int clamped = std::max(0, milliseconds);
    const int totalSeconds = clamped / 1000;
    return QStringLiteral("%1:%2.%3")
        .arg(totalSeconds / 60, 2, 10, QLatin1Char('0'))
        .arg(totalSeconds % 60, 2, 10, QLatin1Char('0'))
        .arg((clamped % 1000) / 100, 1, 10, QLatin1Char('0'));
}


// 将模型精度配置值转换成界面上显示的中文名称。
QString MainWindow::precisionLabel(const QString &value) const
{
    if (value == QLatin1String("high")) {
        return QStringLiteral("高精度");
    }
    if (value == QLatin1String("balanced")) {
        return QStringLiteral("均衡");
    }
    if (value == QLatin1String("fast")) {
        return QStringLiteral("高速");
    }
    return value;
}

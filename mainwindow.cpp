#include "mainwindow.h"
#include "iconutils.h"
#include "ui_mainwindow.h"
#include "videoopenglwidget.h"

#include <QAction>
#include <QDateTime>
#include <QFile>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeySequence>
#include <QLabel>
#include <QLayout>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSize>
#include <QShortcut>
#include <QStyle>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>

namespace {

constexpr int kCapturePage = 0;
constexpr int kHistoryPage = 1;
constexpr int kSuggestionPage = 2;
constexpr const char *kPreviousWindowStateProperty = "previousWindowStateBeforeFullScreen";

void clearLayout(QLayout *layout)
{
  
}
 
 

void setRole(QWidget *widget, const char *role)
{
    if (widget) {
        widget->setProperty("role", role);
    }
}

} // namespace
 
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
                           "<td style='color:#d7e4fb; font-size:13px; font-weight:500;'>%3</td>"
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
                    QStringLiteral("已就绪（v2.3.1）"));
    setTopbarModule(ui->storageStatusLabel,
                    QStringLiteral("DB"),
                    QStringLiteral("存储 Storage"),
                    QStringLiteral("1.82T/4.00TB"));

    ui->mainImageLabel->setPlaceholderText(QStringLiteral("主视频\n未播放"));
    ui->mainImageLabel->setOverlayControlsVisible(false);
    for (int i = 0; i < m_cameraButtons.size(); ++i) {
        auto *cameraWidget = m_cameraButtons.at(i);
        const QString cameraName = QStringLiteral("CAM %1").arg(i + 1, 2, 10, QLatin1Char('0'));
        cameraWidget->setChannelName(cameraName);
        cameraWidget->setPlaceholderText(cameraName);
        cameraWidget->setOverlayControlsVisible(true);
    }

    applyStyleSheet();

    auto exitFullScreen = [this]() {
        if (!isFullScreen()) {
            return;
        }

        const auto previousState = static_cast<Qt::WindowStates>(
            property(kPreviousWindowStateProperty).toInt());
        setWindowState(previousState & ~Qt::WindowFullScreen);
    };

    auto *fullScreenShortcut = new QShortcut(QKeySequence(Qt::Key_F11), this);
    fullScreenShortcut->setContext(Qt::WindowShortcut);
    connect(fullScreenShortcut, &QShortcut::activated, this, [this, exitFullScreen]() {
        if (isFullScreen()) {
            exitFullScreen();
            return;
        }

        setProperty(kPreviousWindowStateProperty,
                    static_cast<int>((windowState() & ~Qt::WindowFullScreen).toInt()));
        setWindowState(windowState() | Qt::WindowFullScreen);
    });

    auto *escapeShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    escapeShortcut->setContext(Qt::WindowShortcut);
    connect(escapeShortcut, &QShortcut::activated, this, exitFullScreen);

    auto makeButtonAction = [this](QPushButton *button, const QString &label, const QString &iconPath) {
        auto *action = new QAction(makeNormalizedTintedSvgIcon(iconPath,
                                                               QColor(QStringLiteral("#8a96a6")),
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
    connect(settingsAction, &QAction::triggered, this, [this]() {
        ui->settingsBox->setVisible(!ui->settingsBox->isVisible());
    });

    m_trajectoryViewNormalMinSize = ui->trajectoryViewFrame->minimumSize();
    m_trajectoryViewNormalMaxSize = ui->trajectoryViewFrame->maximumSize();
    m_trajectoryCardNormalMinSize = ui->trajectoryCard->minimumSize();
    m_trajectoryCardNormalMaxSize = ui->trajectoryCard->maximumSize();
    ui->metricsLayout->setAlignment(Qt::AlignTop);
    ui->metricsCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    if (auto *captureLayout = qobject_cast<QVBoxLayout *>(ui->capturePage->layout())) {
        captureLayout->setStretch(0, 1);
        captureLayout->setStretch(1, 0);
    }

    setupConnections();
    ui->legendFrame->setVisible(false);
    ui->collapseTrajectoryButton->setVisible(false);
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::setupUiState()
{
     
}

void MainWindow::setupConnections()
{
    connect(ui->expandTrajectoryButton, &QPushButton::clicked, this, [this]() {
        setTrajectoryExpanded(true);
    });
    connect(ui->collapseTrajectoryButton, &QPushButton::clicked, this, [this]() {
        setTrajectoryExpanded(false);
    });
}
 
void MainWindow::installStaticImages()
{
    ui->poseImageLabelA->setPixmap(QPixmap(QStringLiteral(":/public/pose-a.png")));
    ui->poseImageLabelB->setPixmap(QPixmap(QStringLiteral(":/public/pose-b.png")));
}

void MainWindow::applyStyleSheet()
{
    QFile qssFile(QStringLiteral(":/styles/iskating.qss"));
    if (qssFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        setStyleSheet(QString::fromUtf8(qssFile.readAll()));
    }
}

void MainWindow::switchPage(int pageIndex)
{ 
}

void MainWindow::toggleSidebar()
{
    
}

void MainWindow::selectCamera(int cameraId)
{
   
}

void MainWindow::setTrajectoryExpanded(bool expanded)
{
    if (m_trajectoryExpanded == expanded) {
        return;
    }

    m_trajectoryExpanded = expanded;
    ui->middleLayout->setSpacing(expanded ? 0 : 14);
    ui->middleLayout->setStretch(0, expanded ? 0 : 0);
    ui->middleLayout->setStretch(1, expanded ? 1 : 0);
    ui->cameraGridLayout->setSpacing(expanded ? 0 : 10);
    for (auto *cameraWidget : m_cameraButtons) {
        cameraWidget->setVisible(!expanded);
        cameraWidget->updateGeometry();
    }
    ui->legendFrame->setVisible(expanded);
    ui->expandTrajectoryButton->setVisible(!expanded);
    ui->collapseTrajectoryButton->setVisible(expanded);

    if (expanded) {
        ui->trajectoryCard->setMinimumSize(m_trajectoryCardNormalMinSize);
        ui->trajectoryCard->setMaximumSize(m_trajectoryCardNormalMaxSize);
        ui->trajectoryCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        ui->trajectoryViewFrame->setMinimumSize(m_trajectoryViewNormalMinSize);
        ui->trajectoryViewFrame->setMaximumSize(m_trajectoryViewNormalMaxSize);
        ui->trajectoryViewFrame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    } else {
        ui->trajectoryCard->setMinimumSize(m_trajectoryCardNormalMinSize);
        ui->trajectoryCard->setMaximumSize(m_trajectoryCardNormalMaxSize);
        ui->trajectoryCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        ui->trajectoryViewFrame->setMinimumSize(m_trajectoryViewNormalMinSize);
        ui->trajectoryViewFrame->setMaximumSize(m_trajectoryViewNormalMaxSize);
        ui->trajectoryViewFrame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    }

    ui->trajectoryCard->updateGeometry();
    ui->trajectoryViewFrame->updateGeometry();
    ui->middleLayout->invalidate();
    if (ui->capturePage->layout()) {
        ui->capturePage->layout()->invalidate();
        ui->capturePage->layout()->activate();
    }
}

void MainWindow::startCapture()
{
    ui->mainImageLabel->setPlaying(true);
    for (auto *videoWidget : m_cameraButtons) {
        videoWidget->setPlaying(true);
    }
}

void MainWindow::pauseCapture()
{
    ui->mainImageLabel->setPlaying(false);
    for (auto *videoWidget : m_cameraButtons) {
        videoWidget->setPlaying(false);
    }
}

void MainWindow::stopCapture()
{
    ui->mainImageLabel->setPlaying(false);
    for (auto *videoWidget : m_cameraButtons) {
        videoWidget->setPlaying(false);
    }
}

void MainWindow::saveRecord()
{
    
}

void MainWindow::tick()
{
    
}

void MainWindow::refreshNavButtons()
{
   
}

void MainWindow::refreshCameraButtons()
{
   
}

void MainWindow::refreshStats()
{
  
}

void MainWindow::refreshHistory()
{
     
}

void MainWindow::refreshSuggestions()
{
    
}

void MainWindow::repolish(QWidget *widget) const
{ 
}

QString MainWindow::pad(int num) const
{
    return QStringLiteral("%1").arg(num, 2, 10, QLatin1Char('0'));
}

QString MainWindow::formatTime(int seconds) const
{
    return QStringLiteral("%1:%2")
        .arg(seconds / 60, 2, 10, QLatin1Char('0'))
        .arg(seconds % 60, 2, 10, QLatin1Char('0'));
}


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

#include "mainwindow.h"
#include "iconutils.h"
#include "ui_mainwindow.h"
#include "videoopenglwidget.h"

#include <QAction>
#include <QDateTime>
#include <QEvent>
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
#include <QDebug>
#include <QRandomGenerator>
#include <QSize>
#include <QSizePolicy>
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
constexpr const char *kMutedInactiveColor = "#8c8c8c";
constexpr const char *kHoverActionColor = "#3b8dff";

// 清空布局中的子项；保留该工具函数供动态重建列表类界面时复用。
void clearLayout(QLayout *layout)
{
  
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
                    QStringLiteral("已就绪（v2.3.1）"));
    setTopbarModule(ui->storageStatusLabel,
                    QStringLiteral("DB"),
                    QStringLiteral("存储 Storage"),
                    QStringLiteral("1.82T/4.00TB"));

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
    for (int i = 0; i < m_cameraButtons.size(); ++i) {
        auto *cameraWidget = m_cameraButtons.at(i);
        const QString cameraName = QStringLiteral("CAM %1").arg(i + 1, 2, 10, QLatin1Char('0'));
        cameraWidget->setChannelName(cameraName);
        cameraWidget->setPlaceholderText(cameraName);
        cameraWidget->setOverlayControlsVisible(true);
        cameraWidget->setDoubleClickHandler([this](VideoOpenGLWidget *sourceWidget) {
            qDebug() << "[MainWindow] camera double clicked, play in main view"
                     << sourceWidget->channelName()
                     << sourceWidget->currentVideoPath();
            ui->mainImageLabel->playFile(sourceWidget->currentVideoPath());
        });
    }

    applyStyleSheet();

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

// 释放由 Qt Designer 生成的界面对象。
MainWindow::~MainWindow()
{
    delete ui;
}

// 监听右上角图标按钮的悬停状态：进入时显示文字并点亮图标，离开时恢复纯图标。
bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == ui->toggleSidebarButton
        && (event->type() == QEvent::Enter || event->type() == QEvent::Leave)) {
        refreshSidebarButton();
    } else if (watched == m_fullScreenButton
               && (event->type() == QEvent::Enter || event->type() == QEvent::Leave)) {
        refreshFullScreenButton();
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
     
}

// 绑定页面上的交互信号：侧栏开关、全屏切换、三维轨迹展开/收起等。
void MainWindow::setupConnections()
{
    connect(ui->toggleSidebarButton, &QPushButton::clicked, this, [this]() {
        toggleSidebar();
    });
    if (m_fullScreenButton) {
        connect(m_fullScreenButton, &QPushButton::clicked, this, [this]() {
            toggleFullScreen();
        });
    }
    connect(ui->expandTrajectoryButton, &QPushButton::clicked, this, [this]() {
        setTrajectoryExpanded(true);
    });
    connect(ui->collapseTrajectoryButton, &QPushButton::clicked, this, [this]() {
        setTrajectoryExpanded(false);
    });
}

// 将资源文件中的静态示例图片贴到界面对应占位控件上。
void MainWindow::installStaticImages()
{
    ui->poseImageLabelA->setPixmap(QPixmap(QStringLiteral(":/public/pose-a.png")));
    ui->poseImageLabelB->setPixmap(QPixmap(QStringLiteral(":/public/pose-b.png")));
}

// 从资源系统读取 QSS，统一应用暗色仪表盘主题样式。
void MainWindow::applyStyleSheet()
{
    QFile qssFile(QStringLiteral(":/styles/iskating.qss"));
    if (qssFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        setStyleSheet(QString::fromUtf8(qssFile.readAll()));
    }
}

// 切换主页面堆栈；pageIndex 对应实时采集、历史分析和纠正建议等页面。
void MainWindow::switchPage(int pageIndex)
{ 
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
   
}

// 展开或收起三维轨迹区域：展开时隐藏 12 路视频网格，收起时恢复原布局尺寸。
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

// 开始采集：主视图和 12 路预览同时播放当前目录下的默认视频。
void MainWindow::startCapture()
{
    qDebug() << "[MainWindow] startCapture clicked";
    ui->mainImageLabel->playDefaultVideo();
    for (auto *videoWidget : m_cameraButtons) {
        videoWidget->playDefaultVideo();
    }
}

// 暂停采集：暂停主视图和全部摄像头预览的播放器状态。
void MainWindow::pauseCapture()
{
    qDebug() << "[MainWindow] pauseCapture clicked";
    ui->mainImageLabel->pausePlayback();
    for (auto *videoWidget : m_cameraButtons) {
        videoWidget->pausePlayback();
    }
}

// 停止采集：停止主视图和全部摄像头预览，并恢复未播放占位状态。
void MainWindow::stopCapture()
{
    qDebug() << "[MainWindow] stopCapture clicked";
    ui->mainImageLabel->stopPlayback();
    for (auto *videoWidget : m_cameraButtons) {
        videoWidget->stopPlayback();
    }
}

// 保存当前训练记录；后续可在此持久化训练时长、动作数量和模型评分。
void MainWindow::saveRecord()
{
    
}

// 训练计时器回调：用于累计训练时长、刷新统计数据和实时反馈。
void MainWindow::tick()
{
    
}

// 根据当前页面刷新左侧导航按钮的选中/普通状态。
void MainWindow::refreshNavButtons()
{
   
}

// 根据当前选择的摄像头刷新 12 路预览控件的选中状态。
void MainWindow::refreshCameraButtons()
{
   
}

// 刷新动作计数、训练时长、实时得分等统计卡片。
void MainWindow::refreshStats()
{
  
}

// 重新生成训练历史列表和概要统计卡片。
void MainWindow::refreshHistory()
{
     
}

// 刷新动作纠正建议列表。
void MainWindow::refreshSuggestions()
{
    
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

// 重新应用指定控件的 QSS，用于动态属性变化后立即刷新外观。
void MainWindow::repolish(QWidget *widget) const
{ 
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

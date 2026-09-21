#include "mainwindowchrome.h"

#include <QAbstractButton>
#include <QApplication>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QMainWindow>
#include <QMouseEvent>
#include <QPainter>
#include <QStyle>
#include <QVBoxLayout>
#include <QWindow>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#endif

namespace {

enum class ChromeGlyph { Minimize, Maximize, Restore, Close };

class ChromeButton final : public QAbstractButton
{
public:
    ChromeButton(ChromeGlyph glyph, QWidget *parent) : QAbstractButton(parent), m_glyph(glyph)
    {
        setFixedSize(46, 32);
        setFocusPolicy(Qt::NoFocus);
    }

    void setGlyph(ChromeGlyph glyph) { m_glyph = glyph; update(); }

protected:
    void enterEvent(QEnterEvent *event) override { update(); QAbstractButton::enterEvent(event); }
    void leaveEvent(QEvent *event) override { update(); QAbstractButton::leaveEvent(event); }
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        if (underMouse()) painter.fillRect(rect(), m_glyph == ChromeGlyph::Close ? QColor("#C42B1C") : QColor("#1C2A3A"));
        painter.setRenderHint(QPainter::Antialiasing, false);
        QPen pen(underMouse() && m_glyph == ChromeGlyph::Close ? Qt::white : QColor("#D7E0EA"));
        pen.setWidthF(1.0);
        painter.setPen(pen);
        const QPoint c = rect().center();
        if (m_glyph == ChromeGlyph::Minimize) {
            painter.drawLine(c.x() - 5, c.y() + 3, c.x() + 5, c.y() + 3);
        } else if (m_glyph == ChromeGlyph::Maximize) {
            painter.drawRect(QRect(c.x() - 5, c.y() - 5, 10, 10));
        } else if (m_glyph == ChromeGlyph::Restore) {
            painter.drawRect(QRect(c.x() - 3, c.y() - 5, 9, 9));
            painter.fillRect(QRect(c.x() - 6, c.y() - 2, 9, 9),
                             underMouse() ? QColor("#1C2A3A") : QColor("#0D131B"));
            painter.drawRect(QRect(c.x() - 6, c.y() - 2, 9, 9));
        } else {
            painter.drawLine(c.x() - 5, c.y() - 5, c.x() + 5, c.y() + 5);
            painter.drawLine(c.x() + 5, c.y() - 5, c.x() - 5, c.y() + 5);
        }
    }

private:
    ChromeGlyph m_glyph;
};

}

MainWindowChrome::MainWindowChrome(QMainWindow *window) : QObject(window), m_window(window) {}

void MainWindowChrome::install()
{
    QWidget *content = m_window->takeCentralWidget();
    m_frame = new QFrame(m_window);
    m_frame->setObjectName(QStringLiteral("mainWindowChromeFrame"));
    m_frame->setStyleSheet(QStringLiteral("#mainWindowChromeFrame { background: #31445A; }"));
    auto *shell = new QVBoxLayout(m_frame);
    shell->setContentsMargins(1, 1, 1, 1);
    shell->setSpacing(0);

    m_titleBar = new QWidget(m_frame);
    m_titleBar->setObjectName(QStringLiteral("mainWindowTitleBar"));
    m_titleBar->setFixedHeight(32);
    m_titleBar->setStyleSheet(QStringLiteral("#mainWindowTitleBar { background: #0D131B; border-bottom: 1px solid #243244; }"));
    auto *bar = new QHBoxLayout(m_titleBar);
    bar->setContentsMargins(0, 0, 0, 0);
    bar->setSpacing(8);
    bar->addStretch();

    m_controls = new QWidget(m_titleBar);
    auto *buttons = new QHBoxLayout(m_controls);
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->setSpacing(0);
    auto *minimize = new ChromeButton(ChromeGlyph::Minimize, m_controls);
    auto *maximize = new ChromeButton(ChromeGlyph::Maximize, m_controls);
    auto *close = new ChromeButton(ChromeGlyph::Close, m_controls);
    minimize->setAccessibleName(QStringLiteral("最小化")); minimize->setToolTip(QStringLiteral("最小化"));
    maximize->setAccessibleName(QStringLiteral("最大化或还原")); maximize->setToolTip(QStringLiteral("最大化或还原"));
    close->setAccessibleName(QStringLiteral("关闭")); close->setToolTip(QStringLiteral("关闭"));
    m_maximizeButton = maximize;
    buttons->addWidget(minimize); buttons->addWidget(maximize); buttons->addWidget(close);
    bar->addWidget(m_controls);
    connect(minimize, &QAbstractButton::clicked, m_window, &QWidget::showMinimized);
    connect(maximize, &QAbstractButton::clicked, this, [this] { m_window->isMaximized() ? m_window->showNormal() : m_window->showMaximized(); });
    connect(close, &QAbstractButton::clicked, m_window, &QWidget::close);

    shell->addWidget(m_titleBar);
    shell->addWidget(content, 1);
    m_window->setCentralWidget(m_frame);
    m_window->installEventFilter(this);
    m_titleBar->installEventFilter(this);
    syncWindowState();
}

void MainWindowChrome::setSidebarButton(QWidget *button)
{
    m_sidebarButton = button;
    static_cast<QHBoxLayout *>(m_titleBar->layout())->insertWidget(0, button);
}

void MainWindowChrome::syncWindowState()
{
    const bool fullScreen = m_window->isFullScreen();
    const bool maximized = m_window->isMaximized();
    m_titleBar->setVisible(!fullScreen);
    if (auto *button = static_cast<ChromeButton *>(m_maximizeButton))
        button->setGlyph(maximized ? ChromeGlyph::Restore : ChromeGlyph::Maximize);
    if (auto *layout = qobject_cast<QVBoxLayout *>(m_frame->layout()))
        layout->setContentsMargins((maximized || fullScreen) ? 0 : 1, (maximized || fullScreen) ? 0 : 1,
                                   (maximized || fullScreen) ? 0 : 1, (maximized || fullScreen) ? 0 : 1);
}

void MainWindowChrome::applyNativeStyle()
{
#ifdef Q_OS_WIN
    if (m_updatingNativeStyle || !m_window->windowHandle()) return;
    m_updatingNativeStyle = true;
    HWND hwnd = reinterpret_cast<HWND>(m_window->winId());
    LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
    const LONG_PTR required = WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
    if ((style & required) != required) {
        SetWindowLongPtr(hwnd, GWL_STYLE, style | required);
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
    m_updatingNativeStyle = false;
#endif
}

bool MainWindowChrome::pointIsInTitleBar(const QPoint &globalPoint) const
{
    if (!m_titleBar->isVisible() || !m_window->isEnabled()) return false;
    const QPoint local = m_titleBar->mapFromGlobal(globalPoint);
    return m_titleBar->rect().contains(local) && !m_controls->geometry().contains(local)
           && (!m_sidebarButton || !m_sidebarButton->geometry().contains(local));
}

bool MainWindowChrome::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_window && (event->type() == QEvent::WindowStateChange || event->type() == QEvent::Show))
        syncWindowState();
    if (watched == m_window && (event->type() == QEvent::Show || event->type() == QEvent::WinIdChange))
        applyNativeStyle();
#ifndef Q_OS_WIN
    if (watched == m_titleBar && event->type() == QEvent::MouseButtonDblClick) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::LeftButton && pointIsInTitleBar(mouse->globalPosition().toPoint())) {
            m_window->isMaximized() ? m_window->showNormal() : m_window->showMaximized();
            return true;
        }
    }
    if (watched == m_titleBar && event->type() == QEvent::MouseButtonPress) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::LeftButton && pointIsInTitleBar(mouse->globalPosition().toPoint())
            && !m_window->isMaximized() && m_window->windowHandle())
            return m_window->windowHandle()->startSystemMove();
    }
#endif
    return QObject::eventFilter(watched, event);
}

bool MainWindowChrome::handleNativeEvent(const QByteArray &, void *message, qintptr *result)
{
#ifdef Q_OS_WIN
    MSG *msg = static_cast<MSG *>(message);
    if (msg->message == WM_NCHITTEST) {
        const QPoint global(GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam));
        RECT wr{}; GetWindowRect(msg->hwnd, &wr);
        const int edge = qRound(6.0 * m_window->devicePixelRatioF());
        const bool chromeEnabled = m_window->isEnabled() && IsWindowEnabled(msg->hwnd);
        if (chromeEnabled && !m_window->isMaximized() && !m_window->isFullScreen()) {
            const bool left = global.x() < wr.left + edge, right = global.x() >= wr.right - edge;
            const bool top = global.y() < wr.top + edge, bottom = global.y() >= wr.bottom - edge;
            if (top && left) *result = HTTOPLEFT; else if (top && right) *result = HTTOPRIGHT;
            else if (bottom && left) *result = HTBOTTOMLEFT; else if (bottom && right) *result = HTBOTTOMRIGHT;
            else if (left) *result = HTLEFT; else if (right) *result = HTRIGHT;
            else if (top) *result = HTTOP; else if (bottom) *result = HTBOTTOM;
            else {
                POINT client{global.x(), global.y()};
                ScreenToClient(msg->hwnd, &client);
                const qreal dpr = m_window->devicePixelRatioF();
                const int logicalX = qRound(static_cast<qreal>(client.x) / dpr);
                const int logicalY = qRound(static_cast<qreal>(client.y) / dpr);
                const QPoint titlePoint = m_titleBar->mapFrom(m_window, QPoint(logicalX, logicalY));
                if (m_titleBar->rect().contains(titlePoint) && !m_controls->geometry().contains(titlePoint)
                    && (!m_sidebarButton || !m_sidebarButton->geometry().contains(titlePoint))) *result = HTCAPTION;
                else *result = HTCLIENT;
            }
            return true;
        }
        if (chromeEnabled && !m_window->isFullScreen()) {
            POINT client{global.x(), global.y()}; ScreenToClient(msg->hwnd, &client);
            const qreal dpr = m_window->devicePixelRatioF();
            const int logicalX = qRound(static_cast<qreal>(client.x) / dpr);
            const int logicalY = qRound(static_cast<qreal>(client.y) / dpr);
            const QPoint titlePoint = m_titleBar->mapFrom(m_window, QPoint(logicalX, logicalY));
            if (m_titleBar->rect().contains(titlePoint) && !m_controls->geometry().contains(titlePoint)
                && (!m_sidebarButton || !m_sidebarButton->geometry().contains(titlePoint))) { *result = HTCAPTION; return true; }
        }
        *result = HTCLIENT;
        return true;
    }
    if (msg->message == WM_NCCALCSIZE && msg->wParam) {
        auto *params = reinterpret_cast<NCCALCSIZE_PARAMS *>(msg->lParam);
        if (m_window->isMaximized() && !m_window->isFullScreen()) {
            HMONITOR monitor = MonitorFromWindow(msg->hwnd, MONITOR_DEFAULTTONEAREST);
            MONITORINFO info{sizeof(info)};
            if (GetMonitorInfo(monitor, &info)) params->rgrc[0] = info.rcWork;
        }
        *result = 0;
        return true;
    }
#else
    Q_UNUSED(message); Q_UNUSED(result);
#endif
    return false;
}

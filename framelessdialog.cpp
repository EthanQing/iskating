#include "framelessdialog.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QShowEvent>
#include <QVBoxLayout>

FramelessDialog::FramelessDialog(QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("framelessDialog"));
    setWindowFlags((windowFlags() | Qt::Dialog | Qt::FramelessWindowHint)
                   & ~Qt::WindowContextHelpButtonHint);
    setModal(true);

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(1, 1, 1, 1);
    rootLayout->setSpacing(0);

    m_titleBar = new QWidget(this);
    m_titleBar->setObjectName(QStringLiteral("dialogTitleBar"));
    auto *titleLayout = new QHBoxLayout(m_titleBar);
    titleLayout->setContentsMargins(14, 8, 8, 8);
    titleLayout->setSpacing(8);

    m_titleLabel = new QLabel(this);
    m_titleLabel->setObjectName(QStringLiteral("dialogTitleLabel"));
    titleLayout->addWidget(m_titleLabel, 1);

    m_closeButton = new QPushButton(QStringLiteral("×"), this);
    m_closeButton->setObjectName(QStringLiteral("dialogCloseButton"));
    m_closeButton->setFixedSize(28, 24);
    m_closeButton->setFocusPolicy(Qt::NoFocus);
    titleLayout->addWidget(m_closeButton, 0, Qt::AlignRight | Qt::AlignVCenter);
    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::reject);

    rootLayout->addWidget(m_titleBar);

    auto *contentWidget = new QWidget(this);
    contentWidget->setObjectName(QStringLiteral("dialogContent"));
    m_contentLayout = new QVBoxLayout(contentWidget);
    m_contentLayout->setContentsMargins(18, 18, 18, 18);
    m_contentLayout->setSpacing(14);
    rootLayout->addWidget(contentWidget);

    setStyleSheet(QStringLiteral(R"QSS(
QDialog#framelessDialog {
    background: #0f141d;
    border: 1px solid #2b3447;
    color: #e7edf7;
    font-family: "Microsoft YaHei", "PingFang SC", sans-serif;
}
QWidget#dialogTitleBar {
    background: #111d31;
    border-bottom: 1px solid #1d2737;
}
QLabel#dialogTitleLabel {
    color: #7fb6ff;
    font-size: 14px;
    font-weight: 700;
    background: transparent;
}
QWidget#dialogContent {
    background: #0f141d;
}
QDialog#framelessDialog QLabel {
    color: #8c8c8c;
    font-size: 13px;
    background: transparent;
}
QDialog#framelessDialog QLineEdit {
    min-height: 30px;
    border: 1px solid #2b3447;
    border-radius: 0;
    background: #101623;
    color: #e7edf7;
    padding: 6px 8px;
    selection-background-color: #15335c;
}
QDialog#framelessDialog QLineEdit:hover,
QDialog#framelessDialog QLineEdit:focus {
    border-color: #3b8dff;
    background: #111d31;
}
QDialog#framelessDialog QPushButton {
    min-width: 76px;
    min-height: 30px;
    border: none;
    border-radius: 0;
    background: #101623;
    color: #8c8c8c;
    padding: 6px 14px;
}
QDialog#framelessDialog QPushButton:hover {
    background: #172338;
    color: #e7edf7;
}
QDialog#framelessDialog QPushButton:default {
    background: #15335c;
    color: #ffffff;
}
QDialog#framelessDialog QPushButton:pressed {
    background: #0d1420;
}
QPushButton#dialogCloseButton {
    min-width: 28px;
    min-height: 24px;
    max-width: 28px;
    max-height: 24px;
    background: transparent;
    color: #8c8c8c;
    font-size: 18px;
    padding: 0;
}
QPushButton#dialogCloseButton:hover {
    background: #7a1f2b;
    color: #ffffff;
}
)QSS"));
}

QVBoxLayout *FramelessDialog::contentLayout() const
{
    return m_contentLayout;
}

void FramelessDialog::setDialogTitle(const QString &title)
{
    setWindowTitle(title);
    if (m_titleLabel) {
        m_titleLabel->setText(title);
    }
}

void FramelessDialog::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton
        && m_titleBar
        && m_titleBar->geometry().contains(event->position().toPoint())) {
        m_dragging = true;
        m_dragPosition = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
        return;
    }

    QDialog::mousePressEvent(event);
}

void FramelessDialog::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging && (event->buttons() & Qt::LeftButton)) {
        move(event->globalPosition().toPoint() - m_dragPosition);
        event->accept();
        return;
    }

    QDialog::mouseMoveEvent(event);
}

void FramelessDialog::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = false;
    }

    QDialog::mouseReleaseEvent(event);
}

void FramelessDialog::showEvent(QShowEvent *event)
{
    if (m_titleLabel && m_titleLabel->text().isEmpty()) {
        m_titleLabel->setText(windowTitle());
    }
    QDialog::showEvent(event);
}

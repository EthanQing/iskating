#include "framelessdialog.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QShowEvent>
#include <QVBoxLayout>

namespace {

class ModalScrim final : public QWidget
{
public:
    explicit ModalScrim(QWidget *owner)
        : QWidget(owner, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus
                             | Qt::WindowTransparentForInput)
    {
        setObjectName(QStringLiteral("modalScrim"));
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAttribute(Qt::WA_TranslucentBackground);
        setFocusPolicy(Qt::NoFocus);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), QColor(0, 0, 0, 105));
    }
};

class ModalScrimController final : public QObject
{
public:
    explicit ModalScrimController(QDialog *dialog)
        : QObject(dialog)
        , m_dialog(dialog)
    {
        dialog->installEventFilter(this);
    }

    ~ModalScrimController() override
    {
        hideScrim();
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == m_dialog) {
            if (event->type() == QEvent::Show && m_dialog->isModal()) {
                showScrim();
            } else if (event->type() == QEvent::Hide) {
                hideScrim();
            }
        } else if (watched == m_owner) {
            switch (event->type()) {
            case QEvent::Move:
            case QEvent::Resize:
            case QEvent::WindowStateChange:
                updateGeometry();
                break;
            case QEvent::Show:
                if (m_scrim && m_dialog && m_dialog->isVisible()) {
                    updateGeometry();
                    m_scrim->show();
                    m_dialog->raise();
                }
                break;
            case QEvent::Hide:
                if (m_scrim) {
                    m_scrim->hide();
                }
                break;
            default:
                break;
            }
        }
        return QObject::eventFilter(watched, event);
    }

private:
    void showScrim()
    {
        QWidget *owner = m_dialog && m_dialog->parentWidget()
                             ? m_dialog->parentWidget()->window()
                             : nullptr;
        if (!owner || owner == m_dialog) {
            return;
        }
        if (m_owner != owner) {
            hideScrim();
            m_owner = owner;
            m_owner->installEventFilter(this);
        }
        if (!m_scrim) {
            m_scrim = new ModalScrim(owner);
        }
        updateGeometry();
        m_scrim->show();
        m_scrim->raise();
        m_dialog->raise();
    }

    void hideScrim()
    {
        if (m_owner) {
            m_owner->removeEventFilter(this);
        }
        if (m_scrim) {
            m_scrim->hide();
            m_scrim->deleteLater();
        }
        m_scrim.clear();
        m_owner.clear();
    }

    void updateGeometry()
    {
        if (m_owner && m_scrim) {
            m_scrim->setGeometry(m_owner->frameGeometry());
        }
    }

    QPointer<QDialog> m_dialog;
    QPointer<QWidget> m_owner;
    QPointer<QWidget> m_scrim;
};

} // namespace

void installModalScrim(QDialog *dialog)
{
    if (dialog && !dialog->property("modalScrimInstalled").toBool()) {
        dialog->setProperty("modalScrimInstalled", true);
        new ModalScrimController(dialog);
    }
}

FramelessDialog::FramelessDialog(QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("framelessDialog"));
    setWindowFlags((windowFlags() | Qt::Dialog | Qt::FramelessWindowHint)
                   & ~Qt::WindowContextHelpButtonHint);
    setModal(true);
    installModalScrim(this);

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

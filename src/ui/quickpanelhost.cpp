#include "quickpanelhost.h"
#include "mainwindowpresentation.h"
#include <QCoreApplication>
#include <QDebug>
#include <QLabel>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWidget>
#include <QVBoxLayout>

QuickPanelHost::QuickPanelHost(MainWindowPresentation *presentation, const QUrl &source, QWidget *parent)
    : QWidget(parent), m_view(new QQuickWidget(this))
{
    setObjectName(QStringLiteral("quickPanelHost"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_view->setResizeMode(QQuickWidget::SizeRootObjectToView);
    m_view->setClearColor(QColor(QStringLiteral("#151B25")));
    m_view->setFocusPolicy(Qt::StrongFocus);
    m_view->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    // Qt 6.7 uses a context property; imports resolve beside the executable.
    m_view->engine()->addImportPath(QCoreApplication::applicationDirPath() + QStringLiteral("/qml"));
    m_view->rootContext()->setContextProperty(QStringLiteral("windowPresentation"), presentation);
    layout->addWidget(m_view);
    auto *errorLabel = new QLabel(QStringLiteral("界面加载失败，请查看应用日志。"), this);
    errorLabel->setWordWrap(true);
    errorLabel->setAccessibleName(QStringLiteral("QML 界面加载失败"));
    errorLabel->hide();
    layout->addWidget(errorLabel);
    connect(m_view, &QQuickWidget::statusChanged, this, [this, errorLabel](QQuickWidget::Status status) {
        if (status == QQuickWidget::Error) {
            for (const auto &error : m_view->errors()) qWarning().noquote() << error.toString();
            m_view->hide();
            errorLabel->show();
        }
    });
    m_view->setSource(source);
}

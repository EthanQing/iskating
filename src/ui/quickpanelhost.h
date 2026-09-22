#ifndef QUICKPANELHOST_H
#define QUICKPANELHOST_H
#include <QWidget>
#include <QUrl>
class MainWindowPresentation;
class QQuickWidget;
class QuickPanelHost : public QWidget
{
    Q_OBJECT
public:
    explicit QuickPanelHost(MainWindowPresentation *presentation, const QUrl &source, QWidget *parent = nullptr);
    QQuickWidget *view() const { return m_view; }
private:
    QQuickWidget *m_view;
};
#endif

#ifndef MAINWINDOWCHROME_H
#define MAINWINDOWCHROME_H

#include <QObject>
#include <QByteArray>
#include <QPoint>

class QMainWindow;
class QFrame;
class QWidget;

class MainWindowChrome : public QObject
{
    Q_OBJECT

public:
    explicit MainWindowChrome(QMainWindow *window);
    void install();
    bool handleNativeEvent(const QByteArray &eventType, void *message, qintptr *result);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void syncWindowState();
    void applyNativeStyle();
    bool pointIsInTitleBar(const QPoint &globalPoint) const;

    QMainWindow *m_window = nullptr;
    QFrame *m_frame = nullptr;
    QWidget *m_titleBar = nullptr;
    QWidget *m_controls = nullptr;
    QWidget *m_maximizeButton = nullptr;
    bool m_updatingNativeStyle = false;
};

#endif

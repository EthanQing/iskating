#ifndef FRAMELESSDIALOG_H
#define FRAMELESSDIALOG_H

#include <QDialog>
#include <QPoint>

class QLabel;
class QMouseEvent;
class QPushButton;
class QShowEvent;
class QVBoxLayout;

class FramelessDialog : public QDialog
{
public:
    explicit FramelessDialog(QWidget *parent = nullptr);

    QVBoxLayout *contentLayout() const;
    void setDialogTitle(const QString &title);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    QWidget *m_titleBar = nullptr;
    QLabel *m_titleLabel = nullptr;
    QPushButton *m_closeButton = nullptr;
    QVBoxLayout *m_contentLayout = nullptr;
    bool m_dragging = false;
    QPoint m_dragPosition;
};

#endif // FRAMELESSDIALOG_H

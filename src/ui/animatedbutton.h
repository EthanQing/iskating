#ifndef ANIMATEDBUTTON_H
#define ANIMATEDBUTTON_H

#include <QPushButton>

class QEnterEvent;
class QVariantAnimation;

class AnimatedButton : public QPushButton
{
    Q_OBJECT
    Q_PROPERTY(qreal textOpacity READ textOpacity WRITE setTextOpacity)

public:
    explicit AnimatedButton(QWidget *parent = nullptr);

    void setIconSource(const QString &source);
    qreal textOpacity() const;
    void setTextOpacity(qreal opacity);

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    void animateHover(qreal target, int duration);
    void animatePressed(qreal target, int duration);
    QString m_iconSource;
    qreal m_hoverProgress = 0.0;
    qreal m_pressProgress = 0.0;
    qreal m_textOpacity = 1.0;
    QVariantAnimation *m_hoverAnimation = nullptr;
    QVariantAnimation *m_pressAnimation = nullptr;
};

#endif // ANIMATEDBUTTON_H

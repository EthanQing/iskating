#ifndef VIDEOOPENGLWIDGET_H
#define VIDEOOPENGLWIDGET_H

#include <QOpenGLWidget>
#include <QSvgRenderer>
#include <QString>

class VideoOpenGLWidget : public QOpenGLWidget
{
public:
    explicit VideoOpenGLWidget(QWidget *parent = nullptr);

    bool isPlaying() const;
    void setPlaying(bool playing);
    QString placeholderText() const;
    void setPlaceholderText(const QString &text);

protected:
    void paintGL() override;

private:
    bool m_playing = false;
    QString m_placeholderText;
    QSvgRenderer m_placeholderRenderer;
};

#endif // VIDEOOPENGLWIDGET_H

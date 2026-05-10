#ifndef VIDEOOPENGLWIDGET_H
#define VIDEOOPENGLWIDGET_H

#include <QOpenGLWidget>
#include <QSvgRenderer>
#include <QString>

class QToolButton;

class VideoOpenGLWidget : public QOpenGLWidget
{
public:
    explicit VideoOpenGLWidget(QWidget *parent = nullptr);

    bool isPlaying() const;
    void setPlaying(bool playing);
    QString placeholderText() const;
    void setPlaceholderText(const QString &text);
    void setOverlayControlsVisible(bool visible);
    bool overlayControlsVisible() const;
    void setChannelName(const QString &name);
    QString channelName() const;

protected:
    void paintGL() override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void setupOverlayControls();
    void layoutOverlayControls();
    void openConfigDialog();

    bool m_playing = false;
    bool m_overlayControlsVisible = false;
    QString m_placeholderText;
    QString m_channelName;
    QString m_streamIp = QStringLiteral("192.168.1.100");
    QString m_streamPort = QStringLiteral("554");
    QString m_streamPath = QStringLiteral("/stream");
    QSvgRenderer m_placeholderRenderer;
    QToolButton *m_playButton = nullptr;
    QToolButton *m_stopButton = nullptr;
    QToolButton *m_configButton = nullptr;
};

#endif // VIDEOOPENGLWIDGET_H

#ifndef VIDEOOPENGLWIDGET_H
#define VIDEOOPENGLWIDGET_H

#include <QOpenGLWidget>
#include <QSvgRenderer>
#include <QString>

#include <functional>

class QMediaPlayer;
class QMouseEvent;
class QToolButton;
class QVideoSink;

class VideoOpenGLWidget : public QOpenGLWidget
{
public:
    explicit VideoOpenGLWidget(QWidget *parent = nullptr);

    static QString defaultVideoPath();

    bool isPlaying() const;
    void setPlaying(bool playing);
    void playDefaultVideo();
    void playFile(const QString &filePath);
    void pausePlayback();
    void stopPlayback();
    QString currentVideoPath() const;

    QString placeholderText() const;
    void setPlaceholderText(const QString &text);
    void setOverlayControlsVisible(bool visible);
    bool overlayControlsVisible() const;
    void setChannelName(const QString &name);
    QString channelName() const;
    void setDoubleClickHandler(std::function<void(VideoOpenGLWidget *)> handler);

protected:
    void paintGL() override;
    void resizeEvent(QResizeEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

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
    QString m_videoPath;
    QImage m_currentFrame;
    QSvgRenderer m_placeholderRenderer;
    QMediaPlayer *m_mediaPlayer = nullptr;
    QVideoSink *m_videoSink = nullptr;
    QToolButton *m_playButton = nullptr;
    QToolButton *m_pauseButton = nullptr;
    QToolButton *m_stopButton = nullptr;
    QToolButton *m_configButton = nullptr;
    std::function<void(VideoOpenGLWidget *)> m_doubleClickHandler;
};

#endif // VIDEOOPENGLWIDGET_H

#ifndef VIDEOOPENGLWIDGET_H
#define VIDEOOPENGLWIDGET_H

#include "poseresult.h"

#include <QSvgRenderer>
#include <QString>
#include <QWidget>

#include <functional>
#include <memory>

class QEnterEvent;
class QEvent;
class QMouseEvent;
class QPaintEvent;
class QToolButton;
class QTimer;
class D3DVideoSurface;
class RtspStream;

class VideoOpenGLWidget : public QWidget
{
public:
    explicit VideoOpenGLWidget(QWidget *parent = nullptr);

    static QString defaultVideoPath();

    bool isPlaying() const;
    void setPlaying(bool playing);
    void setStillImage(const QString &imagePath);
    void playDefaultVideo();
    void playFile(const QString &filePath);
    void pausePlayback();
    void stopPlayback();
    QString currentVideoPath() const;

    QString placeholderText() const;
    void setPlaceholderText(const QString &text);
    void setPlaceholderIconVisible(bool visible);
    void setOverlayControlsVisible(bool visible);
    bool overlayControlsVisible() const;
    void setConfigButtonVisible(bool visible);
    void setChannelName(const QString &name);
    QString channelName() const;
    QString streamIp() const;
    QString streamPort() const;
    QString streamPath() const;
    QString streamUrl() const;
    QString previewUrl() const;
    QString mainUrl() const;
    void setStreamConfig(const QString &ip, const QString &port, const QString &path);
    void setStreamUrls(const QString &previewUrl, const QString &mainUrl);
    void playMainUrl();
    void playMainUrlWithFallback(const QString &mainUrl, const QString &fallbackUrl);
    std::shared_ptr<RtspStream> activeStream() const;
    void setPoseFrame(const PoseFrameResult &frame);
    void setDoubleClickHandler(std::function<void(VideoOpenGLWidget *)> handler);
    void setConfigChangedHandler(std::function<void(VideoOpenGLWidget *)> handler);
    void setStreamChangedHandler(std::function<void(VideoOpenGLWidget *)> handler);

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    void setupOverlayControls();
    void layoutOverlayControls();
    void openConfigDialog();
    void attachStream(const QString &source);
    void attachStream(const QString &source, const QString &fallbackSource);
    void notifyStreamChanged();
    void refreshVideoFrame();

    bool m_playing = false;
    bool m_placeholderIconVisible = true;
    bool m_overlayControlsVisible = false;
    bool m_configButtonVisible = true;
    QString m_placeholderText;
    QString m_channelName;
    QString m_streamIp;
    QString m_streamPort;
    QString m_streamPath;
    QString m_previewUrl;
    QString m_mainUrl;
    QString m_videoPath;
    QString m_fallbackVideoPath;
    QString m_statusText;
    bool m_usingFallback = false;
    qint64 m_lastPresentedMsec = 0;
    QSvgRenderer m_placeholderRenderer;
    D3DVideoSurface *m_videoSurface = nullptr;
    QTimer *m_renderTimer = nullptr;
    std::shared_ptr<RtspStream> m_stream;
    QToolButton *m_playButton = nullptr;
    QToolButton *m_pauseButton = nullptr;
    QToolButton *m_stopButton = nullptr;
    QToolButton *m_configButton = nullptr;
    std::function<void(VideoOpenGLWidget *)> m_doubleClickHandler;
    std::function<void(VideoOpenGLWidget *)> m_configChangedHandler;
    std::function<void(VideoOpenGLWidget *)> m_streamChangedHandler;
};

#endif // VIDEOOPENGLWIDGET_H

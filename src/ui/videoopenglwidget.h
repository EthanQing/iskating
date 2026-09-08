#ifndef VIDEOOPENGLWIDGET_H
#define VIDEOOPENGLWIDGET_H

#include "athleteanalysisresult.h"

#include <QSvgRenderer>
#include <QString>
#include <QWidget>

#include <functional>
#include <memory>

class QEnterEvent;
class QContextMenuEvent;
class QEvent;
class QKeyEvent;
class QLabel;
class QMouseEvent;
class QPaintEvent;
class QToolButton;
class QTimer;
class QVariantAnimation;
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
    void playFile(const QString &filePath, qint64 startPositionMs);
    void pausePlayback();
    void stopPlayback();
    void seekTo(qint64 positionMs);
    void setPlaybackRate(double rate);
    void stepForward();
    qint64 positionMs() const;
    qint64 durationMs() const;
    bool isSeekable() const;
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
    void setAthleteFrame(const AthleteFrameResult &frame);
    void setSourceTile(bool enabled);
    bool isSourceTile() const;
    void setSelected(bool selected);
    bool isSelected() const;
    void setClickHandler(std::function<void(VideoOpenGLWidget *)> handler);
    QString sourceState() const;
    void setDoubleClickHandler(std::function<void(VideoOpenGLWidget *)> handler);
    void setConfigChangedHandler(std::function<void(VideoOpenGLWidget *)> handler);
    void setStreamChangedHandler(std::function<void(VideoOpenGLWidget *)> handler);

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    void setupOverlayControls();
    void layoutOverlayControls();
    void openConfigDialog();
    void attachStream(const QString &source, const QString &fallbackSource = QString(), qint64 startPositionMs = 0);
    void notifyStreamChanged();
    void refreshVideoFrame();
    void refreshAthleteLabels();
    void layoutVideoSurface();
    void animateTile(qreal target, int duration);
    QString cameraDisplayName() const;

    bool m_playing = false;
    bool m_placeholderIconVisible = true;
    bool m_overlayControlsVisible = false;
    bool m_configButtonVisible = true;
    bool m_sourceTile = false;
    bool m_selected = false;
    qreal m_tileHoverProgress = 0.0;
    qreal m_tilePressProgress = 0.0;
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
    QVariantAnimation *m_tileHoverAnimation = nullptr;
    QVariantAnimation *m_tilePressAnimation = nullptr;
    std::shared_ptr<RtspStream> m_stream;
    QToolButton *m_playButton = nullptr;
    QToolButton *m_pauseButton = nullptr;
    QToolButton *m_stopButton = nullptr;
    QToolButton *m_configButton = nullptr;
    QVector<QLabel *> m_athleteLabels;
    AthleteFrameResult m_athleteFrame;
    std::function<void(VideoOpenGLWidget *)> m_doubleClickHandler;
    std::function<void(VideoOpenGLWidget *)> m_clickHandler;
    std::function<void(VideoOpenGLWidget *)> m_configChangedHandler;
    std::function<void(VideoOpenGLWidget *)> m_streamChangedHandler;
};

#endif // VIDEOOPENGLWIDGET_H

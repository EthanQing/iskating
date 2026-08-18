#include "athletedetectionroi.h"
#include "athletetracker.h"
#include "cameraconfigtemplate.h"
#include "videostorageplan.h"

#include <QCoreApplication>
#include <QDate>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTemporaryDir>

#include <QtTest>

class ClientContractsTest final : public QObject
{
    Q_OBJECT

private slots:
    void cameraTemplateRoundTrip();
    void detectionRoiScalesFootPoint();
    void athleteTrackerKeepsOneToOneTracks();
    void trackReidPolicyThrottlesAttempts();
    void videoStoragePlanUsesStableLayout();
};

void ClientContractsTest::cameraTemplateRoundTrip()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    SharedCameraSettings shared;
    shared.username = QStringLiteral("coach");
    shared.password = QStringLiteral("secret");
    shared.port = QStringLiteral("8554");
    shared.previewPath = QStringLiteral("/preview");
    shared.mainPath = QStringLiteral("main");
    shared.nvrPlaybackTemplate = QStringLiteral("rtsp://{ip}/play?start={start}&end={end}");

    CapturePreferenceSettings capture;
    capture.modelPrecision = QStringLiteral("fast");
    capture.analysisSource = QStringLiteral("main");
    capture.analysisTargetFps = 8;
    capture.analysisMaxStreams = 4;
    capture.analysisAutoDegrade = false;

    CameraSlotSettings camera;
    camera.ip = QStringLiteral("10.0.0.7");
    camera.fieldStartM = 5.0;
    camera.fieldEndM = 10.0;
    camera.calibrationJson = QStringLiteral("{\"version\":1}");

    const QString path = QDir(temp.path()).filePath(QStringLiteral("camera-template.json"));
    QString error;
    QVERIFY2(saveCameraConfigTemplate(path, shared, capture, {camera}, &error), qPrintable(error));

    const CameraConfigTemplateResult loaded = loadCameraConfigTemplate(path, 2);
    QVERIFY2(loaded.ok, qPrintable(loaded.error));
    QCOMPARE(loaded.data.sourceCameraCount, 1);
    QCOMPARE(loaded.data.cameras.size(), 2);
    QCOMPARE(loaded.data.shared.port, QStringLiteral("8554"));
    QCOMPARE(loaded.data.shared.previewPath, QStringLiteral("preview"));
    QCOMPARE(loaded.data.capture.analysisSource, QStringLiteral("main"));
    QCOMPARE(loaded.data.capture.analysisTargetFps, 8);
    QCOMPARE(loaded.data.cameras.at(0).ip, QStringLiteral("10.0.0.7"));
    QCOMPARE(loaded.data.cameras.at(1).fieldStartM, 5.0);
    QVERIFY(!loaded.data.warnings.isEmpty());
}

void ClientContractsTest::detectionRoiScalesFootPoint()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString path = QDir(temp.path()).filePath(QStringLiteral("camera_detect_rois.json"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write(R"({"cameras":{"cam1":{"frame_width":1920,"frame_height":1080,"polygon":[[0,400],[1920,400],[1920,1000],[0,1000]]}}})");
    file.close();

    QStringList warnings;
    const AthleteDetectionRoiMap rois = loadAthleteDetectionRois(path, &warnings);
    QCOMPARE(warnings.size(), 0);
    QCOMPARE(rois.size(), 1);
    QVERIFY(rois.contains(1));

    const AthleteDetectionRoi roi = rois.value(1);
    QVERIFY(isAthleteDetectionInsideRoi(roi, QRectF(100, 240, 100, 100), QSize(960, 540)));
    QVERIFY(!isAthleteDetectionInsideRoi(roi, QRectF(100, 10, 100, 100), QSize(960, 540)));
}

void ClientContractsTest::athleteTrackerKeepsOneToOneTracks()
{
    AthleteTracker tracker;
    const QVector<int> first = tracker.update(1,
                                               {QRectF(10, 10, 100, 200), QRectF(300, 10, 100, 200)},
                                               1000);
    QCOMPARE(first.size(), 2);
    QVERIFY(first.at(0) != first.at(1));

    const QVector<int> second = tracker.update(1,
                                                {QRectF(305, 12, 100, 200), QRectF(12, 12, 100, 200)},
                                                1100);
    QCOMPARE(second.at(0), first.at(1));
    QCOMPARE(second.at(1), first.at(0));
    QVERIFY(tracker.track(1, first.at(0)));
    QCOMPARE(tracker.track(1, first.at(0))->hits, 2);

    const QVector<int> expired = tracker.update(1, {QRectF(12, 12, 100, 200)}, 2301);
    QVERIFY(expired.at(0) != first.at(0));
}

void ClientContractsTest::trackReidPolicyThrottlesAttempts()
{
    AthleteTrackReidPolicy policy;
    policy.minTrackHits = 2;
    policy.maxAttempts = 3;
    policy.retryIntervalMs = 1000;

    AthleteTrackState track;
    track.hits = 1;
    QVERIFY(!shouldAttemptAthleteTrackReid(track, policy, 1000));

    track.hits = 2;
    QVERIFY(shouldAttemptAthleteTrackReid(track, policy, 1000));

    track.reidAttempts = 1;
    track.lastReidAttemptMs = 1000;
    QVERIFY(!shouldAttemptAthleteTrackReid(track, policy, 1500));
    QVERIFY(shouldAttemptAthleteTrackReid(track, policy, 2000));

    track.identityStatus = QStringLiteral("identified");
    QVERIFY(!shouldAttemptAthleteTrackReid(track, policy, 3000));
}

void ClientContractsTest::videoStoragePlanUsesStableLayout()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temp.path());
    QCoreApplication::setOrganizationName(QStringLiteral("iskating-tests"));
    QCoreApplication::setApplicationName(QStringLiteral("client-contracts"));

    QSettings settings;
    settings.setValue(QStringLiteral("videoStorage/rootDir"), temp.path());
    settings.sync();

    VideoStoragePlanInput input;
    input.sessionId = QStringLiteral("session-001");
    input.athleteId = QStringLiteral("athlete-001");
    input.athleteName = QStringLiteral("Test Athlete");
    input.startedAt = QDateTime(QDate(2026, 7, 23), QTime(10, 20, 30), Qt::LocalTime);
    input.camera = 3;
    input.cameraName = QStringLiteral("side");
    input.sourceUrl = QStringLiteral("rtsp://10.0.0.7/main");
    input.durationSec = 12;

    const TrainingVideoFile plan = buildTrainingVideoFilePlan(input);
    QCOMPARE(plan.status, QStringLiteral("planned"));
    QCOMPARE(plan.videoIndex, 1);
    QCOMPARE(plan.camera, 3);
    QCOMPARE(plan.durationMs, 12000);
    QVERIFY(plan.relativeDir.endsWith(QStringLiteral("/athlete-athlete001/session-session001")));
    QVERIFY(plan.fileName.contains(QStringLiteral("_cam03_v01_session-session")));
    QVERIFY(plan.filePath.endsWith(plan.relativeDir + QLatin1Char('/') + plan.fileName));

    const QJsonObject metadata = QJsonDocument::fromJson(plan.metadataJson.toUtf8()).object();
    QCOMPARE(metadata.value(QStringLiteral("status")).toString(), QStringLiteral("planned"));
    QCOMPARE(metadata.value(QStringLiteral("camera")).toInt(), 3);
    QCOMPARE(metadata.value(QStringLiteral("durationMs")).toInt(), 12000);
}

QTEST_MAIN(ClientContractsTest)
#include "tst_clientcontracts.moc"

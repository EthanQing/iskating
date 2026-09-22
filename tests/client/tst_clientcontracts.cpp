#include "mainwindowpresentation.h"
#include "quickpanelhost.h"
#include <QQuickWidget>
#include <QQuickItem>
#include <QQmlEngine>
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
    void qmlPanelsLoad();
    void qmlHeaderRequests();
    void qmlTrainingRequests();
    void qmlNavigationAndSelection();
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

void ClientContractsTest::qmlPanelsLoad()
{
    MainWindowPresentation presentation;
    for (const QString &name : {QStringLiteral("NavigationPanel"), QStringLiteral("WorkspaceHeader"), QStringLiteral("TrainingPanel")}) {
        QuickPanelHost host(&presentation, QUrl(QStringLiteral("qrc:/qml/%1.qml").arg(name)));
        host.resize(420, 900);
        QCOMPARE(host.view()->status(), QQuickWidget::Ready);
        QVERIFY(host.view()->rootObject());
    }
}

namespace {
QQuickItem *panelItem(QQuickItem *parent, const QString &name)
{
    if (parent->objectName() == name) return parent;
    for (QQuickItem *child : parent->childItems()) {
        if (auto *found = panelItem(child, name)) return found;
    }
    return nullptr;
}
void clickPanelItem(QQuickWidget *view, QQuickItem *item)
{
    QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier,
                      item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
}
}

void ClientContractsTest::qmlHeaderRequests()
{
    MainWindowPresentation presentation;
    QuickPanelHost host(&presentation, QUrl(QStringLiteral("qrc:/qml/WorkspaceHeader.qml")));
    host.resize(750, 112);
    host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    QSignalSpy importSpy(&presentation, &MainWindowPresentation::importVideoRequested);
    QSignalSpy analysisSpy(&presentation, &MainWindowPresentation::offlineAnalysisRequested);
    QSignalSpy tasksSpy(&presentation, &MainWindowPresentation::taskCenterRequested);
    QSignalSpy fullScreenSpy(&presentation, &MainWindowPresentation::toggleFullScreenRequested);
    QSignalSpy checkSpy(&presentation, &MainWindowPresentation::checkRequested);
    for (const QString &name : {QStringLiteral("importVideo"), QStringLiteral("offlineAnalysis"),
                                QStringLiteral("taskCenter"), QStringLiteral("toggleFullScreen")}) {
        auto *button = panelItem(host.view()->rootObject(), name);
        QVERIFY(button);
        clickPanelItem(host.view(), button);
    }
    QCOMPARE(importSpy.count(), 1);
    QCOMPARE(analysisSpy.count(), 1);
    QCOMPARE(tasksSpy.count(), 1);
    QCOMPARE(fullScreenSpy.count(), 1);
    auto *check = panelItem(host.view()->rootObject(), QStringLiteral("environmentCheck"));
    QVERIFY(check && !check->isVisible());
    presentation.setActivePage(3);
    presentation.setPageTitle(QStringLiteral("系统状态"));
    QTRY_VERIFY(check->isVisible());
    QCoreApplication::processEvents();
    clickPanelItem(host.view(), check);
    QCOMPARE(checkSpy.count(), 1);
    presentation.setCanCheck(false);
    clickPanelItem(host.view(), check);
    QCOMPARE(checkSpy.count(), 1);
    auto *title = panelItem(host.view()->rootObject(), QStringLiteral("pageTitle"));
    QVERIFY(title);
    QCOMPARE(title->property("text").toString(), QStringLiteral("系统状态"));
}

void ClientContractsTest::qmlTrainingRequests()
{
    MainWindowPresentation presentation;
    presentation.setStartText(QStringLiteral("开始训练"));
    QuickPanelHost host(&presentation, QUrl(QStringLiteral("qrc:/qml/TrainingPanel.qml")));
    host.resize(360, 1000);
    host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    auto *start = panelItem(host.view()->rootObject(), QStringLiteral("startTraining"));
    auto *pause = panelItem(host.view()->rootObject(), QStringLiteral("pauseTraining"));
    auto *stop = panelItem(host.view()->rootObject(), QStringLiteral("stopTraining"));
    auto *save = panelItem(host.view()->rootObject(), QStringLiteral("saveTraining"));
    QVERIFY(start && pause && stop && save);
    QSignalSpy startSpy(&presentation, &MainWindowPresentation::startRequested);
    QSignalSpy pauseSpy(&presentation, &MainWindowPresentation::pauseRequested);
    QSignalSpy stopSpy(&presentation, &MainWindowPresentation::stopRequested);
    QSignalSpy saveSpy(&presentation, &MainWindowPresentation::saveRequested);
    clickPanelItem(host.view(), start);
    QCOMPARE(startSpy.count(), 1);
    clickPanelItem(host.view(), pause);
    clickPanelItem(host.view(), stop);
    clickPanelItem(host.view(), save);
    QCOMPARE(pauseSpy.count(), 0);
    QCOMPARE(stopSpy.count(), 0);
    QCOMPARE(saveSpy.count(), 0);
    presentation.setCanStart(false);
    presentation.setCanPause(true);
    presentation.setCanStop(true);
    presentation.setCanSave(true);
    QTRY_VERIFY(!start->isEnabled());
    clickPanelItem(host.view(), start);
    QCOMPARE(startSpy.count(), 1);
    clickPanelItem(host.view(), pause);
    clickPanelItem(host.view(), stop);
    clickPanelItem(host.view(), save);
    QCOMPARE(pauseSpy.count(), 1);
    QCOMPARE(stopSpy.count(), 1);
    QCOMPARE(saveSpy.count(), 1);
    presentation.setStartText(QStringLiteral("继续训练"));
    QCOMPARE(start->property("text").toString(), QStringLiteral("继续训练"));
    presentation.setSaveTip(QStringLiteral("保存失败：服务未连接"));
    auto *tip = panelItem(host.view()->rootObject(), QStringLiteral("saveTip"));
    QVERIFY(tip);
    QCOMPARE(tip->property("text").toString(), presentation.saveTip());
}

void ClientContractsTest::qmlNavigationAndSelection()
{
    MainWindowPresentation presentation;
    QuickPanelHost nav(&presentation, QUrl(QStringLiteral("qrc:/qml/NavigationPanel.qml")));
    nav.resize(220, 750);
    nav.show();
    QVERIFY(QTest::qWaitForWindowExposed(&nav));
    connect(&presentation, &MainWindowPresentation::pageRequested,
            &presentation, &MainWindowPresentation::setActivePage);
    QSignalSpy pageSpy(&presentation, &MainWindowPresentation::pageRequested);
    for (int page : {1, 3, 0}) {
        auto *button = panelItem(nav.view()->rootObject(), QStringLiteral("navigation%1").arg(page));
        QVERIFY(button);
        clickPanelItem(nav.view(), button);
        QCOMPARE(presentation.activePage(), page);
        QVERIFY(button->property("selected").toBool());
    }
    QCOMPARE(pageSpy.count(), 3);
    presentation.navigate(2); // The legacy insights page is not a public navigation entry.
    QCOMPARE(pageSpy.count(), 3);
    presentation.setSidebarExpanded(false);
    nav.resize(64, 750);
    auto *history = panelItem(nav.view()->rootObject(), QStringLiteral("navigation1"));
    QTRY_VERIFY2(history->width() <= 44, qPrintable(QStringLiteral("host %1 view %2 item %3").arg(nav.width()).arg(nav.view()->width()).arg(history->width())));
    clickPanelItem(nav.view(), history);
    QCOMPARE(presentation.activePage(), 1);

    presentation.setAthletes({QVariantMap{{"id", "a"}, {"name", "同名运动员"}},
                              QVariantMap{{"id", "b"}, {"name", "同名运动员"}}});
    presentation.setSelectedAthleteId(QStringLiteral("a"));
    QuickPanelHost training(&presentation, QUrl(QStringLiteral("qrc:/qml/TrainingPanel.qml")));
    training.resize(360, 900);
    training.show();
    QVERIFY(QTest::qWaitForWindowExposed(&training));
    auto *selector = panelItem(training.view()->rootObject(), QStringLiteral("athleteSelector"));
    QVERIFY(selector);
    QCOMPARE(selector->property("currentIndex").toInt(), 0);
    connect(&presentation, &MainWindowPresentation::athleteSelected,
            &presentation, &MainWindowPresentation::setSelectedAthleteId);
    QSignalSpy selectionSpy(&presentation, &MainWindowPresentation::athleteSelected);
    selector->forceActiveFocus();
    QTest::keyClick(training.view(), Qt::Key_Down);
    QTRY_COMPARE(presentation.selectedAthleteId(), QStringLiteral("b"));
    QCOMPARE(selectionSpy.count(), 1);
    presentation.setSelectedAthleteId(QStringLiteral("a"));
    QCOMPARE(selector->property("currentIndex").toInt(), 0);
    presentation.selectAthlete(QStringLiteral("missing"));
    QCOMPARE(presentation.selectedAthleteId(), QStringLiteral("a"));
    presentation.setAthletes({QVariantMap{{"id", "b"}, {"name", "同名运动员"}},
                              QVariantMap{{"id", "a"}, {"name", "同名运动员"}}});
    QCOMPARE(selector->property("currentIndex").toInt(), 1);
    presentation.setAthletes({});
    presentation.setSelectedAthleteId(QString());
    QCOMPARE(selector->property("currentIndex").toInt(), -1);
}

QTEST_MAIN(ClientContractsTest)
#include "tst_clientcontracts.moc"

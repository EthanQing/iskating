#include "cameraconnectivitytester.h"

#include <QPointer>
#include <QSignalSpy>
#include <QThread>
#include <QUrl>
#include <QtTest>

class CameraConnectivityTest : public QObject
{
    Q_OBJECT

private slots:
    void unconfiguredSlotsAreSkipped();
    void failuresDoNotStopRemainingSlots();
    void interruptionFinishesAndDeletesWorker();
    void logUrlHidesPassword();
};

void CameraConnectivityTest::unconfiguredSlotsAreSkipped()
{
    QVector<CameraSlotSettings> cameras(12);
    cameras[3].ip = QStringLiteral("   ");
    CameraConnectivityTester tester({}, cameras);
    QSignalSpy progress(&tester, &CameraConnectivityTester::progress);
    QSignalSpy finished(&tester, &CameraConnectivityTester::finished);
    tester.run();
    QCOMPARE(progress.count(), 12);
    QCOMPARE(finished.count(), 1);
    const auto results = qvariant_cast<QVector<CameraConnectivityResult>>(finished.first().first());
    QCOMPARE(results.size(), 12);
    for (int i = 0; i < results.size(); ++i) {
        QCOMPARE(results.at(i).cameraIndex, i);
        QVERIFY(results.at(i).skipped);
        QVERIFY(!results.at(i).success);
    }
}

void CameraConnectivityTest::failuresDoNotStopRemainingSlots()
{
    // Missing preview path fails locally before any network request.
    QVector<CameraSlotSettings> cameras(12);
    cameras[3].ip = QStringLiteral("192.0.2.4");
    cameras[6].ip = QStringLiteral("192.0.2.7");
    SharedCameraSettings shared;
    shared.previewPath.clear();
    CameraConnectivityTester tester(shared, cameras);
    QSignalSpy finished(&tester, &CameraConnectivityTester::finished);
    tester.run();
    const auto results = qvariant_cast<QVector<CameraConnectivityResult>>(finished.first().first());
    int failed = 0;
    int skipped = 0;
    for (const auto &result : results) {
        if (result.skipped) {
            ++skipped;
        } else {
            ++failed;
            QVERIFY(!result.success);
            QCOMPARE(result.failureStage, QStringLiteral("configuration"));
            QCOMPARE(result.errorCode, QStringLiteral("preview_path_missing"));
        }
    }
    QCOMPARE(failed, 2);
    QCOMPARE(skipped, 10);
}

void CameraConnectivityTest::interruptionFinishesAndDeletesWorker()
{
    QThread thread;
    QPointer<CameraConnectivityTester> tester = new CameraConnectivityTester({}, QVector<CameraSlotSettings>(12));
    tester->moveToThread(&thread);
    QSignalSpy finished(tester, &CameraConnectivityTester::finished);
    connect(&thread, &QThread::started, tester, &CameraConnectivityTester::run);
    connect(tester, &CameraConnectivityTester::progress, tester, [&thread]() {
        thread.requestInterruption();
    }, Qt::DirectConnection);
    connect(tester, &CameraConnectivityTester::finished, tester, &QObject::deleteLater);
    connect(tester, &CameraConnectivityTester::finished, &thread, &QThread::quit, Qt::DirectConnection);
    thread.start();
    const bool stopped = thread.wait(3000);
    if (!stopped) {
        thread.requestInterruption();
        thread.quit();
        thread.wait();
    }
    QVERIFY(stopped);
    QCOMPARE(finished.count(), 1);
    const auto results = qvariant_cast<QVector<CameraConnectivityResult>>(finished.first().first());
    QCOMPARE(results.size(), 1);
    QVERIFY(tester.isNull());
}

void CameraConnectivityTest::logUrlHidesPassword()
{
    SharedCameraSettings shared;
    shared.username = QStringLiteral("test-user");
    shared.password = QStringLiteral("secret@:/word");
    shared.previewPath = QStringLiteral("/preview");
    const QString url = composeCameraPreviewTestUrl(shared, QStringLiteral("192.0.2.4"));
    const QString safe = safeCameraTestUrlForLog(url);
    QVERIFY(!safe.contains(shared.password));
    QVERIFY(!safe.contains(QString::fromLatin1(QUrl::toPercentEncoding(shared.password))));
    QCOMPARE(QUrl(safe).password(), QStringLiteral("***"));
}

QTEST_GUILESS_MAIN(CameraConnectivityTest)
#include "tst_cameraconnectivity.moc"

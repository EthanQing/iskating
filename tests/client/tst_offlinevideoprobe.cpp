#include "analysistaskmanager.h"
#include "d3d11videodevice.h"

#include <QtTest>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <atomic>

#ifdef FORCED_FALLBACK
static std::atomic<int> deviceDelayMs{0};
AVBufferRef *D3D11VideoDevice::createFfmpegHwDevice(QString *error)
{
    QThread::msleep(deviceDelayMs.load());
    if (error) *error = QStringLiteral("Test device creation failure");
    return nullptr;
}
#endif

class OfflineVideoProbeTests : public QObject
{
    Q_OBJECT
    QTemporaryDir files;
    QString ffmpeg;
    QString h264;

    QString generate(const QString &name, const QStringList &encoding, int frames = 3, const QString &extension = ".mkv")
    {
        const QString path = files.filePath(name + extension);
        QProcess process;
        process.start(ffmpeg, QStringList{"-v", "error", "-y", "-f", "lavfi", "-i",
                      "testsrc2=size=128x96:rate=10", "-frames:v", QString::number(frames)} + encoding + QStringList{path});
        if (!process.waitForFinished(30000) || process.exitCode() != 0) {
            qWarning().noquote() << process.readAllStandardError();
            return {};
        }
        return path;
    }

private slots:
    void initTestCase()
    {
        QVERIFY(files.isValid());
        ffmpeg = qEnvironmentVariable("FFMPEG_ROOT", "C:/Users/qc/zm/ffmpeg-8.0.1-full_build-shared") + "/bin/ffmpeg.exe";
        QVERIFY(QFile::exists(ffmpeg));
        QCoreApplication::setOrganizationName("OfflineProbeTests");
        QCoreApplication::setApplicationName("OfflineProbeTests");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, files.path());
        h264 = generate("h264", {"-c:v", "libx264", "-pix_fmt", "yuv420p", "-bf", "2"});
        QVERIFY(!h264.isEmpty());
    }

    void codecs_data()
    {
        QTest::addColumn<QString>("encoder");
        QTest::addColumn<QString>("pixelFormat");
        QTest::newRow("h264") << "libx264" << "yuv420p";
        QTest::newRow("hevc") << "libx265" << "yuv420p";
        QTest::newRow("h264-444") << "libx264" << "yuv444p";
        QTest::newRow("hevc-10bit") << "libx265" << "yuv420p10le";
        QTest::newRow("ffv1-no-hardware-config") << "ffv1" << "yuv444p";
    }

    void codecs()
    {
        QFETCH(QString, encoder);
        QFETCH(QString, pixelFormat);
        const QString path = generate(encoder + pixelFormat, {"-c:v", encoder, "-pix_fmt", pixelFormat});
        QVERIFY(!path.isEmpty());
        const auto result = OfflineVideoProbe::probe(path);
        QVERIFY2(result.success, qPrintable(result.message));
        QVERIFY(result.seekable);
        QVERIFY(result.durationMs > 0);
        QCOMPARE(result.resolution, QString("128x96"));
#ifdef FORCED_FALLBACK
        QVERIFY(!result.d3d11vaReady);
#endif
        if (encoder == "ffv1" || pixelFormat == "yuv444p") QVERIFY(!result.d3d11vaReady);
    }

    void hardwarePreferred()
    {
#ifdef FORCED_FALLBACK
        QSKIP("Hardware is intentionally disabled in this build.");
#else
        const auto result = OfflineVideoProbe::probe(h264);
        QVERIFY2(result.success, qPrintable(result.message));
        if (!result.d3d11vaReady) QSKIP("This machine did not provide D3D11VA H.264 decoding.");
        QVERIFY(result.d3d11vaReady);
#endif
    }

    void independentBudget()
    {
#ifdef FORCED_FALLBACK
        deviceDelayMs = 1200;
        const auto result = OfflineVideoProbe::probe(h264, 1000);
        deviceDelayMs = 0;
        QVERIFY2(result.success, qPrintable(result.message));
        QVERIFY(!result.d3d11vaReady);
#else
        QSKIP("Requires the forced device failure build.");
#endif
    }

    void delayedSingleFrame()
    {
        const QString path = generate("single", {"-c:v", "libx264", "-bf", "2"}, 1);
        QVERIFY(!path.isEmpty());
        const auto result = OfflineVideoProbe::probe(path);
        QVERIFY2(result.success, qPrintable(result.message));
    }

    void damagedVideoPackets()
    {
        const QString path = generate("damaged", {"-c:v", "libx264", "-bf", "2"}, 3, ".mp4");
        QVERIFY(!path.isEmpty());
        QProcess process;
        QString ffprobe = ffmpeg;
        ffprobe.replace("ffmpeg.exe", "ffprobe.exe");
        process.start(ffprobe, {"-v", "error", "-select_streams", "v", "-show_packets",
                               "-show_entries", "packet=pos,size", "-of", "json", path});
        QVERIFY(process.waitForFinished(30000));
        QCOMPARE(process.exitCode(), 0);
        const auto packets = QJsonDocument::fromJson(process.readAllStandardOutput()).object().value("packets").toArray();
        QVERIFY(!packets.isEmpty());
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadWrite));
        for (const auto &packet : packets) {
            const auto object = packet.toObject();
            QVERIFY(file.seek(object.value("pos").toString().toLongLong()));
            const int size = object.value("size").toString().toInt();
            QCOMPARE(file.write(QByteArray(size, '\0')), qint64(size));
        }
        file.close();
        const auto result = OfflineVideoProbe::probe(path);
        QVERIFY(!result.success);
        QVERIFY(result.durationMs > 0);
        QCOMPARE(result.codecName, QString("h264"));
        QVERIFY(!result.message.isEmpty());
    }

    void invalidMedia()
    {
        QVERIFY(!OfflineVideoProbe::probe(files.filePath("missing.mkv")).success);
        QFile empty(files.filePath("empty.mkv"));
        QVERIFY(empty.open(QIODevice::WriteOnly));
        empty.close();
        QVERIFY(!OfflineVideoProbe::probe(empty.fileName()).success);
        QFile corrupt(files.filePath("corrupt.mkv"));
        QVERIFY(corrupt.open(QIODevice::WriteOnly));
        corrupt.write("not a media container");
        corrupt.close();
        const auto result = OfflineVideoProbe::probe(corrupt.fileName());
        QVERIFY(!result.success);
        QVERIFY(!result.message.isEmpty());
    }

    void audioOnly()
    {
        const QString path = files.filePath("audio.wav");
        QProcess process;
        process.start(ffmpeg, {"-v", "error", "-y", "-f", "lavfi", "-i", "sine=duration=0.3", path});
        QVERIFY(process.waitForFinished(30000));
        QCOMPARE(process.exitCode(), 0);
        const auto result = OfflineVideoProbe::probe(path);
        QVERIFY(!result.success);
        QVERIFY(result.message.contains(QStringLiteral("视频轨道")));
    }

    void saveImport_data()
    {
        QTest::addColumn<bool>("failSave");
        QTest::newRow("success") << false;
        QTest::newRow("save-failure") << true;
    }

    void saveImport()
    {
        QFETCH(bool, failSave);
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QSettings settings;
        settings.setValue("server/baseUrl", QString("http://127.0.0.1:%1").arg(server.serverPort()));
        settings.setValue("auth/accessToken", "isolated-test-token");
        settings.sync();
        QJsonObject saved;
        connect(&server, &QTcpServer::newConnection, this, [&] {
            while (auto *socket = server.nextPendingConnection()) {
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                    QByteArray request = socket->property("request").toByteArray() + socket->readAll();
                    socket->setProperty("request", request);
                    const int headerEnd = request.indexOf("\r\n\r\n");
                    if (headerEnd < 0) return;
                    int length = 0;
                    for (const auto &line : request.left(headerEnd).split('\n')) {
                        if (line.toLower().startsWith("content-length:")) length = line.mid(15).trimmed().toInt();
                    }
                    if (request.size() < headerEnd + 4 + length) return;
                    const QByteArray route = request.left(request.indexOf("\r\n"));
                    QByteArray response = "{}";
                    bool failure = false;
                    if (route.startsWith("GET /athletes ")) response = "[]";
                    if (route.startsWith("POST ")) response = request.mid(headerEnd + 4, length);
                    if (route.startsWith("POST /offline-analysis/tasks ")) {
                        saved = QJsonDocument::fromJson(response).object();
                        failure = failSave;
                        if (failure) response = "{\"detail\":\"test save failure\"}";
                    }
                    socket->write(QByteArray(failure ? "HTTP/1.1 500 Error\r\n" : "HTTP/1.1 200 OK\r\n")
                                  + "Content-Type: application/json\r\nConnection: close\r\nContent-Length: "
                                  + QByteArray::number(response.size()) + "\r\n\r\n" + response);
                    socket->disconnectFromHost();
                });
            }
        });
        AnalysisTaskManager manager;
        QSignalSpy ready(&manager, &AnalysisTaskManager::offlineImportReady);
        const QString id = manager.enqueueOfflineImport(h264);
        auto status = [&] {
            for (const auto &task : manager.tasks()) if (task.id == id) return task.status;
            return QString();
        };
        QTRY_COMPARE_WITH_TIMEOUT(status(), failSave ? QString("failed") : QString("completed"), 15000);
        QVERIFY(!saved.isEmpty());
        const QJsonObject metadata = saved.value("probeMetadata").toObject();
        const bool hardware = metadata.value("d3d11vaReady").toBool();
        QCOMPARE(metadata.value("decodeMode").toString(), hardware ? QString("d3d11va") : QString("software"));
#ifdef FORCED_FALLBACK
        QVERIFY(!hardware);
#endif
        if (failSave) QCOMPARE(ready.count(), 0);
        else {
            QTRY_COMPARE(ready.count(), 1);
            QVERIFY(qvariant_cast<OfflineVideoProbeResult>(ready.at(0).at(1)).success);
        }
    }
};

QTEST_GUILESS_MAIN(OfflineVideoProbeTests)
#include "tst_offlinevideoprobe.moc"

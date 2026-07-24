#ifndef OFFLINEVIDEOPROBE_H
#define OFFLINEVIDEOPROBE_H

#include <QDateTime>
#include <QString>

struct OfflineVideoProbeResult
{
    bool success = false;
    QString message;
    QString filePath;
    QString codecName;
    QString resolution;
    qint64 durationMs = -1;
    bool seekable = false;
    bool d3d11vaReady = false;
    qint64 fileSize = 0;
    QDateTime lastModified;
};

class OfflineVideoProbe
{
public:
    static OfflineVideoProbeResult probe(const QString &filePath, int timeoutMs = 5000);
};

#endif // OFFLINEVIDEOPROBE_H

#ifndef STREAMREGISTRY_H
#define STREAMREGISTRY_H

#include <QHash>
#include <QMutex>
#include <QString>

#include <memory>

class RtspStream;

class StreamRegistry
{
public:
    static StreamRegistry &instance();
    std::shared_ptr<RtspStream> acquire(const QString &url);

private:
    StreamRegistry() = default;

    QMutex m_mutex;
    QHash<QString, std::weak_ptr<RtspStream>> m_streams;
};

#endif // STREAMREGISTRY_H

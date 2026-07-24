#include "streamregistry.h"
#include "rtspstream.h"

StreamRegistry &StreamRegistry::instance()
{
    static StreamRegistry registry;
    return registry;
}

std::shared_ptr<RtspStream> StreamRegistry::acquire(const QString &url)
{
    const QString key = url.trimmed();
    if (key.isEmpty()) {
        return {};
    }

    QMutexLocker locker(&m_mutex);
    if (auto existing = m_streams.value(key).lock()) {
        return existing;
    }

    auto stream = std::make_shared<RtspStream>(key);
    m_streams.insert(key, stream);
    stream->start();
    return stream;
}

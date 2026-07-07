#include "poseidentityresolver.h"

void PoseIdentityResolver::setBindings(const QVector<PoseIdentityBinding> &bindings)
{
    m_bindings = bindings;
}

QVector<PoseIdentityBinding> PoseIdentityResolver::bindings() const
{
    return m_bindings;
}

PoseFrameResult PoseIdentityResolver::resolve(const PoseFrameResult &frame) const
{
    PoseFrameResult resolved = frame;
    for (PoseInstance &instance : resolved.instances) {
        if (!instance.athleteId.trimmed().isEmpty() || !instance.participantId.trimmed().isEmpty()) {
            if (instance.identityStatus.trimmed().isEmpty() || instance.identityStatus == QStringLiteral("unknown")) {
                instance.identityStatus = QStringLiteral("identified");
            }
            if (instance.identitySource.trimmed().isEmpty()) {
                instance.identitySource = QStringLiteral("algorithm");
            }
            continue;
        }

        instance.identityStatus = QStringLiteral("unknown");
        for (const PoseIdentityBinding &binding : m_bindings) {
            if (binding.cameraId != resolved.cameraId || binding.trackId != instance.trackId) {
                continue;
            }
            instance.participantId = binding.participantId;
            instance.athleteId = binding.athleteId;
            instance.identityStatus = QStringLiteral("identified");
            instance.identityConfidence = 1.0f;
            instance.identitySource = QStringLiteral("manual");
            break;
        }
    }
    return resolved;
}

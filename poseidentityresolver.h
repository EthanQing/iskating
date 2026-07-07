#ifndef POSEIDENTITYRESOLVER_H
#define POSEIDENTITYRESOLVER_H

#include "poseresult.h"

#include <QString>
#include <QVector>

struct PoseIdentityBinding
{
    int cameraId = 0;
    int trackId = -1;
    QString participantId;
    QString athleteId;
    QString label;
};

class PoseIdentityResolver
{
public:
    void setBindings(const QVector<PoseIdentityBinding> &bindings);
    QVector<PoseIdentityBinding> bindings() const;
    PoseFrameResult resolve(const PoseFrameResult &frame) const;

private:
    QVector<PoseIdentityBinding> m_bindings;
};

#endif // POSEIDENTITYRESOLVER_H

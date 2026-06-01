#include "actionstandardscorer.h"

#include "poseresult.h"
#include "posestandardnessscorer.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineF>
#include <QtMath>

#include <algorithm>

namespace {

int weightedScore(const PoseStandardnessResult &base, const ActionStandard &standard)
{
    const double totalWeight = std::max(0.01,
                                        standard.detectionWeight
                                            + standard.symmetryWeight
                                            + standard.balanceWeight
                                            + standard.stabilityWeight
                                            + standard.depthWeight);
    const double score = base.detectionScore * standard.detectionWeight
                         + base.symmetryScore * standard.symmetryWeight
                         + base.balanceScore * standard.balanceWeight
                         + base.stabilityScore * standard.stabilityWeight
                         + base.depthScore * standard.depthWeight;
    return std::clamp(qRound(score / totalWeight), 0, 100);
}

const PoseInstance *primaryInstance(const PoseFrameResult &frame)
{
    const PoseInstance *best = nullptr;
    for (const PoseInstance &instance : frame.instances) {
        if (!best || instance.confidence > best->confidence) {
            best = &instance;
        }
    }
    return best;
}

const PoseKeypoint *keypointAt(const PoseInstance &instance, int index)
{
    if (index < 0 || index >= instance.keypoints.size()) {
        return nullptr;
    }
    const PoseKeypoint &keypoint = instance.keypoints.at(index);
    return keypoint.valid ? &keypoint : nullptr;
}

bool kneeBendValue(const PoseFrameResult &frame, double *value)
{
    const PoseInstance *person = primaryInstance(frame);
    if (!person || person->keypoints.size() <= 16) {
        return false;
    }

    const PoseKeypoint *leftHip = keypointAt(*person, 11);
    const PoseKeypoint *rightHip = keypointAt(*person, 12);
    const PoseKeypoint *leftKnee = keypointAt(*person, 13);
    const PoseKeypoint *rightKnee = keypointAt(*person, 14);
    if (!leftHip || !rightHip || !leftKnee || !rightKnee) {
        return false;
    }

    const qreal hipY = (leftHip->imagePoint.y() + rightHip->imagePoint.y()) * 0.5;
    const qreal kneeY = (leftKnee->imagePoint.y() + rightKnee->imagePoint.y()) * 0.5;
    const qreal bodyScale = std::max<qreal>(1.0, person->box.height());
    *value = (kneeY - hipY) / bodyScale;
    return true;
}

ActionIssue issueForMetric(const QString &title,
                           const QString &bodyPart,
                           const QString &cause,
                           const QString &correction,
                           int priority)
{
    ActionIssue issue;
    issue.title = title;
    issue.bodyPart = bodyPart;
    issue.cause = cause;
    issue.correction = correction;
    issue.priority = priority;
    return issue;
}

QString poseFrameToJson(const PoseFrameResult &frame)
{
    QJsonObject root;
    root.insert(QStringLiteral("cameraId"), frame.cameraId);
    root.insert(QStringLiteral("timestampMs"), QString::number(frame.timestampMs));
    root.insert(QStringLiteral("width"), frame.frameSize.width());
    root.insert(QStringLiteral("height"), frame.frameSize.height());
    root.insert(QStringLiteral("skeletonType"), static_cast<int>(frame.skeletonType));
    root.insert(QStringLiteral("sourceName"), frame.sourceName);

    QJsonArray instances;
    for (const PoseInstance &instance : frame.instances) {
        QJsonObject instanceObject;
        instanceObject.insert(QStringLiteral("trackId"), instance.trackId);
        instanceObject.insert(QStringLiteral("skeletonType"), static_cast<int>(instance.skeletonType));
        instanceObject.insert(QStringLiteral("kind"), static_cast<int>(instance.kind));
        instanceObject.insert(QStringLiteral("confidence"), instance.confidence);
        instanceObject.insert(QStringLiteral("x"), instance.box.x());
        instanceObject.insert(QStringLiteral("y"), instance.box.y());
        instanceObject.insert(QStringLiteral("w"), instance.box.width());
        instanceObject.insert(QStringLiteral("h"), instance.box.height());

        QJsonArray keypoints;
        for (const PoseKeypoint &keypoint : instance.keypoints) {
            QJsonObject keypointObject;
            keypointObject.insert(QStringLiteral("index"), keypoint.index);
            keypointObject.insert(QStringLiteral("name"), keypoint.name);
            keypointObject.insert(QStringLiteral("x"), keypoint.imagePoint.x());
            keypointObject.insert(QStringLiteral("y"), keypoint.imagePoint.y());
            keypointObject.insert(QStringLiteral("x3"), keypoint.point3d.x());
            keypointObject.insert(QStringLiteral("y3"), keypoint.point3d.y());
            keypointObject.insert(QStringLiteral("z3"), keypoint.point3d.z());
            keypointObject.insert(QStringLiteral("confidence"), keypoint.confidence);
            keypointObject.insert(QStringLiteral("valid"), keypoint.valid);
            keypointObject.insert(QStringLiteral("hasPoint3d"), keypoint.hasPoint3d);
            keypoints.append(keypointObject);
        }
        instanceObject.insert(QStringLiteral("keypoints"), keypoints);
        instances.append(instanceObject);
    }
    root.insert(QStringLiteral("instances"), instances);
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

} // namespace

ActionAssessment ActionStandardScorer::score(const PoseStandardnessResult &baseResult,
                                             const ActionStandard &standard) const
{
    ActionAssessment assessment;
    assessment.detectionScore = baseResult.detectionScore;
    assessment.symmetryScore = baseResult.symmetryScore;
    assessment.balanceScore = baseResult.balanceScore;
    assessment.stabilityScore = baseResult.stabilityScore;
    assessment.depthScore = baseResult.depthScore;
    assessment.valid = baseResult.valid;

    if (!baseResult.valid) {
        assessment.feedback = QStringLiteral("等待姿态");
        return assessment;
    }

    assessment.score = weightedScore(baseResult, standard);
    struct MetricState {
        QString label;
        int value = 0;
        int minValue = 0;
        QString bodyPart;
        QString cause;
        QString correction;
    };

    const QVector<MetricState> metrics = {
        {QStringLiteral("关键点不足"), assessment.detectionScore, standard.detectionMin,
         QStringLiteral("全身关键点"), QStringLiteral("画面中关键点置信度不足或身体未完整入镜"),
         QStringLiteral("调整机位和站位，保证头肩髋膝踝持续可见")},
        {QStringLiteral("左右不对称"), assessment.symmetryScore, standard.symmetryMin,
         QStringLiteral("肩髋"), QStringLiteral("左右肩髋宽度变化过大，发力或展开不一致"),
         QStringLiteral("降低速度，先做左右同步的慢速重复")},
        {QStringLiteral("重心偏移"), assessment.balanceScore, standard.balanceMin,
         QStringLiteral("髋部/支撑腿"), QStringLiteral("肩髋中心偏离，支撑重心不稳定"),
         QStringLiteral("把核心收紧，重心压回支撑脚上方")},
        {QStringLiteral("动作波动大"), assessment.stabilityScore, standard.stabilityMin,
         QStringLiteral("躯干"), QStringLiteral("连续帧关键点位移过大，动作节奏不稳"),
         QStringLiteral("先放慢节奏，减少多余摆动后再逐步提速")},
        {QStringLiteral("3D深度不足"), assessment.depthScore, standard.depthMin,
         QStringLiteral("空间姿态"), QStringLiteral("三维关键点不足，空间姿态可信度偏低"),
         QStringLiteral("调整相机角度和身体朝向，提高立体姿态可辨识度")}
    };

    MetricState weakest;
    bool hasWeakMetric = false;
    for (const MetricState &metric : metrics) {
        if (metric.value >= metric.minValue) {
            continue;
        }
        if (!hasWeakMetric || metric.value < weakest.value) {
            weakest = metric;
            hasWeakMetric = true;
        }
    }

    if (hasWeakMetric) {
        ActionIssue issue = issueForMetric(weakest.label,
                                           weakest.bodyPart,
                                           weakest.cause,
                                           weakest.correction,
                                           2);
        if (!standard.issueTitle.trimmed().isEmpty()) {
            issue.title = standard.issueTitle;
            issue.bodyPart = standard.issueBodyPart;
            issue.cause = standard.issueCause;
            issue.correction = standard.issueCorrection;
            issue.priority = standard.issuePriority;
        }
        assessment.issues.append(issue);
        assessment.feedback = QStringLiteral("%1：%2").arg(issue.title, issue.correction);
    } else if (assessment.score >= standard.targetScore) {
        assessment.feedback = QStringLiteral("%1 达标").arg(standard.name);
    } else {
        assessment.feedback = QStringLiteral("%1 接近达标，继续保持节奏").arg(standard.name);
    }

    return assessment;
}

void ActionRepetitionTracker::reset(const ActionStandard &standard)
{
    clear();
    m_standard = standard;
    m_hasStandard = !standard.id.trimmed().isEmpty();
}

void ActionRepetitionTracker::clear()
{
    m_armed = false;
    m_startedMs = 0;
    m_frameCount = 0;
    m_scoreSum = 0;
    m_detectionSum = 0;
    m_symmetrySum = 0;
    m_balanceSum = 0;
    m_stabilitySum = 0;
    m_depthSum = 0;
    m_lowestScore = 101;
    m_keyFrameMs = 0;
    m_keyFramePoseFrame = {};
    m_feedback.clear();
    m_issueTitles.clear();
}

bool ActionRepetitionTracker::update(const PoseFrameResult &poseFrame,
                                     const ActionAssessment &assessment,
                                     qint64 nowMsec,
                                     qint64 sessionStartMsec,
                                     ActionRepetition *completedRepetition)
{
    if (!m_hasStandard || !assessment.valid || sessionStartMsec <= 0) {
        return false;
    }

    double kneeBend = 0.0;
    if (!kneeBendValue(poseFrame, &kneeBend)) {
        return false;
    }

    const int relativeMs = std::max<qint64>(0, nowMsec - sessionStartMsec);
    if (!m_armed && kneeBend > m_standard.armThreshold) {
        beginRepetition(relativeMs, poseFrame, assessment);
        return false;
    }

    if (m_armed) {
        accumulate(relativeMs, poseFrame, assessment);
    }

    if (m_armed && kneeBend < m_standard.releaseThreshold) {
        if (nowMsec - m_lastCompletedMsec < m_standard.debounceMs) {
            clear();
            return false;
        }

        if (completedRepetition) {
            *completedRepetition = complete(relativeMs, assessment);
        }
        m_lastCompletedMsec = nowMsec;
        clear();
        return completedRepetition != nullptr;
    }

    return false;
}

void ActionRepetitionTracker::beginRepetition(int relativeMs,
                                              const PoseFrameResult &poseFrame,
                                              const ActionAssessment &assessment)
{
    clear();
    m_armed = true;
    m_startedMs = relativeMs;
    accumulate(relativeMs, poseFrame, assessment);
}

void ActionRepetitionTracker::accumulate(int relativeMs,
                                         const PoseFrameResult &poseFrame,
                                         const ActionAssessment &assessment)
{
    ++m_frameCount;
    m_scoreSum += assessment.score;
    m_detectionSum += assessment.detectionScore;
    m_symmetrySum += assessment.symmetryScore;
    m_balanceSum += assessment.balanceScore;
    m_stabilitySum += assessment.stabilityScore;
    m_depthSum += assessment.depthScore;
    if (assessment.score < m_lowestScore) {
        m_lowestScore = assessment.score;
        m_keyFrameMs = relativeMs;
        m_keyFramePoseFrame = poseFrame;
        m_feedback = assessment.feedback;
    }
    for (const ActionIssue &issue : assessment.issues) {
        if (!issue.title.trimmed().isEmpty() && !m_issueTitles.contains(issue.title)) {
            m_issueTitles.append(issue.title);
        }
    }
}

ActionRepetition ActionRepetitionTracker::complete(int relativeMs, const ActionAssessment &assessment)
{
    if (m_frameCount <= 0) {
        accumulate(relativeMs, m_keyFramePoseFrame, assessment);
    }

    const int denominator = std::max(1, m_frameCount);
    ActionRepetition repetition;
    repetition.actionStandardId = m_standard.id;
    repetition.standardVersion = m_standard.version;
    repetition.startedMs = m_startedMs;
    repetition.endedMs = relativeMs;
    repetition.score = std::clamp((m_scoreSum + denominator / 2) / denominator, 0, 100);
    repetition.detectionScore = std::clamp((m_detectionSum + denominator / 2) / denominator, 0, 100);
    repetition.symmetryScore = std::clamp((m_symmetrySum + denominator / 2) / denominator, 0, 100);
    repetition.balanceScore = std::clamp((m_balanceSum + denominator / 2) / denominator, 0, 100);
    repetition.stabilityScore = std::clamp((m_stabilitySum + denominator / 2) / denominator, 0, 100);
    repetition.depthScore = std::clamp((m_depthSum + denominator / 2) / denominator, 0, 100);
    repetition.valid = repetition.score >= m_standard.targetScore;
    repetition.errorCodes = m_issueTitles.join(QStringLiteral("|"));
    repetition.feedback = m_feedback.trimmed().isEmpty() ? assessment.feedback : m_feedback;
    repetition.keyFrameMs = m_keyFrameMs;
    repetition.keyFramePoseJson = poseFrameToJson(m_keyFramePoseFrame);
    return repetition;
}

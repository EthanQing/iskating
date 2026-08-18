#include "athletetracker.h"

#include <QSet>

#include <algorithm>

namespace {

struct TrackCandidate
{
    int detectionIndex = -1;
    int trackId = -1;
    float iou = 0.0f;
};

float intersectionOverUnion(const QRectF &first, const QRectF &second)
{
    const QRectF intersection = first.intersected(second);
    const double intersectionArea = std::max(0.0, intersection.width()) * std::max(0.0, intersection.height());
    const double unionArea = first.width() * first.height() + second.width() * second.height() - intersectionArea;
    return unionArea > 0.0 ? static_cast<float>(intersectionArea / unionArea) : 0.0f;
}

} // namespace

bool shouldAttemptAthleteTrackReid(const AthleteTrackState &track,
                                   const AthleteTrackReidPolicy &policy,
                                   qint64 timestampMs)
{
    if (track.identityStatus == QStringLiteral("identified") || track.hits < policy.minTrackHits) {
        return false;
    }
    if (policy.maxAttempts > 0 && track.reidAttempts >= policy.maxAttempts) {
        return false;
    }
    return track.lastReidAttemptMs <= 0
           || timestampMs - track.lastReidAttemptMs >= policy.retryIntervalMs;
}

void AthleteTracker::reset()
{
    m_tracks.clear();
    m_nextTrackId = 1;
}

void AthleteTracker::setTrackTtlMs(qint64 ttlMs)
{
    m_trackTtlMs = std::max<qint64>(1, ttlMs);
}

void AthleteTracker::setIouThreshold(float threshold)
{
    m_iouThreshold = std::clamp(threshold, 0.0f, 1.0f);
}

QVector<int> AthleteTracker::update(int cameraId, const QVector<QRectF> &boxes, qint64 timestampMs)
{
    QHash<int, AthleteTrackState> &tracks = m_tracks[cameraId];
    for (auto iterator = tracks.begin(); iterator != tracks.end();) {
        if (timestampMs - iterator->lastSeenMs > m_trackTtlMs) {
            iterator = tracks.erase(iterator);
        } else {
            ++iterator;
        }
    }

    QVector<TrackCandidate> candidates;
    for (int detectionIndex = 0; detectionIndex < boxes.size(); ++detectionIndex) {
        for (auto iterator = tracks.cbegin(); iterator != tracks.cend(); ++iterator) {
            const float iou = intersectionOverUnion(boxes.at(detectionIndex), iterator->box);
            if (iou > m_iouThreshold) {
                candidates.append({detectionIndex, iterator.key(), iou});
            }
        }
    }
    std::sort(candidates.begin(), candidates.end(), [](const TrackCandidate &first, const TrackCandidate &second) {
        if (first.iou != second.iou) {
            return first.iou > second.iou;
        }
        if (first.trackId != second.trackId) {
            return first.trackId < second.trackId;
        }
        return first.detectionIndex < second.detectionIndex;
    });

    QVector<int> trackIds(boxes.size(), -1);
    QSet<int> assignedDetections;
    QSet<int> assignedTracks;
    for (const TrackCandidate &candidate : candidates) {
        if (assignedDetections.contains(candidate.detectionIndex) || assignedTracks.contains(candidate.trackId)) {
            continue;
        }
        auto iterator = tracks.find(candidate.trackId);
        if (iterator == tracks.end()) {
            continue;
        }
        iterator->box = boxes.at(candidate.detectionIndex);
        iterator->lastSeenMs = timestampMs;
        ++iterator->hits;
        trackIds[candidate.detectionIndex] = candidate.trackId;
        assignedDetections.insert(candidate.detectionIndex);
        assignedTracks.insert(candidate.trackId);
    }

    for (int detectionIndex = 0; detectionIndex < boxes.size(); ++detectionIndex) {
        if (trackIds.at(detectionIndex) >= 0) {
            continue;
        }
        AthleteTrackState state;
        state.trackId = m_nextTrackId++;
        state.box = boxes.at(detectionIndex);
        state.lastSeenMs = timestampMs;
        state.hits = 1;
        tracks.insert(state.trackId, state);
        trackIds[detectionIndex] = state.trackId;
    }

    return trackIds;
}

AthleteTrackState *AthleteTracker::track(int cameraId, int trackId)
{
    auto cameraTracks = m_tracks.find(cameraId);
    if (cameraTracks == m_tracks.end()) {
        return nullptr;
    }
    auto track = cameraTracks->find(trackId);
    return track == cameraTracks->end() ? nullptr : &track.value();
}

const AthleteTrackState *AthleteTracker::track(int cameraId, int trackId) const
{
    const auto cameraTracks = m_tracks.constFind(cameraId);
    if (cameraTracks == m_tracks.cend()) {
        return nullptr;
    }
    const auto track = cameraTracks->constFind(trackId);
    return track == cameraTracks->cend() ? nullptr : &track.value();
}

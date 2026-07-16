"""Backfill video indexes and offline analysis tasks for an existing PostgreSQL development database."""

from __future__ import annotations

import argparse
import os
from urllib.parse import urlsplit, urlunsplit

import psycopg


def masked_url(url: str) -> str:
    parts = urlsplit(url)
    netloc = parts.netloc
    if "@" in netloc:
        credentials, host = netloc.rsplit("@", 1)
        user = credentials.split(":", 1)[0]
        netloc = f"{user}:***@{host}"
    return urlunsplit((parts.scheme, netloc, parts.path, parts.query, parts.fragment))


def database_url(value: str | None) -> str:
    url = (value or os.getenv("ISKATING_DATABASE_URL") or "").strip()
    if not url:
        raise RuntimeError("ISKATING_DATABASE_URL is required")
    if url.startswith("postgresql+psycopg://"):
        return "postgresql://" + url.removeprefix("postgresql+psycopg://")
    return url


def backfill(url: str) -> dict[str, int]:
    with psycopg.connect(url) as db:
        with db.cursor() as cur:
            cur.execute(
                """
                ALTER TABLE training_video_files
                    ADD COLUMN IF NOT EXISTS session_start_ms integer NOT NULL DEFAULT 0,
                    ADD COLUMN IF NOT EXISTS session_end_ms integer NOT NULL DEFAULT 0,
                    ADD COLUMN IF NOT EXISTS duration_ms integer NOT NULL DEFAULT 0,
                    ADD COLUMN IF NOT EXISTS file_size_bytes bigint,
                    ADD COLUMN IF NOT EXISTS file_modified_at timestamptz,
                    ADD COLUMN IF NOT EXISTS checksum_algorithm text,
                    ADD COLUMN IF NOT EXISTS checksum_value text;

                ALTER TABLE action_repetitions
                    ADD COLUMN IF NOT EXISTS video_file_id uuid,
                    ADD COLUMN IF NOT EXISTS video_index integer NOT NULL DEFAULT 1;

                CREATE TABLE IF NOT EXISTS offline_analysis_tasks (
                    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
                    batch_id uuid,
                    camera_id integer NOT NULL DEFAULT 0,
                    time_offset_ms integer NOT NULL DEFAULT 0,
                    video_path text NOT NULL,
                    file_name text,
                    file_size_bytes bigint,
                    file_modified_at timestamptz,
                    duration_ms integer NOT NULL DEFAULT 0,
                    status text NOT NULL DEFAULT 'imported',
                    probe_metadata jsonb NOT NULL DEFAULT '{}'::jsonb,
                    summary_metadata jsonb NOT NULL DEFAULT '{}'::jsonb,
                    created_at timestamptz NOT NULL DEFAULT now(),
                    updated_at timestamptz NOT NULL DEFAULT now()
                );

                ALTER TABLE training_sessions
                    ADD COLUMN IF NOT EXISTS analysis_task_id uuid;

                CREATE TABLE IF NOT EXISTS training_session_participants (
                    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
                    session_id uuid NOT NULL REFERENCES training_sessions(id) ON DELETE CASCADE,
                    athlete_id uuid NOT NULL REFERENCES athletes(id),
                    slot_index integer NOT NULL,
                    role text NOT NULL DEFAULT 'participant',
                    track_label text,
                    notes text,
                    active boolean NOT NULL DEFAULT true,
                    created_at timestamptz NOT NULL DEFAULT now(),
                    updated_at timestamptz NOT NULL DEFAULT now(),
                    CONSTRAINT ck_training_session_participants_slot CHECK (slot_index >= 1 AND slot_index <= 4),
                    CONSTRAINT ck_training_session_participants_role CHECK (role IN ('primary', 'participant')),
                    CONSTRAINT uq_training_session_participants_athlete UNIQUE (session_id, athlete_id),
                    CONSTRAINT uq_training_session_participants_slot UNIQUE (session_id, slot_index)
                );

                CREATE TABLE IF NOT EXISTS track_points (
                    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
                    participant_id uuid NOT NULL REFERENCES training_session_participants(id) ON DELETE CASCADE,
                    t_ms bigint NOT NULL,
                    x double precision NOT NULL,
                    y double precision NOT NULL,
                    z double precision NOT NULL DEFAULT 0,
                    speed_source text NOT NULL DEFAULT 'position_delta',
                    camera_id integer NOT NULL DEFAULT 0,
                    confidence double precision,
                    created_at timestamptz NOT NULL DEFAULT now(),
                    UNIQUE (participant_id, t_ms, camera_id)
                );
                CREATE INDEX IF NOT EXISTS ix_track_points_participant_time ON track_points(participant_id, t_ms);

                CREATE TABLE IF NOT EXISTS speed_metrics (
                    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
                    participant_id uuid NOT NULL REFERENCES training_session_participants(id) ON DELETE CASCADE,
                    track_point_id uuid NOT NULL REFERENCES track_points(id) ON DELETE CASCADE,
                    t_ms bigint NOT NULL,
                    camera_id integer NOT NULL DEFAULT 0,
                    instantaneous_speed_mps double precision NOT NULL DEFAULT 0,
                    smoothed_speed_mps double precision NOT NULL DEFAULT 0,
                    smoothing_window_ms integer NOT NULL DEFAULT 1000,
                    unit text NOT NULL DEFAULT 'm/s',
                    algorithm_version text NOT NULL DEFAULT 'trajectory_speed_v1',
                    valid boolean NOT NULL DEFAULT false,
                    created_at timestamptz NOT NULL DEFAULT now(),
                    UNIQUE (track_point_id)
                );
                CREATE INDEX IF NOT EXISTS ix_speed_metrics_participant_time ON speed_metrics(participant_id, t_ms);

                CREATE TABLE IF NOT EXISTS participant_repetitions (
                    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
                    session_id uuid NOT NULL REFERENCES training_sessions(id) ON DELETE CASCADE,
                    participant_id uuid REFERENCES training_session_participants(id) ON DELETE SET NULL,
                    athlete_id uuid REFERENCES athletes(id),
                    action_repetition_id uuid REFERENCES action_repetitions(id) ON DELETE SET NULL,
                    action_standard_id uuid NOT NULL REFERENCES action_standards(id),
                    standard_version integer NOT NULL DEFAULT 1,
                    started_ms integer NOT NULL DEFAULT 0,
                    ended_ms integer NOT NULL DEFAULT 0,
                    valid boolean NOT NULL DEFAULT false,
                    score integer NOT NULL DEFAULT 0,
                    scores jsonb NOT NULL DEFAULT '{}'::jsonb,
                    error_codes jsonb,
                    feedback text,
                    key_frame_ms integer NOT NULL DEFAULT 0,
                    video_file_id uuid REFERENCES training_video_files(id) ON DELETE SET NULL,
                    video_index integer NOT NULL DEFAULT 1,
                    video_clip_start_ms integer NOT NULL DEFAULT 0,
                    video_clip_end_ms integer NOT NULL DEFAULT 0,
                    source text NOT NULL DEFAULT 'ai',
                    review_status text NOT NULL DEFAULT 'unreviewed',
                    reviewer_coach_id uuid REFERENCES coaches(id),
                    reviewed_at timestamptz,
                    manual_started_ms integer,
                    manual_ended_ms integer,
                    manual_valid boolean,
                    manual_score integer,
                    manual_scores jsonb,
                    manual_error_codes jsonb,
                    manual_feedback text,
                    coach_note text,
                    key_frame_pose jsonb,
                    track_id integer,
                    camera_id integer,
                    frame_time_ms bigint,
                    identity_status text NOT NULL DEFAULT 'unknown',
                    identity_confidence double precision,
                    identity_source text
                );

                DO $$
                BEGIN
                    IF NOT EXISTS (
                        SELECT 1 FROM pg_constraint WHERE conname = 'fk_action_repetitions_video_file'
                    ) THEN
                        ALTER TABLE action_repetitions
                            ADD CONSTRAINT fk_action_repetitions_video_file
                            FOREIGN KEY (video_file_id) REFERENCES training_video_files(id) ON DELETE SET NULL;
                    END IF;
                    IF NOT EXISTS (
                        SELECT 1 FROM pg_constraint WHERE conname = 'fk_training_sessions_analysis_task'
                    ) THEN
                        ALTER TABLE training_sessions
                            ADD CONSTRAINT fk_training_sessions_analysis_task
                            FOREIGN KEY (analysis_task_id) REFERENCES offline_analysis_tasks(id) ON DELETE SET NULL;
                    END IF;
                END $$;

                CREATE INDEX IF NOT EXISTS ix_action_repetitions_video_file
                    ON action_repetitions(video_file_id);
                CREATE INDEX IF NOT EXISTS ix_training_sessions_analysis_task
                    ON training_sessions(analysis_task_id);
                CREATE INDEX IF NOT EXISTS ix_offline_analysis_tasks_batch
                    ON offline_analysis_tasks(batch_id);
                CREATE INDEX IF NOT EXISTS ix_offline_analysis_tasks_video_path
                    ON offline_analysis_tasks(video_path);
                CREATE INDEX IF NOT EXISTS ix_participant_repetitions_session
                    ON participant_repetitions(session_id);
                CREATE INDEX IF NOT EXISTS ix_participant_repetitions_participant
                    ON participant_repetitions(participant_id);
                CREATE INDEX IF NOT EXISTS ix_participant_repetitions_athlete
                    ON participant_repetitions(athlete_id);
                CREATE INDEX IF NOT EXISTS ix_participant_repetitions_action_repetition
                    ON participant_repetitions(action_repetition_id);
                """
            )
            cur.execute(
                """
                INSERT INTO offline_analysis_tasks
                (id, batch_id, camera_id, time_offset_ms, video_path, file_name, file_size_bytes,
                 file_modified_at, duration_ms, status, probe_metadata, summary_metadata, updated_at)
                SELECT gen_random_uuid(), gen_random_uuid(), 0, 0, ts.video_source,
                       regexp_replace(ts.video_source, '^.*[\\\\/]', ''),
                       tvf.file_size_bytes, tvf.file_modified_at,
                       GREATEST(0, ts.duration_sec * 1000), 'completed',
                       jsonb_build_object('legacyBackfill', true),
                       jsonb_build_object('mode', 'single_video', 'multiVideoReserved', true),
                       now()
                FROM training_sessions ts
                LEFT JOIN training_video_files tvf ON tvf.session_id = ts.id AND tvf.video_index = 1
                WHERE ts.camera = 0
                  AND COALESCE(ts.video_source, '') <> ''
                  AND COALESCE(ts.video_source, '') NOT LIKE '%://%'
                  AND ts.analysis_task_id IS NULL
                """
            )
            inserted_analysis_tasks = cur.rowcount
            cur.execute(
                """
                UPDATE training_sessions ts
                SET analysis_task_id = oat.id,
                    source_type = 'offline_import',
                    source_ref = oat.id::text
                FROM offline_analysis_tasks oat
                WHERE ts.camera = 0
                  AND COALESCE(ts.video_source, '') = oat.video_path
                  AND ts.analysis_task_id IS NULL
                """
            )
            updated_sessions = cur.rowcount
            cur.execute(
                """
                INSERT INTO training_video_files
                (id, session_id, video_index, camera, camera_name, source_url, fallback_url,
                 file_name, file_path, status, session_start_ms, session_end_ms, duration_ms,
                 metadata, updated_at)
                SELECT gen_random_uuid(), ts.id, 1, ts.camera, ts.video_camera_name, ts.video_source,
                       ts.video_fallback_source,
                       CASE
                           WHEN ts.camera = 0 AND COALESCE(ts.video_source, '') NOT LIKE '%://%'
                               THEN regexp_replace(ts.video_source, '^.*[\\\\/]', '')
                           ELSE NULL
                       END,
                       CASE
                           WHEN ts.camera = 0 AND COALESCE(ts.video_source, '') NOT LIKE '%://%'
                               THEN ts.video_source
                           ELSE NULL
                       END,
                       CASE
                           WHEN ts.camera = 0 AND COALESCE(ts.video_source, '') NOT LIKE '%://%'
                               THEN 'external'
                           ELSE 'planned'
                       END,
                       0,
                       GREATEST(0, ts.duration_sec * 1000),
                       GREATEST(0, ts.duration_sec * 1000),
                       jsonb_build_object(
                           'sessionId', ts.id::text,
                           'videoIndex', 1,
                           'camera', ts.camera,
                           'cameraName', COALESCE(ts.video_camera_name, ''),
                           'sourceUrl', COALESCE(ts.video_source, ''),
                           'fallbackUrl', COALESCE(ts.video_fallback_source, ''),
                           'status', CASE
                               WHEN ts.camera = 0 AND COALESCE(ts.video_source, '') NOT LIKE '%://%'
                                   THEN 'external'
                               ELSE 'planned'
                           END,
                           'backfill', true
                       ),
                       now()
                FROM training_sessions ts
                WHERE (COALESCE(ts.video_source, '') <> '' OR COALESCE(ts.video_fallback_source, '') <> '')
                  AND NOT EXISTS (
                      SELECT 1 FROM training_video_files tvf
                      WHERE tvf.session_id = ts.id AND tvf.video_index = 1
                  )
                """
            )
            inserted_video_files = cur.rowcount
            cur.execute(
                """
                UPDATE training_video_files tvf
                SET session_start_ms = 0,
                    session_end_ms = GREATEST(tvf.session_end_ms, ts.duration_sec * 1000),
                    duration_ms = GREATEST(tvf.duration_ms, ts.duration_sec * 1000),
                    updated_at = now()
                FROM training_sessions ts
                WHERE tvf.session_id = ts.id
                  AND tvf.video_index = 1
                  AND (tvf.session_end_ms = 0 OR tvf.duration_ms = 0)
                """
            )
            updated_video_files = cur.rowcount
            cur.execute(
                """
                UPDATE action_repetitions ar
                SET video_index = COALESCE(NULLIF(ar.video_index, 0), 1),
                    video_file_id = tvf.id
                FROM training_video_files tvf
                WHERE ar.session_id = tvf.session_id
                  AND tvf.video_index = COALESCE(NULLIF(ar.video_index, 0), 1)
                  AND ar.video_file_id IS NULL
                """
            )
            updated_repetitions = cur.rowcount
            cur.execute(
                """
                INSERT INTO training_session_participants
                (id, session_id, athlete_id, slot_index, role, active, updated_at)
                SELECT gen_random_uuid(), ts.id, ts.athlete_id, 1, 'primary', true, now()
                FROM training_sessions ts
                WHERE NOT EXISTS (
                    SELECT 1 FROM training_session_participants tsp
                    WHERE tsp.session_id = ts.id AND tsp.athlete_id = ts.athlete_id
                )
                """
            )
            inserted_participants = cur.rowcount
            cur.execute(
                """
                INSERT INTO participant_repetitions
                (id, session_id, participant_id, athlete_id, action_repetition_id, action_standard_id,
                 standard_version, started_ms, ended_ms, valid, score, scores, error_codes, feedback,
                 key_frame_ms, video_file_id, video_index, video_clip_start_ms, video_clip_end_ms,
                 source, review_status, reviewer_coach_id, reviewed_at, manual_started_ms, manual_ended_ms,
                 manual_valid, manual_score, manual_scores, manual_error_codes, manual_feedback, coach_note,
                 key_frame_pose, track_id, camera_id, frame_time_ms, identity_status, identity_confidence, identity_source)
                SELECT gen_random_uuid(), ar.session_id, tsp.id, COALESCE(ar.athlete_id, ts.athlete_id), ar.id,
                       ar.action_standard_id, ar.standard_version, ar.started_ms, ar.ended_ms, ar.valid,
                       ar.score, ar.scores, ar.error_codes, ar.feedback, ar.key_frame_ms, ar.video_file_id,
                       ar.video_index, ar.video_clip_start_ms, ar.video_clip_end_ms, ar.source, ar.review_status,
                       ar.reviewer_coach_id, ar.reviewed_at, ar.manual_started_ms, ar.manual_ended_ms,
                       ar.manual_valid, ar.manual_score, ar.manual_scores, ar.manual_error_codes,
                       ar.manual_feedback, ar.coach_note, ar.key_frame_pose, ar.track_id, ar.camera_id,
                       ar.frame_time_ms, ar.identity_status, ar.identity_confidence, ar.identity_source
                FROM action_repetitions ar
                JOIN training_sessions ts ON ts.id = ar.session_id
                LEFT JOIN training_session_participants tsp ON tsp.session_id = ar.session_id
                     AND tsp.athlete_id = COALESCE(ar.athlete_id, ts.athlete_id)
                WHERE NOT EXISTS (
                    SELECT 1 FROM participant_repetitions pr WHERE pr.action_repetition_id = ar.id
                )
                """
            )
            inserted_participant_repetitions = cur.rowcount
        db.commit()
    return {
        "inserted_analysis_tasks": inserted_analysis_tasks,
        "updated_sessions": updated_sessions,
        "inserted_video_files": inserted_video_files,
        "updated_video_files": updated_video_files,
        "updated_repetitions": updated_repetitions,
        "inserted_participants": inserted_participants,
        "inserted_participant_repetitions": inserted_participant_repetitions,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Backfill video file indexes and offline analysis tasks for PostgreSQL.")
    parser.add_argument("--database-url", default=None, help="PostgreSQL URL; defaults to ISKATING_DATABASE_URL.")
    args = parser.parse_args()
    url = database_url(args.database_url)
    print(f"Target database: {masked_url(url)}")
    counts = backfill(url)
    for key, value in counts.items():
        print(f"{key}: {value}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

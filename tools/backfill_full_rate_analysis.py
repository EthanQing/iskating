"""Add full-rate offline analysis batches, runs, sources, and chunk indexes."""

from __future__ import annotations

import argparse

import psycopg

from backfill_video_indexes import database_url, masked_url


MIGRATION_SQL = """
CREATE TABLE IF NOT EXISTS offline_analysis_batches (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(), status text NOT NULL DEFAULT 'imported',
    source_started_at timestamptz, active_run_id uuid, metadata jsonb NOT NULL DEFAULT '{}'::jsonb,
    created_at timestamptz NOT NULL DEFAULT now(), updated_at timestamptz NOT NULL DEFAULT now()
);

INSERT INTO offline_analysis_batches (id, status, metadata)
SELECT DISTINCT batch_id, 'imported', jsonb_build_object('legacyBackfill', true)
FROM offline_analysis_tasks WHERE batch_id IS NOT NULL
ON CONFLICT (id) DO NOTHING;

INSERT INTO offline_analysis_batches (id, status, metadata)
SELECT gen_random_uuid(), 'imported', jsonb_build_object('legacyBackfill', true, 'taskId', id::text)
FROM offline_analysis_tasks WHERE batch_id IS NULL;

UPDATE offline_analysis_tasks task
SET batch_id = batch.id
FROM offline_analysis_batches batch
WHERE task.batch_id IS NULL AND batch.metadata->>'taskId' = task.id::text;

CREATE TABLE IF NOT EXISTS offline_analysis_runs (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    batch_id uuid NOT NULL REFERENCES offline_analysis_batches(id) ON DELETE CASCADE,
    status text NOT NULL DEFAULT 'queued', model_version text NOT NULL, preprocessing_version text NOT NULL,
    gallery_snapshot_hash text, configuration jsonb NOT NULL DEFAULT '{}'::jsonb,
    total_frames bigint NOT NULL DEFAULT 0, processed_frames bigint NOT NULL DEFAULT 0,
    error_message text, worker_id text, lease_expires_at timestamptz,
    cancel_requested boolean NOT NULL DEFAULT false, artifact_root_uri text NOT NULL,
    started_at timestamptz, completed_at timestamptz,
    created_at timestamptz NOT NULL DEFAULT now(), updated_at timestamptz NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS offline_analysis_run_sources (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    run_id uuid NOT NULL REFERENCES offline_analysis_runs(id) ON DELETE CASCADE,
    task_id uuid NOT NULL REFERENCES offline_analysis_tasks(id) ON DELETE CASCADE,
    camera_id integer NOT NULL, source_uri text NOT NULL, source_started_at timestamptz,
    manual_correction_ms integer NOT NULL DEFAULT 0, status text NOT NULL DEFAULT 'queued',
    total_frames bigint NOT NULL DEFAULT 0, processed_frames bigint NOT NULL DEFAULT 0,
    last_frame_index bigint NOT NULL DEFAULT -1, last_pts_ms bigint, completed_through_ms bigint,
    error_message text, retry_count integer NOT NULL DEFAULT 0,
    created_at timestamptz NOT NULL DEFAULT now(), updated_at timestamptz NOT NULL DEFAULT now(),
    UNIQUE (run_id, task_id), UNIQUE (run_id, camera_id)
);

CREATE TABLE IF NOT EXISTS offline_analysis_result_chunks (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    run_source_id uuid NOT NULL REFERENCES offline_analysis_run_sources(id) ON DELETE CASCADE,
    start_frame_index bigint NOT NULL, end_frame_index bigint NOT NULL,
    start_pts_ms bigint NOT NULL, end_pts_ms bigint NOT NULL,
    frame_count integer NOT NULL, object_count integer NOT NULL DEFAULT 0,
    artifact_uri text NOT NULL, checksum_sha256 text NOT NULL, schema_version integer NOT NULL DEFAULT 1,
    created_at timestamptz NOT NULL DEFAULT now(), UNIQUE (run_source_id, start_frame_index)
);

ALTER TABLE training_sessions
    ADD COLUMN IF NOT EXISTS analysis_batch_id uuid,
    ADD COLUMN IF NOT EXISTS analysis_run_id uuid;

DO $$
BEGIN
    IF NOT EXISTS (SELECT 1 FROM pg_constraint WHERE conname='fk_offline_analysis_tasks_batch') THEN
        ALTER TABLE offline_analysis_tasks ADD CONSTRAINT fk_offline_analysis_tasks_batch
            FOREIGN KEY (batch_id) REFERENCES offline_analysis_batches(id) ON DELETE CASCADE;
    END IF;
    IF NOT EXISTS (SELECT 1 FROM pg_constraint WHERE conname='fk_offline_analysis_batches_active_run') THEN
        ALTER TABLE offline_analysis_batches ADD CONSTRAINT fk_offline_analysis_batches_active_run
            FOREIGN KEY (active_run_id) REFERENCES offline_analysis_runs(id) ON DELETE SET NULL;
    END IF;
    IF NOT EXISTS (SELECT 1 FROM pg_constraint WHERE conname='fk_training_sessions_analysis_batch') THEN
        ALTER TABLE training_sessions ADD CONSTRAINT fk_training_sessions_analysis_batch
            FOREIGN KEY (analysis_batch_id) REFERENCES offline_analysis_batches(id) ON DELETE SET NULL;
    END IF;
    IF NOT EXISTS (SELECT 1 FROM pg_constraint WHERE conname='fk_training_sessions_analysis_run') THEN
        ALTER TABLE training_sessions ADD CONSTRAINT fk_training_sessions_analysis_run
            FOREIGN KEY (analysis_run_id) REFERENCES offline_analysis_runs(id) ON DELETE SET NULL;
    END IF;
END $$;

UPDATE training_sessions session
SET analysis_batch_id=task.batch_id
FROM offline_analysis_tasks task
WHERE session.analysis_task_id=task.id AND session.analysis_batch_id IS NULL;

CREATE INDEX IF NOT EXISTS ix_training_sessions_analysis_batch ON training_sessions(analysis_batch_id);
CREATE INDEX IF NOT EXISTS ix_training_sessions_analysis_run ON training_sessions(analysis_run_id);
CREATE INDEX IF NOT EXISTS ix_offline_analysis_runs_batch ON offline_analysis_runs(batch_id, created_at DESC);
CREATE INDEX IF NOT EXISTS ix_offline_analysis_runs_status ON offline_analysis_runs(status, created_at);
CREATE INDEX IF NOT EXISTS ix_offline_analysis_run_sources_run ON offline_analysis_run_sources(run_id, camera_id);
CREATE INDEX IF NOT EXISTS ix_offline_analysis_result_chunks_source_pts
    ON offline_analysis_result_chunks(run_source_id, start_pts_ms, end_pts_ms);
"""


def migrate(url: str) -> None:
    with psycopg.connect(url) as connection:
        with connection.cursor() as cursor:
            cursor.execute(MIGRATION_SQL)
        connection.commit()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--database-url")
    args = parser.parse_args()
    url = database_url(args.database_url)
    print(f"Migrating {masked_url(url)}")
    migrate(url)
    print("Full-rate analysis schema is ready.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

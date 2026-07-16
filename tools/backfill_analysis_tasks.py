"""Add generic analysis task tracking to an existing iSkating PostgreSQL database."""

from __future__ import annotations

import argparse

import psycopg

from backfill_video_indexes import database_url, masked_url


MIGRATION_SQL = """
CREATE TABLE IF NOT EXISTS analysis_tasks (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    type text NOT NULL CHECK (type IN ('offline_import', 'full_rate_batch')),
    status text NOT NULL DEFAULT 'queued' CHECK (status IN ('queued', 'running', 'paused', 'completed', 'failed', 'cancelled')),
    progress double precision NOT NULL DEFAULT 0 CHECK (progress >= 0 AND progress <= 100),
    input jsonb NOT NULL DEFAULT '{}'::jsonb,
    output_session_id uuid,
    error text,
    created_at timestamptz NOT NULL DEFAULT now(),
    updated_at timestamptz NOT NULL DEFAULT now()
);

ALTER TABLE offline_analysis_tasks ADD COLUMN IF NOT EXISTS analysis_task_id uuid;
ALTER TABLE offline_analysis_batches ADD COLUMN IF NOT EXISTS analysis_task_id uuid;

DO $$
BEGIN
    IF EXISTS (SELECT 1 FROM pg_constraint WHERE conname='analysis_tasks_status_check') THEN
        ALTER TABLE analysis_tasks DROP CONSTRAINT analysis_tasks_status_check;
    END IF;
    IF EXISTS (SELECT 1 FROM pg_constraint WHERE conname='analysis_tasks_status_check1') THEN
        ALTER TABLE analysis_tasks DROP CONSTRAINT analysis_tasks_status_check1;
    END IF;
    IF NOT EXISTS (SELECT 1 FROM pg_constraint WHERE conname='ck_analysis_tasks_status') THEN
        ALTER TABLE analysis_tasks ADD CONSTRAINT ck_analysis_tasks_status
            CHECK (status IN ('queued', 'running', 'paused', 'completed', 'failed', 'cancelled'));
    END IF;
END $$;

INSERT INTO analysis_tasks (id, type, status, progress, input, created_at, updated_at)
SELECT gen_random_uuid(), 'offline_import',
       CASE WHEN task.status='completed' THEN 'completed' WHEN task.status='failed' THEN 'failed' ELSE 'queued' END,
       CASE WHEN task.status='completed' THEN 100 ELSE 0 END,
       jsonb_build_object('offlineTaskId', task.id::text, 'videoPath', task.video_path, 'fileName', COALESCE(task.file_name, '')),
       task.created_at, task.updated_at
FROM offline_analysis_tasks task WHERE task.analysis_task_id IS NULL;

UPDATE offline_analysis_tasks task SET analysis_task_id = generic.id
FROM analysis_tasks generic
WHERE task.analysis_task_id IS NULL AND generic.input->>'offlineTaskId' = task.id::text;

INSERT INTO analysis_tasks (id, type, status, progress, input)
SELECT gen_random_uuid(), 'full_rate_batch', 'queued', 0,
       jsonb_build_object('batchId', batch.id::text, 'mode', 'synchronized_12_camera')
FROM offline_analysis_batches batch WHERE batch.analysis_task_id IS NULL;

UPDATE offline_analysis_batches batch SET analysis_task_id = generic.id
FROM analysis_tasks generic
WHERE batch.analysis_task_id IS NULL AND generic.input->>'batchId' = batch.id::text;

UPDATE analysis_tasks generic SET output_session_id = session.id, status = 'completed', progress = 100, updated_at = now()
FROM training_sessions session
LEFT JOIN offline_analysis_tasks offline ON offline.id = session.analysis_task_id
LEFT JOIN offline_analysis_batches batch ON batch.id = session.analysis_batch_id
WHERE generic.id = COALESCE(offline.analysis_task_id, batch.analysis_task_id)
  AND generic.output_session_id IS NULL;

DO $$
BEGIN
    IF NOT EXISTS (SELECT 1 FROM pg_constraint WHERE conname='fk_offline_analysis_tasks_analysis_task') THEN
        ALTER TABLE offline_analysis_tasks ADD CONSTRAINT fk_offline_analysis_tasks_analysis_task
            FOREIGN KEY (analysis_task_id) REFERENCES analysis_tasks(id) ON DELETE SET NULL;
    END IF;
    IF NOT EXISTS (SELECT 1 FROM pg_constraint WHERE conname='fk_offline_analysis_batches_analysis_task') THEN
        ALTER TABLE offline_analysis_batches ADD CONSTRAINT fk_offline_analysis_batches_analysis_task
            FOREIGN KEY (analysis_task_id) REFERENCES analysis_tasks(id) ON DELETE SET NULL;
    END IF;
END $$;

CREATE INDEX IF NOT EXISTS ix_offline_analysis_tasks_analysis_task ON offline_analysis_tasks(analysis_task_id);
CREATE INDEX IF NOT EXISTS ix_offline_analysis_batches_analysis_task ON offline_analysis_batches(analysis_task_id);
CREATE INDEX IF NOT EXISTS ix_analysis_tasks_type_status ON analysis_tasks(type, status, created_at DESC);
CREATE INDEX IF NOT EXISTS ix_analysis_tasks_output_session ON analysis_tasks(output_session_id);
"""


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--database-url")
    args = parser.parse_args()
    url = database_url(args.database_url)
    print(f"Migrating {masked_url(url)}")
    with psycopg.connect(url) as connection:
        with connection.cursor() as cursor:
            cursor.execute(MIGRATION_SQL)
        connection.commit()
    print("Generic analysis task schema is ready.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

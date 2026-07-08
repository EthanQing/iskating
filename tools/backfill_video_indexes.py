"""Backfill F-10 video indexes for an existing PostgreSQL development database."""

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

                DO $$
                BEGIN
                    IF NOT EXISTS (
                        SELECT 1 FROM pg_constraint WHERE conname = 'fk_action_repetitions_video_file'
                    ) THEN
                        ALTER TABLE action_repetitions
                            ADD CONSTRAINT fk_action_repetitions_video_file
                            FOREIGN KEY (video_file_id) REFERENCES training_video_files(id) ON DELETE SET NULL;
                    END IF;
                END $$;

                CREATE INDEX IF NOT EXISTS ix_action_repetitions_video_file
                    ON action_repetitions(video_file_id);
                """
            )
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
        db.commit()
    return {
        "inserted_video_files": inserted_video_files,
        "updated_video_files": updated_video_files,
        "updated_repetitions": updated_repetitions,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Backfill F-10 video file indexes for PostgreSQL.")
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

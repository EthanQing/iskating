"""Add the F-26 joint metric table to an existing iSkating PostgreSQL database."""

from __future__ import annotations

import os

from sqlalchemy import create_engine, text


def main() -> None:
    url = os.environ.get("ISKATING_DATABASE_URL")
    if not url:
        raise SystemExit("ISKATING_DATABASE_URL is required")
    engine = create_engine(url)
    statements = (
        """CREATE TABLE IF NOT EXISTS joint_metrics (
            id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
            participant_id uuid NOT NULL REFERENCES training_session_participants(id) ON DELETE CASCADE,
            t_ms bigint NOT NULL, camera_id integer NOT NULL DEFAULT 0,
            joint text NOT NULL, side text NOT NULL CHECK (side IN ('left', 'right')),
            angle_deg double precision NOT NULL DEFAULT 0,
            angular_velocity_deg_per_sec double precision NOT NULL DEFAULT 0,
            valid boolean NOT NULL DEFAULT false, confidence double precision,
            algorithm_version text NOT NULL, created_at timestamptz NOT NULL DEFAULT now(),
            UNIQUE (participant_id, t_ms, camera_id, joint, side)
        )""",
        "CREATE INDEX IF NOT EXISTS ix_joint_metrics_participant_time ON joint_metrics(participant_id, t_ms)",
        "CREATE INDEX IF NOT EXISTS ix_joint_metrics_filter ON joint_metrics(participant_id, joint, side, t_ms)",
    )
    with engine.begin() as connection:
        for statement in statements:
            connection.execute(text(statement))


if __name__ == "__main__":
    main()

"""initial PostgreSQL schema

Revision ID: 20260630_0001
Revises:
Create Date: 2026-06-30
"""

from alembic import op
import sqlalchemy as sa
from sqlalchemy.dialects import postgresql

revision = "20260630_0001"
down_revision = None
branch_labels = None
depends_on = None


def uuid_pk():
    return sa.Column("id", postgresql.UUID(as_uuid=True), primary_key=True)


def timestamps():
    return (
        sa.Column("created_at", sa.DateTime(timezone=True), nullable=False, server_default=sa.text("now()")),
        sa.Column("updated_at", sa.DateTime(timezone=True), nullable=False, server_default=sa.text("now()")),
    )


def upgrade() -> None:
    op.execute('CREATE EXTENSION IF NOT EXISTS "pgcrypto"')

    op.create_table(
        "users",
        uuid_pk(),
        sa.Column("username", sa.Text(), nullable=False, unique=True),
        sa.Column("password_hash", sa.Text(), nullable=False),
        sa.Column("role", sa.Text(), nullable=False, server_default="coach"),
        sa.Column("active", sa.Boolean(), nullable=False, server_default=sa.text("true")),
        *timestamps(),
    )

    op.create_table(
        "athletes",
        uuid_pk(),
        sa.Column("name", sa.Text(), nullable=False),
        sa.Column("code", sa.Text()),
        sa.Column("age_group", sa.Text()),
        sa.Column("height_cm", sa.Float(), nullable=False, server_default="0"),
        sa.Column("weight_kg", sa.Float(), nullable=False, server_default="0"),
        sa.Column("discipline", sa.Text()),
        sa.Column("level", sa.Text()),
        sa.Column("preferred_rotation", sa.Text()),
        sa.Column("preferred_takeoff_foot", sa.Text()),
        sa.Column("injury_notes", sa.Text()),
        sa.Column("goals", sa.Text()),
        sa.Column("active", sa.Boolean(), nullable=False, server_default=sa.text("true")),
        *timestamps(),
    )

    op.create_table(
        "coaches",
        uuid_pk(),
        sa.Column("name", sa.Text(), nullable=False),
        sa.Column("code", sa.Text()),
        sa.Column("specialty", sa.Text()),
        sa.Column("phone", sa.Text()),
        sa.Column("notes", sa.Text()),
        sa.Column("active", sa.Boolean(), nullable=False, server_default=sa.text("true")),
        *timestamps(),
    )

    op.create_table(
        "coach_athletes",
        sa.Column("coach_id", postgresql.UUID(as_uuid=True), sa.ForeignKey("coaches.id"), nullable=False),
        sa.Column("athlete_id", postgresql.UUID(as_uuid=True), sa.ForeignKey("athletes.id"), nullable=False),
        sa.Column("created_at", sa.DateTime(timezone=True), nullable=False, server_default=sa.text("now()")),
        sa.PrimaryKeyConstraint("coach_id", "athlete_id"),
    )

    op.create_table(
        "action_categories",
        uuid_pk(),
        sa.Column("code", sa.Text(), nullable=False, unique=True),
        sa.Column("name", sa.Text(), nullable=False),
        sa.Column("sort_order", sa.Integer(), nullable=False, server_default="0"),
    )

    op.create_table(
        "action_standards",
        uuid_pk(),
        sa.Column("code", sa.Text(), nullable=False, unique=True),
        sa.Column("name", sa.Text(), nullable=False),
        sa.Column("category_id", postgresql.UUID(as_uuid=True), sa.ForeignKey("action_categories.id"), nullable=False),
        sa.Column("level", sa.Text()),
        sa.Column("purpose", sa.Text()),
        sa.Column("version", sa.Integer(), nullable=False, server_default="1"),
        sa.Column("target_reps", sa.Integer(), nullable=False, server_default="10"),
        sa.Column("target_score", sa.Integer(), nullable=False, server_default="80"),
        sa.Column("set_count", sa.Integer(), nullable=False, server_default="1"),
        sa.Column("rest_seconds", sa.Integer(), nullable=False, server_default="60"),
        sa.Column("arm_threshold", sa.Float(), nullable=False, server_default="0.28"),
        sa.Column("release_threshold", sa.Float(), nullable=False, server_default="0.18"),
        sa.Column("debounce_ms", sa.Integer(), nullable=False, server_default="900"),
        sa.Column("weights", postgresql.JSONB(), nullable=False, server_default=sa.text("'{}'::jsonb")),
        sa.Column("minimums", postgresql.JSONB(), nullable=False, server_default=sa.text("'{}'::jsonb")),
        sa.Column("phases", postgresql.JSONB()),
        sa.Column("key_points", postgresql.JSONB()),
        sa.Column("issue", postgresql.JSONB(), nullable=False, server_default=sa.text("'{}'::jsonb")),
        sa.Column("reference_video_source", sa.Text()),
        sa.Column("reference_repetition_id", postgresql.UUID(as_uuid=True)),
        sa.Column("reference_notes", sa.Text()),
        sa.Column("active", sa.Boolean(), nullable=False, server_default=sa.text("true")),
        *timestamps(),
    )

    op.create_table(
        "training_plans",
        uuid_pk(),
        sa.Column("athlete_id", postgresql.UUID(as_uuid=True), sa.ForeignKey("athletes.id"), nullable=False),
        sa.Column("coach_id", postgresql.UUID(as_uuid=True), sa.ForeignKey("coaches.id")),
        sa.Column("name", sa.Text(), nullable=False),
        sa.Column("training_date", sa.Date(), nullable=False),
        sa.Column("site", sa.Text()),
        sa.Column("training_phase", sa.Text()),
        sa.Column("goal", sa.Text()),
        sa.Column("status", sa.Text(), nullable=False, server_default="active"),
        *timestamps(),
    )

    op.create_table(
        "training_tasks",
        uuid_pk(),
        sa.Column("plan_id", postgresql.UUID(as_uuid=True), sa.ForeignKey("training_plans.id"), nullable=False),
        sa.Column("action_standard_id", postgresql.UUID(as_uuid=True), sa.ForeignKey("action_standards.id"), nullable=False),
        sa.Column("standard_version", sa.Integer(), nullable=False, server_default="1"),
        sa.Column("target_reps", sa.Integer(), nullable=False, server_default="10"),
        sa.Column("target_score", sa.Integer(), nullable=False, server_default="80"),
        sa.Column("set_count", sa.Integer(), nullable=False, server_default="1"),
        sa.Column("rest_seconds", sa.Integer(), nullable=False, server_default="60"),
        sa.Column("status", sa.Text(), nullable=False, server_default="active"),
        sa.Column("sort_order", sa.Integer(), nullable=False, server_default="0"),
        *timestamps(),
    )

    op.create_table(
        "training_sessions",
        uuid_pk(),
        sa.Column("athlete_id", postgresql.UUID(as_uuid=True), sa.ForeignKey("athletes.id"), nullable=False),
        sa.Column("coach_id", postgresql.UUID(as_uuid=True), sa.ForeignKey("coaches.id")),
        sa.Column("plan_id", postgresql.UUID(as_uuid=True), sa.ForeignKey("training_plans.id")),
        sa.Column("task_id", postgresql.UUID(as_uuid=True), sa.ForeignKey("training_tasks.id")),
        sa.Column("action_standard_id", postgresql.UUID(as_uuid=True), sa.ForeignKey("action_standards.id"), nullable=False),
        sa.Column("standard_version", sa.Integer(), nullable=False, server_default="1"),
        sa.Column("legacy_qsettings_id", sa.BigInteger(), nullable=False, server_default="0"),
        sa.Column("started_at", sa.DateTime(timezone=True), nullable=False),
        sa.Column("saved_at", sa.DateTime(timezone=True), nullable=False),
        sa.Column("duration_sec", sa.Integer(), nullable=False, server_default="0"),
        sa.Column("total_reps", sa.Integer(), nullable=False, server_default="0"),
        sa.Column("valid_reps", sa.Integer(), nullable=False, server_default="0"),
        sa.Column("average_score", sa.Integer(), nullable=False, server_default="0"),
        sa.Column("best_score", sa.Integer(), nullable=False, server_default="0"),
        sa.Column("camera", sa.Integer(), nullable=False, server_default="1"),
        sa.Column("model_precision", sa.Text()),
        sa.Column("fps", sa.Integer(), nullable=False, server_default="30"),
        sa.Column("scores", postgresql.JSONB(), nullable=False, server_default=sa.text("'{}'::jsonb")),
        sa.Column("site", sa.Text()),
        sa.Column("training_phase", sa.Text()),
        sa.Column("goal", sa.Text()),
        sa.Column("target_reps", sa.Integer(), nullable=False, server_default="0"),
        sa.Column("target_score", sa.Integer(), nullable=False, server_default="0"),
        sa.Column("set_count", sa.Integer(), nullable=False, server_default="1"),
        sa.Column("rest_seconds", sa.Integer(), nullable=False, server_default="60"),
        sa.Column("video_source", sa.Text()),
        sa.Column("video_fallback_source", sa.Text()),
        sa.Column("video_camera_name", sa.Text()),
        sa.Column("feedback", sa.Text()),
        sa.Column("notes", sa.Text()),
        sa.Column("coach_comment", sa.Text()),
    )

    op.create_table(
        "action_repetitions",
        uuid_pk(),
        sa.Column("session_id", postgresql.UUID(as_uuid=True), sa.ForeignKey("training_sessions.id", ondelete="CASCADE"), nullable=False),
        sa.Column("action_standard_id", postgresql.UUID(as_uuid=True), sa.ForeignKey("action_standards.id"), nullable=False),
        sa.Column("standard_version", sa.Integer(), nullable=False, server_default="1"),
        sa.Column("started_ms", sa.Integer(), nullable=False, server_default="0"),
        sa.Column("ended_ms", sa.Integer(), nullable=False, server_default="0"),
        sa.Column("valid", sa.Boolean(), nullable=False, server_default=sa.text("false")),
        sa.Column("score", sa.Integer(), nullable=False, server_default="0"),
        sa.Column("scores", postgresql.JSONB(), nullable=False, server_default=sa.text("'{}'::jsonb")),
        sa.Column("error_codes", postgresql.JSONB()),
        sa.Column("feedback", sa.Text()),
        sa.Column("key_frame_ms", sa.Integer(), nullable=False, server_default="0"),
        sa.Column("video_clip_start_ms", sa.Integer(), nullable=False, server_default="0"),
        sa.Column("video_clip_end_ms", sa.Integer(), nullable=False, server_default="0"),
        sa.Column("source", sa.Text(), nullable=False, server_default="ai"),
        sa.Column("review_status", sa.Text(), nullable=False, server_default="unreviewed"),
        sa.Column("reviewer_coach_id", postgresql.UUID(as_uuid=True), sa.ForeignKey("coaches.id")),
        sa.Column("reviewed_at", sa.DateTime(timezone=True)),
        sa.Column("manual_started_ms", sa.Integer()),
        sa.Column("manual_ended_ms", sa.Integer()),
        sa.Column("manual_valid", sa.Boolean()),
        sa.Column("manual_score", sa.Integer()),
        sa.Column("manual_scores", postgresql.JSONB()),
        sa.Column("manual_error_codes", postgresql.JSONB()),
        sa.Column("manual_feedback", sa.Text()),
        sa.Column("coach_note", sa.Text()),
        sa.Column("key_frame_pose", postgresql.JSONB()),
    )

    op.create_table(
        "athlete_action_baselines",
        sa.Column("athlete_id", postgresql.UUID(as_uuid=True), sa.ForeignKey("athletes.id"), nullable=False),
        sa.Column("action_standard_id", postgresql.UUID(as_uuid=True), sa.ForeignKey("action_standards.id"), nullable=False),
        sa.Column("session_count", sa.Integer(), nullable=False, server_default="0"),
        sa.Column("average_score", sa.Integer(), nullable=False, server_default="0"),
        sa.Column("average_valid_reps", sa.Integer(), nullable=False, server_default="0"),
        sa.Column("updated_at", sa.DateTime(timezone=True), nullable=False, server_default=sa.text("now()")),
        sa.PrimaryKeyConstraint("athlete_id", "action_standard_id"),
    )

    op.create_index("ix_training_sessions_saved_at", "training_sessions", ["saved_at"])
    op.create_index("ix_training_sessions_athlete", "training_sessions", ["athlete_id"])
    op.create_index("ix_training_sessions_coach", "training_sessions", ["coach_id"])
    op.create_index("ix_training_sessions_action", "training_sessions", ["action_standard_id"])
    op.create_index("ix_training_tasks_plan", "training_tasks", ["plan_id"])
    op.create_index("ix_training_plans_date", "training_plans", ["training_date"])
    op.create_index("ix_action_repetitions_session", "action_repetitions", ["session_id"])
    op.create_index("ix_action_repetitions_review", "action_repetitions", ["review_status"])


def downgrade() -> None:
    op.drop_table("athlete_action_baselines")
    op.drop_table("action_repetitions")
    op.drop_table("training_sessions")
    op.drop_table("training_tasks")
    op.drop_table("training_plans")
    op.drop_table("action_standards")
    op.drop_table("action_categories")
    op.drop_table("coach_athletes")
    op.drop_table("coaches")
    op.drop_table("athletes")
    op.drop_table("users")

"""add multi participant identity protocol

Revision ID: 20260707_0005
Revises: 20260707_0004
Create Date: 2026-07-07
"""

from alembic import op
import sqlalchemy as sa
from sqlalchemy.dialects import postgresql


revision = "20260707_0005"
down_revision = "20260707_0004"
branch_labels = None
depends_on = None


def upgrade() -> None:
    op.create_table(
        "training_session_participants",
        sa.Column("id", postgresql.UUID(as_uuid=True), primary_key=True),
        sa.Column("session_id", postgresql.UUID(as_uuid=True), sa.ForeignKey("training_sessions.id", ondelete="CASCADE"), nullable=False),
        sa.Column("athlete_id", postgresql.UUID(as_uuid=True), sa.ForeignKey("athletes.id"), nullable=False),
        sa.Column("slot_index", sa.Integer(), nullable=False),
        sa.Column("role", sa.Text(), nullable=False, server_default="participant"),
        sa.Column("track_label", sa.Text()),
        sa.Column("notes", sa.Text()),
        sa.Column("active", sa.Boolean(), nullable=False, server_default=sa.text("true")),
        sa.Column("created_at", sa.DateTime(timezone=True), nullable=False, server_default=sa.text("now()")),
        sa.Column("updated_at", sa.DateTime(timezone=True), nullable=False, server_default=sa.text("now()")),
        sa.CheckConstraint("slot_index >= 1 AND slot_index <= 4", name="ck_training_session_participants_slot"),
        sa.CheckConstraint("role IN ('primary', 'participant')", name="ck_training_session_participants_role"),
        sa.UniqueConstraint("session_id", "athlete_id", name="uq_training_session_participants_athlete"),
        sa.UniqueConstraint("session_id", "slot_index", name="uq_training_session_participants_slot"),
    )
    op.create_index("ix_training_session_participants_session", "training_session_participants", ["session_id"])
    op.create_index("ix_training_session_participants_athlete", "training_session_participants", ["athlete_id"])

    op.add_column("action_repetitions", sa.Column("participant_id", postgresql.UUID(as_uuid=True)))
    op.add_column("action_repetitions", sa.Column("athlete_id", postgresql.UUID(as_uuid=True)))
    op.add_column("action_repetitions", sa.Column("track_id", sa.Integer()))
    op.add_column("action_repetitions", sa.Column("camera_id", sa.Integer()))
    op.add_column("action_repetitions", sa.Column("frame_time_ms", sa.BigInteger()))
    op.add_column("action_repetitions", sa.Column("identity_status", sa.Text(), nullable=False, server_default="unknown"))
    op.add_column("action_repetitions", sa.Column("identity_confidence", sa.Float()))
    op.add_column("action_repetitions", sa.Column("identity_source", sa.Text()))
    op.create_foreign_key("fk_action_repetitions_participant_id", "action_repetitions", "training_session_participants", ["participant_id"], ["id"])
    op.create_foreign_key("fk_action_repetitions_athlete_id", "action_repetitions", "athletes", ["athlete_id"], ["id"])
    op.create_index("ix_action_repetitions_participant", "action_repetitions", ["participant_id"])
    op.create_index("ix_action_repetitions_athlete", "action_repetitions", ["athlete_id"])
    op.create_index("ix_action_repetitions_track", "action_repetitions", ["track_id"])
    op.create_index("ix_action_repetitions_camera", "action_repetitions", ["camera_id"])
    op.create_index("ix_action_repetitions_frame_time", "action_repetitions", ["frame_time_ms"])


def downgrade() -> None:
    op.drop_index("ix_action_repetitions_frame_time", table_name="action_repetitions")
    op.drop_index("ix_action_repetitions_camera", table_name="action_repetitions")
    op.drop_index("ix_action_repetitions_track", table_name="action_repetitions")
    op.drop_index("ix_action_repetitions_athlete", table_name="action_repetitions")
    op.drop_index("ix_action_repetitions_participant", table_name="action_repetitions")
    op.drop_constraint("fk_action_repetitions_athlete_id", "action_repetitions", type_="foreignkey")
    op.drop_constraint("fk_action_repetitions_participant_id", "action_repetitions", type_="foreignkey")
    op.drop_column("action_repetitions", "identity_source")
    op.drop_column("action_repetitions", "identity_confidence")
    op.drop_column("action_repetitions", "identity_status")
    op.drop_column("action_repetitions", "frame_time_ms")
    op.drop_column("action_repetitions", "camera_id")
    op.drop_column("action_repetitions", "track_id")
    op.drop_column("action_repetitions", "athlete_id")
    op.drop_column("action_repetitions", "participant_id")
    op.drop_index("ix_training_session_participants_athlete", table_name="training_session_participants")
    op.drop_index("ix_training_session_participants_session", table_name="training_session_participants")
    op.drop_table("training_session_participants")

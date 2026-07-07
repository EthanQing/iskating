"""add competition events

Revision ID: 20260707_0003
Revises: 20260707_0002
Create Date: 2026-07-07
"""

from alembic import op
import sqlalchemy as sa
from sqlalchemy.dialects import postgresql


revision = "20260707_0003"
down_revision = "20260707_0002"
branch_labels = None
depends_on = None


def timestamps() -> tuple[sa.Column, sa.Column]:
    return (
        sa.Column("created_at", sa.DateTime(timezone=True), nullable=False, server_default=sa.text("now()")),
        sa.Column("updated_at", sa.DateTime(timezone=True), nullable=False, server_default=sa.text("now()")),
    )


def upgrade() -> None:
    op.create_table(
        "competition_events",
        sa.Column("id", postgresql.UUID(as_uuid=True), primary_key=True, server_default=sa.text("gen_random_uuid()")),
        sa.Column("competition_id", postgresql.UUID(as_uuid=True), sa.ForeignKey("competitions.id"), nullable=False),
        sa.Column("race_name", sa.Text()),
        sa.Column("event_name", sa.Text()),
        sa.Column("heat_name", sa.Text()),
        sa.Column("group_name", sa.Text()),
        sa.Column("scheduled_at", sa.DateTime(timezone=True)),
        sa.Column("notes", sa.Text()),
        sa.Column("active", sa.Boolean(), nullable=False, server_default=sa.text("true")),
        *timestamps(),
    )
    op.create_table(
        "event_athletes",
        sa.Column("id", postgresql.UUID(as_uuid=True), primary_key=True, server_default=sa.text("gen_random_uuid()")),
        sa.Column("event_id", postgresql.UUID(as_uuid=True), sa.ForeignKey("competition_events.id"), nullable=False),
        sa.Column("athlete_id", postgresql.UUID(as_uuid=True), sa.ForeignKey("athletes.id"), nullable=False),
        sa.Column("bib_number", sa.Text()),
        sa.Column("lane_number", sa.Text()),
        sa.Column("sort_order", sa.Integer(), nullable=False, server_default="0"),
        sa.Column("result_score", sa.Integer()),
        sa.Column("result_rank", sa.Integer()),
        sa.Column("notes", sa.Text()),
        sa.Column("active", sa.Boolean(), nullable=False, server_default=sa.text("true")),
        *timestamps(),
        sa.UniqueConstraint("event_id", "athlete_id", name="uq_event_athletes_event_athlete"),
    )
    op.add_column("training_sessions", sa.Column("competition_event_id", postgresql.UUID(as_uuid=True)))
    op.add_column("training_sessions", sa.Column("event_athlete_id", postgresql.UUID(as_uuid=True)))
    op.create_foreign_key(
        "fk_training_sessions_competition_event_id",
        "training_sessions",
        "competition_events",
        ["competition_event_id"],
        ["id"],
    )
    op.create_foreign_key(
        "fk_training_sessions_event_athlete_id",
        "training_sessions",
        "event_athletes",
        ["event_athlete_id"],
        ["id"],
    )
    op.create_index("ix_competition_events_competition", "competition_events", ["competition_id"])
    op.create_index("ix_event_athletes_event", "event_athletes", ["event_id"])
    op.create_index("ix_event_athletes_athlete", "event_athletes", ["athlete_id"])
    op.create_index("ix_training_sessions_competition_event", "training_sessions", ["competition_event_id"])
    op.create_index("ix_training_sessions_event_athlete", "training_sessions", ["event_athlete_id"])


def downgrade() -> None:
    op.drop_index("ix_training_sessions_event_athlete", table_name="training_sessions")
    op.drop_index("ix_training_sessions_competition_event", table_name="training_sessions")
    op.drop_index("ix_event_athletes_athlete", table_name="event_athletes")
    op.drop_index("ix_event_athletes_event", table_name="event_athletes")
    op.drop_index("ix_competition_events_competition", table_name="competition_events")
    op.drop_constraint("fk_training_sessions_event_athlete_id", "training_sessions", type_="foreignkey")
    op.drop_constraint("fk_training_sessions_competition_event_id", "training_sessions", type_="foreignkey")
    op.drop_column("training_sessions", "event_athlete_id")
    op.drop_column("training_sessions", "competition_event_id")
    op.drop_table("event_athletes")
    op.drop_table("competition_events")

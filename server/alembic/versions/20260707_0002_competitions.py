"""add competitions

Revision ID: 20260707_0002
Revises: 20260630_0001
Create Date: 2026-07-07
"""

from alembic import op
import sqlalchemy as sa
from sqlalchemy.dialects import postgresql


revision = "20260707_0002"
down_revision = "20260630_0001"
branch_labels = None
depends_on = None


def upgrade() -> None:
    op.create_table(
        "competitions",
        sa.Column("id", postgresql.UUID(as_uuid=True), primary_key=True, server_default=sa.text("gen_random_uuid()")),
        sa.Column("name", sa.Text(), nullable=False),
        sa.Column("location", sa.Text()),
        sa.Column("competition_date", sa.Date()),
        sa.Column("competition_type", sa.Text()),
        sa.Column("notes", sa.Text()),
        sa.Column("active", sa.Boolean(), nullable=False, server_default=sa.text("true")),
        sa.Column("created_at", sa.DateTime(timezone=True), nullable=False, server_default=sa.text("now()")),
        sa.Column("updated_at", sa.DateTime(timezone=True), nullable=False, server_default=sa.text("now()")),
    )
    op.add_column(
        "training_sessions",
        sa.Column("competition_id", postgresql.UUID(as_uuid=True)),
    )
    op.create_foreign_key(
        "fk_training_sessions_competition_id",
        "training_sessions",
        "competitions",
        ["competition_id"],
        ["id"],
    )
    op.create_index("ix_training_sessions_competition", "training_sessions", ["competition_id"])


def downgrade() -> None:
    op.drop_index("ix_training_sessions_competition", table_name="training_sessions")
    op.drop_constraint("fk_training_sessions_competition_id", "training_sessions", type_="foreignkey")
    op.drop_column("training_sessions", "competition_id")
    op.drop_table("competitions")

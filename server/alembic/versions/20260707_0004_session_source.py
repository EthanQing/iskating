"""add session source fields

Revision ID: 20260707_0004
Revises: 20260707_0003
Create Date: 2026-07-07
"""

from alembic import op
import sqlalchemy as sa


revision = "20260707_0004"
down_revision = "20260707_0003"
branch_labels = None
depends_on = None


def upgrade() -> None:
    op.add_column(
        "training_sessions",
        sa.Column("source_type", sa.Text(), nullable=False, server_default="training"),
    )
    op.add_column("training_sessions", sa.Column("source_ref", sa.Text()))
    op.create_index("ix_training_sessions_source_type", "training_sessions", ["source_type"])
    op.create_index("ix_training_sessions_source_ref", "training_sessions", ["source_ref"])


def downgrade() -> None:
    op.drop_index("ix_training_sessions_source_ref", table_name="training_sessions")
    op.drop_index("ix_training_sessions_source_type", table_name="training_sessions")
    op.drop_column("training_sessions", "source_ref")
    op.drop_column("training_sessions", "source_type")

#!/usr/bin/env python3
"""One-shot importer from the legacy local SQLite database to PostgreSQL.

Usage:
  python tools/import_sqlite_to_postgres.py --sqlite "%APPDATA%/iSkating/iSkating Coach/iskating.db"

The target PostgreSQL connection is read from ISKATING_DATABASE_URL. Run Alembic
migrations first. The importer is idempotent for primary keys and updates rows
when the same legacy id already exists.
"""

from __future__ import annotations

import argparse
import json
import os
import sqlite3
import uuid
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

import psycopg
from psycopg.types.json import Jsonb


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--sqlite", required=True, help="Path to the legacy iskating.db SQLite file")
    parser.add_argument("--database-url", default=os.getenv("ISKATING_DATABASE_URL"), help="PostgreSQL URL")
    return parser.parse_args()


def as_uuid(value: Any) -> str | None:
    if value is None or str(value).strip() == "":
        return None
    return str(uuid.UUID(str(value).strip()))


def as_bool(value: Any) -> bool:
    return bool(int(value or 0))


def as_dt(value: Any) -> datetime:
    if not value:
        return datetime.now(timezone.utc)
    text = str(value).replace("Z", "+00:00")
    parsed = datetime.fromisoformat(text)
    if parsed.tzinfo is None:
        parsed = parsed.replace(tzinfo=timezone.utc)
    return parsed


def json_text(value: Any) -> Any:
    if value is None or value == "":
        return ""
    try:
        return json.loads(value)
    except (TypeError, json.JSONDecodeError):
        return value


def scores(row: sqlite3.Row, prefix: str = "") -> dict[str, int]:
    def get(name: str, default: int) -> int:
        key = f"{prefix}{name}_score" if prefix else f"{name}_score"
        return int(row[key] if row[key] is not None else default)

    default = -1 if prefix else 0
    return {
        "detection": get("detection", default),
        "symmetry": get("symmetry", default),
        "balance": get("balance", default),
        "stability": get("stability", default),
        "depth": get("depth", default),
    }


def sqlite_rows(db: sqlite3.Connection, table: str) -> list[sqlite3.Row]:
    try:
        return list(db.execute(f"SELECT * FROM {table}"))
    except sqlite3.OperationalError:
        return []


def row_value(row: sqlite3.Row, key: str, default: Any = None) -> Any:
    return row[key] if key in row.keys() else default


def import_data(sqlite_path: Path, database_url: str) -> dict[str, int]:
    source = sqlite3.connect(sqlite_path)
    source.row_factory = sqlite3.Row
    counts: dict[str, int] = {}

    with psycopg.connect(database_url) as target:
        with target.transaction():
            for row in sqlite_rows(source, "athletes"):
                target.execute(
                    """
                    INSERT INTO athletes
                    (id, name, code, age_group, height_cm, weight_kg, discipline, level, preferred_rotation,
                     preferred_takeoff_foot, injury_notes, goals, active, created_at, updated_at)
                    VALUES (%(id)s, %(name)s, %(code)s, %(age_group)s, %(height_cm)s, %(weight_kg)s, %(discipline)s,
                            %(level)s, %(preferred_rotation)s, %(preferred_takeoff_foot)s, %(injury_notes)s,
                            %(goals)s, %(active)s, %(created_at)s, %(updated_at)s)
                    ON CONFLICT (id) DO UPDATE SET name=excluded.name, code=excluded.code, age_group=excluded.age_group,
                    height_cm=excluded.height_cm, weight_kg=excluded.weight_kg, discipline=excluded.discipline,
                    level=excluded.level, preferred_rotation=excluded.preferred_rotation,
                    preferred_takeoff_foot=excluded.preferred_takeoff_foot, injury_notes=excluded.injury_notes,
                    goals=excluded.goals, active=excluded.active, updated_at=excluded.updated_at
                    """,
                    {
                        "id": as_uuid(row["id"]),
                        "name": row["name"],
                        "code": row["code"],
                        "age_group": row["age_group"],
                        "height_cm": row["height_cm"] or 0,
                        "weight_kg": row["weight_kg"] or 0,
                        "discipline": row["discipline"],
                        "level": row["level"],
                        "preferred_rotation": row["preferred_rotation"],
                        "preferred_takeoff_foot": row["preferred_takeoff_foot"],
                        "injury_notes": row["injury_notes"],
                        "goals": row["goals"],
                        "active": as_bool(row["active"]),
                        "created_at": as_dt(row["created_at"]),
                        "updated_at": as_dt(row["updated_at"]),
                    },
                )
                counts["athletes"] = counts.get("athletes", 0) + 1

            for row in sqlite_rows(source, "coaches"):
                target.execute(
                    """
                    INSERT INTO coaches (id, name, code, specialty, phone, notes, active, created_at, updated_at)
                    VALUES (%(id)s, %(name)s, %(code)s, %(specialty)s, %(phone)s, %(notes)s, %(active)s,
                            %(created_at)s, %(updated_at)s)
                    ON CONFLICT (id) DO UPDATE SET name=excluded.name, code=excluded.code, specialty=excluded.specialty,
                    phone=excluded.phone, notes=excluded.notes, active=excluded.active, updated_at=excluded.updated_at
                    """,
                    {
                        "id": as_uuid(row["id"]),
                        "name": row["name"],
                        "code": row["code"],
                        "specialty": row["specialty"],
                        "phone": row["phone"],
                        "notes": row["notes"],
                        "active": as_bool(row["active"]),
                        "created_at": as_dt(row["created_at"]),
                        "updated_at": as_dt(row["updated_at"]),
                    },
                )
                counts["coaches"] = counts.get("coaches", 0) + 1

            for row in sqlite_rows(source, "coach_athletes"):
                target.execute(
                    """
                    INSERT INTO coach_athletes (coach_id, athlete_id, created_at)
                    VALUES (%s, %s, %s) ON CONFLICT DO NOTHING
                    """,
                    (as_uuid(row["coach_id"]), as_uuid(row["athlete_id"]), as_dt(row["created_at"])),
                )
                counts["coach_athletes"] = counts.get("coach_athletes", 0) + 1

            for row in sqlite_rows(source, "action_categories"):
                target.execute(
                    """
                    INSERT INTO action_categories (id, code, name, sort_order)
                    VALUES (%s, %s, %s, %s)
                    ON CONFLICT (code) DO UPDATE SET name=excluded.name, sort_order=excluded.sort_order
                    """,
                    (as_uuid(row["id"]), row["code"], row["name"], row["sort_order"] or 0),
                )
                counts["action_categories"] = counts.get("action_categories", 0) + 1

            for row in sqlite_rows(source, "action_standards"):
                target.execute(
                    """
                    INSERT INTO action_standards
                    (id, code, name, category_id, level, purpose, version, target_reps, target_score, set_count,
                     rest_seconds, arm_threshold, release_threshold, debounce_ms, weights, minimums, phases,
                     key_points, issue, reference_video_source, reference_repetition_id, reference_notes, active,
                     created_at, updated_at)
                    VALUES (%(id)s, %(code)s, %(name)s, %(category_id)s, %(level)s, %(purpose)s, %(version)s,
                            %(target_reps)s, %(target_score)s, %(set_count)s, %(rest_seconds)s, %(arm_threshold)s,
                            %(release_threshold)s, %(debounce_ms)s, %(weights)s, %(minimums)s, %(phases)s,
                            %(key_points)s, %(issue)s, %(reference_video_source)s, %(reference_repetition_id)s,
                            %(reference_notes)s, %(active)s, %(created_at)s, %(updated_at)s)
                    ON CONFLICT (id) DO UPDATE SET name=excluded.name, category_id=excluded.category_id,
                    version=excluded.version, target_reps=excluded.target_reps, target_score=excluded.target_score,
                    set_count=excluded.set_count, rest_seconds=excluded.rest_seconds, weights=excluded.weights,
                    minimums=excluded.minimums, issue=excluded.issue, updated_at=excluded.updated_at
                    """,
                    {
                        "id": as_uuid(row["id"]),
                        "code": row["code"],
                        "name": row["name"],
                        "category_id": as_uuid(row["category_id"]),
                        "level": row["level"],
                        "purpose": row["purpose"],
                        "version": row["version"],
                        "target_reps": row["target_reps"],
                        "target_score": row["target_score"],
                        "set_count": row["set_count"],
                        "rest_seconds": row["rest_seconds"],
                        "arm_threshold": row["arm_threshold"],
                        "release_threshold": row["release_threshold"],
                        "debounce_ms": row["debounce_ms"],
                        "weights": Jsonb({
                            "detection": row["detection_weight"],
                            "symmetry": row["symmetry_weight"],
                            "balance": row["balance_weight"],
                            "stability": row["stability_weight"],
                            "depth": row["depth_weight"],
                        }),
                        "minimums": Jsonb({
                            "detection": row["detection_min"],
                            "symmetry": row["symmetry_min"],
                            "balance": row["balance_min"],
                            "stability": row["stability_min"],
                            "depth": row["depth_min"],
                        }),
                        "phases": Jsonb(json_text(row["phases"])),
                        "key_points": Jsonb(json_text(row["key_points"])),
                        "issue": Jsonb({
                            "title": row["issue_title"],
                            "bodyPart": row["issue_body_part"],
                            "cause": row["issue_cause"],
                            "correction": row["issue_correction"],
                            "priority": row["issue_priority"],
                        }),
                        "reference_video_source": row["reference_video_source"],
                        "reference_repetition_id": as_uuid(row["reference_repetition_id"]),
                        "reference_notes": row["reference_notes"],
                        "active": as_bool(row["active"]),
                        "created_at": as_dt(row["created_at"]),
                        "updated_at": as_dt(row["updated_at"]),
                    },
                )
                counts["action_standards"] = counts.get("action_standards", 0) + 1

            for row in sqlite_rows(source, "training_plans"):
                target.execute(
                    """
                    INSERT INTO training_plans
                    (id, athlete_id, coach_id, name, training_date, site, training_phase, goal, status, created_at, updated_at)
                    VALUES (%s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s)
                    ON CONFLICT (id) DO UPDATE SET coach_id=excluded.coach_id, name=excluded.name,
                    site=excluded.site, training_phase=excluded.training_phase, goal=excluded.goal,
                    status=excluded.status, updated_at=excluded.updated_at
                    """,
                    (
                        as_uuid(row["id"]),
                        as_uuid(row["athlete_id"]),
                        as_uuid(row["coach_id"]),
                        row["name"],
                        row["training_date"],
                        row["site"],
                        row["training_phase"],
                        row["goal"],
                        row["status"],
                        as_dt(row["created_at"]),
                        as_dt(row["updated_at"]),
                    ),
                )
                counts["training_plans"] = counts.get("training_plans", 0) + 1

            for row in sqlite_rows(source, "training_tasks"):
                target.execute(
                    """
                    INSERT INTO training_tasks
                    (id, plan_id, action_standard_id, standard_version, target_reps, target_score, set_count,
                     rest_seconds, status, sort_order, created_at, updated_at)
                    VALUES (%s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s)
                    ON CONFLICT (id) DO UPDATE SET standard_version=excluded.standard_version,
                    target_reps=excluded.target_reps, target_score=excluded.target_score, set_count=excluded.set_count,
                    rest_seconds=excluded.rest_seconds, status=excluded.status, updated_at=excluded.updated_at
                    """,
                    (
                        as_uuid(row["id"]),
                        as_uuid(row["plan_id"]),
                        as_uuid(row["action_standard_id"]),
                        row["standard_version"],
                        row["target_reps"],
                        row["target_score"],
                        row["set_count"],
                        row["rest_seconds"],
                        row["status"],
                        row["sort_order"],
                        as_dt(row["created_at"]),
                        as_dt(row["updated_at"]),
                    ),
                )
                counts["training_tasks"] = counts.get("training_tasks", 0) + 1

            for row in sqlite_rows(source, "training_sessions"):
                target.execute(
                    """
                    INSERT INTO training_sessions
                    (id, athlete_id, coach_id, competition_id, competition_event_id, event_athlete_id,
                     plan_id, task_id, action_standard_id, standard_version,
                     legacy_qsettings_id, started_at, saved_at, duration_sec, total_reps, valid_reps,
                     average_score, best_score, camera, model_precision, fps, scores, site, training_phase,
                     goal, target_reps, target_score, set_count, rest_seconds, video_source,
                     video_fallback_source, video_camera_name, source_type, source_ref, feedback, notes, coach_comment)
                    VALUES (%(id)s, %(athlete_id)s, %(coach_id)s, %(competition_id)s, %(competition_event_id)s,
                            %(event_athlete_id)s, %(plan_id)s, %(task_id)s, %(action_standard_id)s,
                            %(standard_version)s, %(legacy_qsettings_id)s, %(started_at)s, %(saved_at)s,
                            %(duration_sec)s, %(total_reps)s, %(valid_reps)s, %(average_score)s, %(best_score)s,
                            %(camera)s, %(model_precision)s, %(fps)s, %(scores)s, %(site)s, %(training_phase)s,
                            %(goal)s, %(target_reps)s, %(target_score)s, %(set_count)s, %(rest_seconds)s,
                            %(video_source)s, %(video_fallback_source)s, %(video_camera_name)s,
                            %(source_type)s, %(source_ref)s, %(feedback)s, %(notes)s, %(coach_comment)s)
                    ON CONFLICT (id) DO UPDATE SET saved_at=excluded.saved_at, total_reps=excluded.total_reps,
                    valid_reps=excluded.valid_reps, average_score=excluded.average_score, best_score=excluded.best_score,
                    competition_id=excluded.competition_id, competition_event_id=excluded.competition_event_id,
                    event_athlete_id=excluded.event_athlete_id, scores=excluded.scores,
                    source_type=excluded.source_type, source_ref=excluded.source_ref,
                    coach_comment=excluded.coach_comment
                    """,
                    {
                        "id": as_uuid(row["id"]),
                        "athlete_id": as_uuid(row["athlete_id"]),
                        "coach_id": as_uuid(row["coach_id"]),
                        "competition_id": as_uuid(row_value(row, "competition_id")),
                        "competition_event_id": as_uuid(row_value(row, "competition_event_id")),
                        "event_athlete_id": as_uuid(row_value(row, "event_athlete_id")),
                        "plan_id": as_uuid(row["plan_id"]),
                        "task_id": as_uuid(row["task_id"]),
                        "action_standard_id": as_uuid(row["action_standard_id"]),
                        "standard_version": row["standard_version"],
                        "legacy_qsettings_id": row["legacy_qsettings_id"],
                        "started_at": as_dt(row["started_at"]),
                        "saved_at": as_dt(row["saved_at"]),
                        "duration_sec": row["duration_sec"],
                        "total_reps": row["total_reps"],
                        "valid_reps": row["valid_reps"],
                        "average_score": row["average_score"],
                        "best_score": row["best_score"],
                        "camera": row["camera"],
                        "model_precision": row["model_precision"],
                        "fps": row["fps"],
                        "scores": Jsonb(scores(row)),
                        "site": row["site"],
                        "training_phase": row["training_phase"],
                        "goal": row["goal"],
                        "target_reps": row["target_reps"],
                        "target_score": row["target_score"],
                        "set_count": row["set_count"],
                        "rest_seconds": row["rest_seconds"],
                        "video_source": row["video_source"],
                        "video_fallback_source": row["video_fallback_source"],
                        "video_camera_name": row["video_camera_name"],
                        "source_type": "training",
                        "source_ref": None,
                        "feedback": row["feedback"],
                        "notes": row["notes"],
                        "coach_comment": row["coach_comment"],
                    },
                )
                counts["training_sessions"] = counts.get("training_sessions", 0) + 1

            for row in sqlite_rows(source, "action_repetitions"):
                target.execute(
                    """
                    INSERT INTO action_repetitions
                    (id, session_id, action_standard_id, standard_version, started_ms, ended_ms, valid, score,
                     scores, error_codes, feedback, key_frame_ms, video_clip_start_ms, video_clip_end_ms,
                     source, review_status, reviewer_coach_id, reviewed_at, manual_started_ms, manual_ended_ms,
                     manual_valid, manual_score, manual_scores, manual_error_codes, manual_feedback, coach_note,
                     key_frame_pose)
                    VALUES (%(id)s, %(session_id)s, %(action_standard_id)s, %(standard_version)s, %(started_ms)s,
                            %(ended_ms)s, %(valid)s, %(score)s, %(scores)s, %(error_codes)s, %(feedback)s,
                            %(key_frame_ms)s, %(video_clip_start_ms)s, %(video_clip_end_ms)s, %(source)s,
                            %(review_status)s, %(reviewer_coach_id)s, %(reviewed_at)s, %(manual_started_ms)s,
                            %(manual_ended_ms)s, %(manual_valid)s, %(manual_score)s, %(manual_scores)s,
                            %(manual_error_codes)s, %(manual_feedback)s, %(coach_note)s, %(key_frame_pose)s)
                    ON CONFLICT (id) DO UPDATE SET review_status=excluded.review_status,
                    manual_started_ms=excluded.manual_started_ms, manual_ended_ms=excluded.manual_ended_ms,
                    manual_valid=excluded.manual_valid, manual_score=excluded.manual_score,
                    manual_scores=excluded.manual_scores, manual_error_codes=excluded.manual_error_codes,
                    manual_feedback=excluded.manual_feedback, coach_note=excluded.coach_note
                    """,
                    {
                        "id": as_uuid(row["id"]),
                        "session_id": as_uuid(row["session_id"]),
                        "action_standard_id": as_uuid(row["action_standard_id"]),
                        "standard_version": row["standard_version"],
                        "started_ms": row["started_ms"],
                        "ended_ms": row["ended_ms"],
                        "valid": as_bool(row["valid"]),
                        "score": row["score"],
                        "scores": Jsonb(scores(row)),
                        "error_codes": Jsonb(json_text(row["error_codes"])),
                        "feedback": row["feedback"],
                        "key_frame_ms": row["key_frame_ms"],
                        "video_clip_start_ms": row["video_clip_start_ms"],
                        "video_clip_end_ms": row["video_clip_end_ms"],
                        "source": row["source"],
                        "review_status": row["review_status"],
                        "reviewer_coach_id": as_uuid(row["reviewer_coach_id"]),
                        "reviewed_at": as_dt(row["reviewed_at"]) if row["reviewed_at"] else None,
                        "manual_started_ms": row["manual_started_ms"] if row["manual_started_ms"] >= 0 else None,
                        "manual_ended_ms": row["manual_ended_ms"] if row["manual_ended_ms"] >= 0 else None,
                        "manual_valid": as_bool(row["manual_valid"]) if row["manual_valid"] >= 0 else None,
                        "manual_score": row["manual_score"] if row["manual_score"] >= 0 else None,
                        "manual_scores": Jsonb(scores(row, "manual")),
                        "manual_error_codes": Jsonb(json_text(row["manual_error_codes"])),
                        "manual_feedback": row["manual_feedback"],
                        "coach_note": row["coach_note"],
                        "key_frame_pose": Jsonb(json_text(row["key_frame_pose_json"])),
                    },
                )
                counts["action_repetitions"] = counts.get("action_repetitions", 0) + 1

            for row in sqlite_rows(source, "athlete_action_baselines"):
                target.execute(
                    """
                    INSERT INTO athlete_action_baselines
                    (athlete_id, action_standard_id, session_count, average_score, average_valid_reps, updated_at)
                    VALUES (%s, %s, %s, %s, %s, %s)
                    ON CONFLICT (athlete_id, action_standard_id) DO UPDATE SET
                    session_count=excluded.session_count, average_score=excluded.average_score,
                    average_valid_reps=excluded.average_valid_reps, updated_at=excluded.updated_at
                    """,
                    (
                        as_uuid(row["athlete_id"]),
                        as_uuid(row["action_standard_id"]),
                        row["session_count"],
                        row["average_score"],
                        row["average_valid_reps"],
                        as_dt(row["updated_at"]),
                    ),
                )
                counts["athlete_action_baselines"] = counts.get("athlete_action_baselines", 0) + 1

    return counts


def main() -> None:
    args = parse_args()
    if not args.database_url:
        raise SystemExit("ISKATING_DATABASE_URL or --database-url is required")
    sqlite_path = Path(args.sqlite).expanduser()
    if not sqlite_path.exists():
        raise SystemExit(f"SQLite database not found: {sqlite_path}")
    counts = import_data(sqlite_path, args.database_url)
    print("Import completed.")
    for table, count in sorted(counts.items()):
        print(f"{table}: {count}")


if __name__ == "__main__":
    main()

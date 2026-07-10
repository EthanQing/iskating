from __future__ import annotations

import json
import os
import uuid
from datetime import date, datetime, timedelta, timezone
from typing import Any

import jwt
from fastapi import Body, Depends, FastAPI, HTTPException, Query, status
from fastapi.middleware.cors import CORSMiddleware
from passlib.context import CryptContext
from sqlalchemy import create_engine, text
from sqlalchemy.engine import Engine
from sqlalchemy.orm import Session, sessionmaker


def env(name: str, default: str | None = None) -> str:
    value = os.getenv(name, default)
    if value is None or value == "":
        raise RuntimeError(f"{name} is required")
    return value


DATABASE_URL = env("ISKATING_DATABASE_URL")
JWT_SECRET = env("ISKATING_JWT_SECRET", "change-me-before-production")
JWT_ALGORITHM = "HS256"
ACCESS_TOKEN_MINUTES = 12 * 60

engine: Engine = create_engine(DATABASE_URL, pool_pre_ping=True)
SessionLocal = sessionmaker(bind=engine, autoflush=False, autocommit=False)
passwords = CryptContext(schemes=["bcrypt"], deprecated="auto")

app = FastAPI(title="iSkating Training API", version="1.0.0")
origins = [item.strip() for item in os.getenv("ISKATING_CORS_ORIGINS", "*").split(",") if item.strip()]
app.add_middleware(
    CORSMiddleware,
    allow_origins=origins,
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)


def db_session():
    db = SessionLocal()
    try:
        yield db
        db.commit()
    except Exception:
        db.rollback()
        raise
    finally:
        db.close()


def now() -> datetime:
    return datetime.now(timezone.utc)


def new_uuid() -> str:
    return str(uuid.uuid4())


def parse_uuid(value: Any) -> str | None:
    if value is None:
        return None
    text_value = str(value).strip()
    if text_value == "":
        return None
    return str(uuid.UUID(text_value))


def parse_dt(value: Any, fallback: datetime | None = None) -> datetime:
    if not value:
        return fallback or now()
    if isinstance(value, datetime):
        return value
    text_value = str(value).replace("Z", "+00:00")
    parsed = datetime.fromisoformat(text_value)
    if parsed.tzinfo is None:
        parsed = parsed.replace(tzinfo=timezone.utc)
    return parsed


def parse_date(value: Any, fallback: date | None = None) -> date:
    if not value:
        return fallback or now().date()
    if isinstance(value, date):
        return value
    return date.fromisoformat(str(value)[:10])


def optional_int(value: Any) -> int | None:
    if value is None or str(value).strip() == "":
        return None
    parsed = int(value)
    return parsed if parsed >= 0 else None


def camel_to_snake(name: str) -> str:
    out: list[str] = []
    for ch in name:
        if ch.isupper():
            out.append("_")
            out.append(ch.lower())
        else:
            out.append(ch)
    return "".join(out).lstrip("_")


def token_for(user: dict[str, Any]) -> str:
    payload = {
        "sub": str(user["id"]),
        "username": user["username"],
        "role": user["role"],
        "exp": datetime.now(timezone.utc) + timedelta(minutes=ACCESS_TOKEN_MINUTES),
    }
    return jwt.encode(payload, JWT_SECRET, algorithm=JWT_ALGORITHM)


def current_user(authorization: str | None = Query(default=None, alias="access_token"),
                 db: Session = Depends(db_session)) -> dict[str, Any]:
    raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="Missing bearer token")


async def bearer_user(request, db: Session = Depends(db_session)) -> dict[str, Any]:
    header = request.headers.get("authorization", "")
    if not header.lower().startswith("bearer "):
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="Missing bearer token")
    token = header.split(" ", 1)[1].strip()
    try:
        payload = jwt.decode(token, JWT_SECRET, algorithms=[JWT_ALGORITHM])
    except jwt.PyJWTError as exc:
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="Invalid token") from exc
    user = db.execute(
        text("SELECT id::text, username, role, active FROM users WHERE id = :id"),
        {"id": payload.get("sub")},
    ).mappings().first()
    if not user or not user["active"]:
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="User disabled")
    return dict(user)


# FastAPI cannot infer Request from a forward-declared annotation on some versions.
from fastapi import Request  # noqa: E402


async def require_user(request: Request, db: Session = Depends(db_session)) -> dict[str, Any]:
    return await bearer_user(request, db)


def require_admin(user: dict[str, Any] = Depends(require_user)) -> dict[str, Any]:
    if user["role"] != "admin":
        raise HTTPException(status_code=status.HTTP_403_FORBIDDEN, detail="Admin role required")
    return user


def table_count(db: Session, table: str) -> int:
    return int(db.execute(text(f"SELECT COUNT(*) FROM {table}")).scalar() or 0)


def seed_defaults() -> None:
    with SessionLocal.begin() as db:
        admin_user = os.getenv("ISKATING_ADMIN_USER", "admin")
        admin_password = os.getenv("ISKATING_ADMIN_PASSWORD", "admin123")
        exists = db.execute(text("SELECT 1 FROM users WHERE username = :username"), {"username": admin_user}).first()
        if not exists:
            db.execute(
                text(
                    "INSERT INTO users (id, username, password_hash, role, active) "
                    "VALUES (:id, :username, :password_hash, 'admin', true)"
                ),
                {"id": new_uuid(), "username": admin_user, "password_hash": passwords.hash(admin_password)},
            )

        if table_count(db, "athletes") == 0:
            db.execute(
                text("INSERT INTO athletes (id, name, code, active) VALUES (:id, :name, :code, true)"),
                {"id": new_uuid(), "name": "默认运动员", "code": "ATH-DEFAULT"},
            )
        if table_count(db, "coaches") == 0:
            db.execute(
                text("INSERT INTO coaches (id, name, code, active) VALUES (:id, :name, :code, true)"),
                {"id": new_uuid(), "name": "默认教练", "code": "COACH-DEFAULT"},
            )

        categories = [
            ("edge", "刃感基础", 10),
            ("push", "蹬冰发力", 20),
            ("crossover", "压步转换", 30),
            ("jump", "跳跃准备", 40),
            ("spin", "旋转控制", 50),
            ("steps", "步法节奏", 60),
            ("landing", "落冰控制", 70),
            ("posture", "姿态稳定", 80),
        ]
        category_ids: dict[str, str] = {}
        for code, name, sort_order in categories:
            row = db.execute(text("SELECT id::text FROM action_categories WHERE code = :code"), {"code": code}).first()
            if row:
                category_ids[code] = row[0]
            else:
                category_id = new_uuid()
                category_ids[code] = category_id
                db.execute(
                    text(
                        "INSERT INTO action_categories (id, code, name, sort_order) "
                        "VALUES (:id, :code, :name, :sort_order)"
                    ),
                    {"id": category_id, "code": code, "name": name, "sort_order": sort_order},
                )

        standards = [
            ("basic_outside_edge", "基础外刃滑行", "edge", "建立外刃控制和身体轴线"),
            ("push_extension", "蹬冰伸展", "push", "提升蹬冰幅度和髋膝伸展"),
            ("crossover_weight_transfer", "压步重心转换", "crossover", "稳定交叉步重心转换"),
            ("rotation_preparation", "转体准备姿态", "posture", "控制上肢和核心预备姿态"),
            ("jump_takeoff_preparation", "跳跃起跳准备", "jump", "强化起跳前节奏和重心"),
            ("landing_control", "落冰控制", "landing", "提升落冰稳定性"),
            ("spin_axis_hold", "旋转轴线保持", "spin", "保持旋转轴线和身体收紧"),
            ("step_sequence_rhythm", "步法节奏控制", "steps", "提升步法节奏和稳定性"),
        ]
        for code, name, category_code, purpose in standards:
            if db.execute(text("SELECT 1 FROM action_standards WHERE code = :code"), {"code": code}).first():
                continue
            db.execute(
                text(
                    "INSERT INTO action_standards "
                    "(id, code, name, category_id, purpose, weights, minimums, issue, active) "
                    "VALUES (:id, :code, :name, :category_id, :purpose, "
                    "CAST(:weights AS jsonb), CAST(:minimums AS jsonb), CAST(:issue AS jsonb), true)"
                ),
                {
                    "id": new_uuid(),
                    "code": code,
                    "name": name,
                    "category_id": category_ids[category_code],
                    "purpose": purpose,
                    "weights": '{"detection":0.28,"symmetry":0.18,"balance":0.22,"stability":0.17,"depth":0.15}',
                    "minimums": '{"detection":55,"symmetry":60,"balance":60,"stability":60,"depth":55}',
                    "issue": '{"title":"动作质量待提升","bodyPart":"整体","cause":"关键姿态或节奏未达标","correction":"降低速度，保持重心和轴线","priority":2}',
                },
            )


@app.on_event("startup")
def startup() -> None:
    seed_defaults()


@app.get("/health")
def health(db: Session = Depends(db_session)) -> dict[str, Any]:
    db.execute(text("SELECT 1"))
    return {"status": "ok"}


@app.post("/auth/login")
def login(payload: dict[str, Any] = Body(...), db: Session = Depends(db_session)) -> dict[str, Any]:
    username = str(payload.get("username", "")).strip()
    password = str(payload.get("password", ""))
    user = db.execute(
        text("SELECT id::text, username, password_hash, role, active FROM users WHERE username = :username"),
        {"username": username},
    ).mappings().first()
    if not user or not user["active"] or not passwords.verify(password, user["password_hash"]):
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="Invalid username or password")
    user_dict = dict(user)
    return {
        "accessToken": token_for(user_dict),
        "tokenType": "bearer",
        "expiresIn": ACCESS_TOKEN_MINUTES * 60,
        "user": {"id": user_dict["id"], "username": user_dict["username"], "role": user_dict["role"]},
    }


def athlete_row(row: dict[str, Any]) -> dict[str, Any]:
    return {
        "id": str(row["id"]),
        "name": row["name"] or "",
        "code": row["code"] or "",
        "ageGroup": row["age_group"] or "",
        "heightCm": float(row["height_cm"] or 0),
        "weightKg": float(row["weight_kg"] or 0),
        "discipline": row["discipline"] or "",
        "level": row["level"] or "",
        "preferredRotation": row["preferred_rotation"] or "",
        "preferredTakeoffFoot": row["preferred_takeoff_foot"] or "",
        "injuryNotes": row["injury_notes"] or "",
        "goals": row["goals"] or "",
        "active": bool(row["active"]),
    }


def coach_row(row: dict[str, Any]) -> dict[str, Any]:
    return {
        "id": str(row["id"]),
        "name": row["name"] or "",
        "code": row["code"] or "",
        "specialty": row["specialty"] or "",
        "phone": row["phone"] or "",
        "notes": row["notes"] or "",
        "active": bool(row["active"]),
    }


def competition_row(row: dict[str, Any]) -> dict[str, Any]:
    return {
        "id": str(row["id"]),
        "name": row["name"] or "",
        "location": row["location"] or "",
        "competitionDate": row["competition_date"].isoformat() if row["competition_date"] else "",
        "competitionType": row["competition_type"] or "",
        "notes": row["notes"] or "",
        "active": bool(row["active"]),
    }


def competition_event_row(row: dict[str, Any]) -> dict[str, Any]:
    return {
        "id": str(row["id"]),
        "competitionId": str(row["competition_id"]),
        "competitionName": row.get("competition_name") or "",
        "raceName": row["race_name"] or "",
        "eventName": row["event_name"] or "",
        "heatName": row["heat_name"] or "",
        "groupName": row["group_name"] or "",
        "scheduledAt": row["scheduled_at"].isoformat() if row["scheduled_at"] else "",
        "notes": row["notes"] or "",
        "active": bool(row["active"]),
    }


def event_athlete_row(row: dict[str, Any]) -> dict[str, Any]:
    return {
        "id": str(row["id"]),
        "eventId": str(row["event_id"]),
        "athleteId": str(row["athlete_id"]),
        "athleteName": row.get("athlete_name") or "",
        "bibNumber": row["bib_number"] or "",
        "laneNumber": row["lane_number"] or "",
        "sortOrder": row["sort_order"],
        "resultScore": row["result_score"] if row["result_score"] is not None else -1,
        "resultRank": row["result_rank"] if row["result_rank"] is not None else -1,
        "notes": row["notes"] or "",
        "active": bool(row["active"]),
    }


def session_source(session: dict[str, Any],
                   competition_id: uuid.UUID | str | None,
                   competition_event_id: uuid.UUID | None) -> tuple[str, str | None]:
    if competition_event_id:
        return "competition", str(competition_event_id)
    if competition_id:
        return "competition", str(competition_id)
    if int(session.get("camera", 1)) == 0 and session.get("videoSource"):
        return "offline_import", session.get("analysisTaskId") or session.get("videoSource")
    return "training", session.get("taskId") or session.get("planId") or None


def metadata_from_payload(value: Any) -> dict[str, Any]:
    if isinstance(value, dict):
        return value
    if isinstance(value, str) and value.strip():
        try:
            parsed = json.loads(value)
            return parsed if isinstance(parsed, dict) else {}
        except json.JSONDecodeError:
            return {}
    return {}


def offline_analysis_task_row(row: dict[str, Any]) -> dict[str, Any]:
    return {
        "id": str(row["id"]),
        "batchId": str(row["batch_id"] or ""),
        "cameraId": row["camera_id"],
        "timeOffsetMs": row["time_offset_ms"],
        "videoPath": row["video_path"] or "",
        "fileName": row["file_name"] or "",
        "fileSizeBytes": row["file_size_bytes"] if row["file_size_bytes"] is not None else -1,
        "fileModifiedAt": row["file_modified_at"].isoformat() if row["file_modified_at"] else "",
        "durationMs": row["duration_ms"],
        "status": row["status"] or "imported",
        "probeMetadata": row["probe_metadata"] or {},
        "summaryMetadata": row["summary_metadata"] or {},
        "createdAt": row["created_at"].isoformat() if row.get("created_at") else "",
        "updatedAt": row["updated_at"].isoformat() if row.get("updated_at") else "",
    }


def scores_from_payload(payload: dict[str, Any], prefix: str = "") -> dict[str, int]:
    def value(name: str) -> int:
        key = f"{prefix}{name[0].upper()}{name[1:]}Score" if prefix else f"{name}Score"
        return int(payload.get(key, -1 if prefix else 0))

    return {
        "detection": value("detection"),
        "symmetry": value("symmetry"),
        "balance": value("balance"),
        "stability": value("stability"),
        "depth": value("depth"),
    }


def standard_row(row: dict[str, Any]) -> dict[str, Any]:
    weights = row["weights"] or {}
    minimums = row["minimums"] or {}
    issue = row["issue"] or {}
    return {
        "id": str(row["id"]),
        "code": row["code"] or "",
        "name": row["name"] or "",
        "categoryId": str(row["category_id"]),
        "categoryName": row.get("category_name") or "",
        "level": row["level"] or "",
        "purpose": row["purpose"] or "",
        "version": row["version"],
        "targetReps": row["target_reps"],
        "targetScore": row["target_score"],
        "setCount": row["set_count"],
        "restSeconds": row["rest_seconds"],
        "armThreshold": row["arm_threshold"],
        "releaseThreshold": row["release_threshold"],
        "debounceMs": row["debounce_ms"],
        "detectionWeight": weights.get("detection", 0.28),
        "symmetryWeight": weights.get("symmetry", 0.18),
        "balanceWeight": weights.get("balance", 0.22),
        "stabilityWeight": weights.get("stability", 0.17),
        "depthWeight": weights.get("depth", 0.15),
        "detectionMin": minimums.get("detection", 55),
        "symmetryMin": minimums.get("symmetry", 60),
        "balanceMin": minimums.get("balance", 60),
        "stabilityMin": minimums.get("stability", 60),
        "depthMin": minimums.get("depth", 55),
        "phases": row["phases"] if isinstance(row["phases"], str) else "",
        "keyPoints": row["key_points"] if isinstance(row["key_points"], str) else "",
        "issueTitle": issue.get("title", ""),
        "issueBodyPart": issue.get("bodyPart", ""),
        "issueCause": issue.get("cause", ""),
        "issueCorrection": issue.get("correction", ""),
        "issuePriority": issue.get("priority", 2),
        "referenceVideoSource": row["reference_video_source"] or "",
        "referenceRepetitionId": str(row["reference_repetition_id"] or ""),
        "referenceNotes": row["reference_notes"] or "",
    }


@app.get("/athletes")
def athletes(db: Session = Depends(db_session), _: dict[str, Any] = Depends(require_user)) -> list[dict[str, Any]]:
    rows = db.execute(text("SELECT * FROM athletes WHERE active = true ORDER BY name")).mappings()
    return [athlete_row(dict(row)) for row in rows]


@app.post("/athletes")
def create_athlete(payload: dict[str, Any] = Body(...),
                   db: Session = Depends(db_session),
                   _: dict[str, Any] = Depends(require_user)) -> dict[str, Any]:
    athlete_id = parse_uuid(payload.get("id")) or new_uuid()
    db.execute(
        text(
            "INSERT INTO athletes "
            "(id, name, code, age_group, height_cm, weight_kg, discipline, level, preferred_rotation, "
            "preferred_takeoff_foot, injury_notes, goals, active, updated_at) "
            "VALUES (:id, :name, :code, :age_group, :height_cm, :weight_kg, :discipline, :level, "
            ":preferred_rotation, :preferred_takeoff_foot, :injury_notes, :goals, :active, now()) "
            "ON CONFLICT (id) DO UPDATE SET name=excluded.name, code=excluded.code, age_group=excluded.age_group, "
            "height_cm=excluded.height_cm, weight_kg=excluded.weight_kg, discipline=excluded.discipline, "
            "level=excluded.level, preferred_rotation=excluded.preferred_rotation, "
            "preferred_takeoff_foot=excluded.preferred_takeoff_foot, injury_notes=excluded.injury_notes, "
            "goals=excluded.goals, active=excluded.active, updated_at=now()"
        ),
        {
            "id": athlete_id,
            "name": str(payload.get("name", "")).strip() or "未命名运动员",
            "code": payload.get("code") or None,
            "age_group": payload.get("ageGroup") or None,
            "height_cm": float(payload.get("heightCm", 0) or 0),
            "weight_kg": float(payload.get("weightKg", 0) or 0),
            "discipline": payload.get("discipline") or None,
            "level": payload.get("level") or None,
            "preferred_rotation": payload.get("preferredRotation") or None,
            "preferred_takeoff_foot": payload.get("preferredTakeoffFoot") or None,
            "injury_notes": payload.get("injuryNotes") or None,
            "goals": payload.get("goals") or None,
            "active": bool(payload.get("active", True)),
        },
    )
    row = db.execute(text("SELECT * FROM athletes WHERE id = :id"), {"id": athlete_id}).mappings().one()
    return athlete_row(dict(row))


@app.patch("/athletes/{athlete_id}")
def archive_athlete(athlete_id: str,
                    payload: dict[str, Any] = Body(default={}),
                    db: Session = Depends(db_session),
                    _: dict[str, Any] = Depends(require_user)) -> dict[str, Any]:
    active = bool(payload.get("active", False))
    db.execute(text("UPDATE athletes SET active = :active, updated_at = now() WHERE id = :id"), {"id": athlete_id, "active": active})
    return {"ok": True}


@app.get("/coaches")
def coaches(db: Session = Depends(db_session), _: dict[str, Any] = Depends(require_user)) -> list[dict[str, Any]]:
    rows = db.execute(text("SELECT * FROM coaches WHERE active = true ORDER BY name")).mappings()
    return [coach_row(dict(row)) for row in rows]


@app.get("/coaches/{coach_id}/athletes")
def coach_athletes(coach_id: str,
                   db: Session = Depends(db_session),
                   _: dict[str, Any] = Depends(require_user)) -> dict[str, Any]:
    rows = db.execute(
        text("SELECT athlete_id::text FROM coach_athletes WHERE coach_id = :coach_id ORDER BY athlete_id"),
        {"coach_id": coach_id},
    )
    return {"athleteIds": [row[0] for row in rows]}


@app.post("/coaches")
def save_coach(payload: dict[str, Any] = Body(...),
               db: Session = Depends(db_session),
               _: dict[str, Any] = Depends(require_user)) -> dict[str, Any]:
    coach_id = parse_uuid(payload.get("id")) or new_uuid()
    athlete_ids = [parse_uuid(value) for value in payload.get("athleteIds", []) if parse_uuid(value)]
    db.execute(
        text(
            "INSERT INTO coaches (id, name, code, specialty, phone, notes, active, updated_at) "
            "VALUES (:id, :name, :code, :specialty, :phone, :notes, :active, now()) "
            "ON CONFLICT (id) DO UPDATE SET name=excluded.name, code=excluded.code, specialty=excluded.specialty, "
            "phone=excluded.phone, notes=excluded.notes, active=excluded.active, updated_at=now()"
        ),
        {
            "id": coach_id,
            "name": str(payload.get("name", "")).strip() or "未命名教练",
            "code": payload.get("code") or None,
            "specialty": payload.get("specialty") or None,
            "phone": payload.get("phone") or None,
            "notes": payload.get("notes") or None,
            "active": bool(payload.get("active", True)),
        },
    )
    if "athleteIds" in payload:
        db.execute(text("DELETE FROM coach_athletes WHERE coach_id = :coach_id"), {"coach_id": coach_id})
        for athlete_id in athlete_ids:
            db.execute(
                text(
                    "INSERT INTO coach_athletes (coach_id, athlete_id) VALUES (:coach_id, :athlete_id) "
                    "ON CONFLICT DO NOTHING"
                ),
                {"coach_id": coach_id, "athlete_id": athlete_id},
            )
    row = db.execute(text("SELECT * FROM coaches WHERE id = :id"), {"id": coach_id}).mappings().one()
    return coach_row(dict(row))


@app.patch("/coaches/{coach_id}")
def archive_coach(coach_id: str,
                  payload: dict[str, Any] = Body(default={}),
                  db: Session = Depends(db_session),
                  _: dict[str, Any] = Depends(require_user)) -> dict[str, Any]:
    active = bool(payload.get("active", False))
    db.execute(text("UPDATE coaches SET active = :active, updated_at = now() WHERE id = :id"), {"id": coach_id, "active": active})
    return {"ok": True}


@app.get("/competitions")
def competitions(q: str = "",
                 includeInactive: bool = False,
                 db: Session = Depends(db_session),
                 _: dict[str, Any] = Depends(require_user)) -> list[dict[str, Any]]:
    where: list[str] = []
    args: dict[str, Any] = {}
    if not includeInactive:
        where.append("active = true")
    if q.strip():
        where.append("(COALESCE(name,'') ILIKE :q OR COALESCE(location,'') ILIKE :q OR COALESCE(competition_type,'') ILIKE :q OR COALESCE(notes,'') ILIKE :q)")
        args["q"] = f"%{q.strip()}%"
    where_sql = "WHERE " + " AND ".join(where) if where else ""
    rows = db.execute(
        text(f"SELECT * FROM competitions {where_sql} ORDER BY competition_date DESC NULLS LAST, name"),
        args,
    ).mappings()
    return [competition_row(dict(row)) for row in rows]


@app.post("/competitions")
def save_competition(payload: dict[str, Any] = Body(...),
                     db: Session = Depends(db_session),
                     _: dict[str, Any] = Depends(require_user)) -> dict[str, Any]:
    competition_id = parse_uuid(payload.get("id")) or new_uuid()
    db.execute(
        text(
            "INSERT INTO competitions "
            "(id, name, location, competition_date, competition_type, notes, active, updated_at) "
            "VALUES (:id, :name, :location, :competition_date, :competition_type, :notes, :active, now()) "
            "ON CONFLICT (id) DO UPDATE SET name=excluded.name, location=excluded.location, "
            "competition_date=excluded.competition_date, competition_type=excluded.competition_type, "
            "notes=excluded.notes, active=excluded.active, updated_at=now()"
        ),
        {
            "id": competition_id,
            "name": str(payload.get("name", "")).strip() or "未命名比赛",
            "location": payload.get("location") or None,
            "competition_date": parse_date(payload.get("competitionDate")) if payload.get("competitionDate") else None,
            "competition_type": payload.get("competitionType") or None,
            "notes": payload.get("notes") or None,
            "active": bool(payload.get("active", True)),
        },
    )
    row = db.execute(text("SELECT * FROM competitions WHERE id = :id"), {"id": competition_id}).mappings().one()
    return competition_row(dict(row))


@app.patch("/competitions/{competition_id}")
def archive_competition(competition_id: str,
                        payload: dict[str, Any] = Body(default={}),
                        db: Session = Depends(db_session),
                        _: dict[str, Any] = Depends(require_user)) -> dict[str, Any]:
    active = bool(payload.get("active", False))
    db.execute(text("UPDATE competitions SET active = :active, updated_at = now() WHERE id = :id"), {"id": competition_id, "active": active})
    return {"ok": True}


@app.get("/competition-events")
def competition_events(competitionId: str = "",
                       q: str = "",
                       includeInactive: bool = False,
                       db: Session = Depends(db_session),
                       _: dict[str, Any] = Depends(require_user)) -> list[dict[str, Any]]:
    where: list[str] = []
    args: dict[str, Any] = {}
    if competitionId:
        where.append("e.competition_id = :competition_id")
        args["competition_id"] = parse_uuid(competitionId)
    if not includeInactive:
        where.append("e.active = true")
    if q.strip():
        where.append("(COALESCE(e.race_name,'') ILIKE :q OR COALESCE(e.event_name,'') ILIKE :q OR COALESCE(e.heat_name,'') ILIKE :q OR COALESCE(e.group_name,'') ILIKE :q OR COALESCE(e.notes,'') ILIKE :q)")
        args["q"] = f"%{q.strip()}%"
    where_sql = "WHERE " + " AND ".join(where) if where else ""
    rows = db.execute(
        text(
            "SELECT e.*, c.name AS competition_name FROM competition_events e "
            "JOIN competitions c ON c.id = e.competition_id "
            f"{where_sql} ORDER BY e.scheduled_at DESC NULLS LAST, e.race_name, e.event_name, e.heat_name, e.group_name"
        ),
        args,
    ).mappings()
    return [competition_event_row(dict(row)) for row in rows]


@app.post("/competition-events")
def save_competition_event(payload: dict[str, Any] = Body(...),
                           db: Session = Depends(db_session),
                           _: dict[str, Any] = Depends(require_user)) -> dict[str, Any]:
    event_id = parse_uuid(payload.get("id")) or new_uuid()
    competition_id = parse_uuid(payload.get("competitionId"))
    if not competition_id:
        raise HTTPException(status_code=400, detail="competitionId is required")
    db.execute(
        text(
            "INSERT INTO competition_events "
            "(id, competition_id, race_name, event_name, heat_name, group_name, scheduled_at, notes, active, updated_at) "
            "VALUES (:id, :competition_id, :race_name, :event_name, :heat_name, :group_name, :scheduled_at, :notes, :active, now()) "
            "ON CONFLICT (id) DO UPDATE SET competition_id=excluded.competition_id, race_name=excluded.race_name, "
            "event_name=excluded.event_name, heat_name=excluded.heat_name, group_name=excluded.group_name, "
            "scheduled_at=excluded.scheduled_at, notes=excluded.notes, active=excluded.active, updated_at=now()"
        ),
        {
            "id": event_id,
            "competition_id": competition_id,
            "race_name": payload.get("raceName") or None,
            "event_name": payload.get("eventName") or None,
            "heat_name": payload.get("heatName") or None,
            "group_name": payload.get("groupName") or None,
            "scheduled_at": parse_dt(payload.get("scheduledAt")) if payload.get("scheduledAt") else None,
            "notes": payload.get("notes") or None,
            "active": bool(payload.get("active", True)),
        },
    )
    row = db.execute(
        text(
            "SELECT e.*, c.name AS competition_name FROM competition_events e "
            "JOIN competitions c ON c.id = e.competition_id WHERE e.id = :id"
        ),
        {"id": event_id},
    ).mappings().one()
    return competition_event_row(dict(row))


@app.patch("/competition-events/{event_id}")
def archive_competition_event(event_id: str,
                              payload: dict[str, Any] = Body(default={}),
                              db: Session = Depends(db_session),
                              _: dict[str, Any] = Depends(require_user)) -> dict[str, Any]:
    active = bool(payload.get("active", False))
    db.execute(text("UPDATE competition_events SET active = :active, updated_at = now() WHERE id = :id"), {"id": event_id, "active": active})
    return {"ok": True}


@app.get("/event-athletes")
def event_athletes(eventId: str = "",
                   athleteId: str = "",
                   includeInactive: bool = False,
                   db: Session = Depends(db_session),
                   _: dict[str, Any] = Depends(require_user)) -> list[dict[str, Any]]:
    where: list[str] = []
    args: dict[str, Any] = {}
    if eventId:
        where.append("ea.event_id = :event_id")
        args["event_id"] = parse_uuid(eventId)
    if athleteId:
        where.append("ea.athlete_id = :athlete_id")
        args["athlete_id"] = parse_uuid(athleteId)
    if not includeInactive:
        where.append("ea.active = true")
    where_sql = "WHERE " + " AND ".join(where) if where else ""
    rows = db.execute(
        text(
            "SELECT ea.*, a.name AS athlete_name FROM event_athletes ea "
            "JOIN athletes a ON a.id = ea.athlete_id "
            f"{where_sql} ORDER BY ea.sort_order, a.name"
        ),
        args,
    ).mappings()
    return [event_athlete_row(dict(row)) for row in rows]


@app.post("/event-athletes")
def save_event_athlete(payload: dict[str, Any] = Body(...),
                       db: Session = Depends(db_session),
                       _: dict[str, Any] = Depends(require_user)) -> dict[str, Any]:
    event_athlete_id = parse_uuid(payload.get("id")) or new_uuid()
    event_id = parse_uuid(payload.get("eventId"))
    athlete_id = parse_uuid(payload.get("athleteId"))
    if not event_id or not athlete_id:
        raise HTTPException(status_code=400, detail="eventId and athleteId are required")
    db.execute(
        text(
            "INSERT INTO event_athletes "
            "(id, event_id, athlete_id, bib_number, lane_number, sort_order, result_score, result_rank, notes, active, updated_at) "
            "VALUES (:id, :event_id, :athlete_id, :bib_number, :lane_number, :sort_order, :result_score, :result_rank, :notes, :active, now()) "
            "ON CONFLICT (event_id, athlete_id) DO UPDATE SET bib_number=excluded.bib_number, lane_number=excluded.lane_number, "
            "sort_order=excluded.sort_order, result_score=excluded.result_score, result_rank=excluded.result_rank, "
            "notes=excluded.notes, active=excluded.active, updated_at=now()"
        ),
        {
            "id": event_athlete_id,
            "event_id": event_id,
            "athlete_id": athlete_id,
            "bib_number": payload.get("bibNumber") or None,
            "lane_number": payload.get("laneNumber") or None,
            "sort_order": int(payload.get("sortOrder", 0) or 0),
            "result_score": optional_int(payload.get("resultScore")),
            "result_rank": optional_int(payload.get("resultRank")),
            "notes": payload.get("notes") or None,
            "active": bool(payload.get("active", True)),
        },
    )
    row = db.execute(
        text(
            "SELECT ea.*, a.name AS athlete_name FROM event_athletes ea "
            "JOIN athletes a ON a.id = ea.athlete_id WHERE ea.event_id = :event_id AND ea.athlete_id = :athlete_id"
        ),
        {"event_id": event_id, "athlete_id": athlete_id},
    ).mappings().one()
    return event_athlete_row(dict(row))


@app.patch("/event-athletes/{event_athlete_id}")
def archive_event_athlete(event_athlete_id: str,
                          payload: dict[str, Any] = Body(default={}),
                          db: Session = Depends(db_session),
                          _: dict[str, Any] = Depends(require_user)) -> dict[str, Any]:
    active = bool(payload.get("active", False))
    db.execute(text("UPDATE event_athletes SET active = :active, updated_at = now() WHERE id = :id"), {"id": event_athlete_id, "active": active})
    return {"ok": True}


@app.get("/action-standards")
def action_standards(db: Session = Depends(db_session), _: dict[str, Any] = Depends(require_user)) -> list[dict[str, Any]]:
    rows = db.execute(
        text(
            "SELECT s.*, c.name AS category_name FROM action_standards s "
            "JOIN action_categories c ON c.id = s.category_id WHERE s.active = true ORDER BY c.sort_order, s.name"
        )
    ).mappings()
    return [standard_row(dict(row)) for row in rows]


@app.post("/action-standards")
def save_action_standard(payload: dict[str, Any] = Body(...),
                         db: Session = Depends(db_session),
                         _: dict[str, Any] = Depends(require_user)) -> dict[str, Any]:
    standard_id = parse_uuid(payload.get("id")) or new_uuid()
    category_id = parse_uuid(payload.get("categoryId"))
    if not category_id:
        category_id = db.execute(text("SELECT id::text FROM action_categories ORDER BY sort_order LIMIT 1")).scalar()
    current_version = db.execute(text("SELECT version FROM action_standards WHERE id = :id"), {"id": standard_id}).scalar()
    version = int(current_version or payload.get("version", 0) or 0) + (1 if current_version else 0)
    if version <= 0:
        version = 1
    weights = {
        "detection": float(payload.get("detectionWeight", 0.28)),
        "symmetry": float(payload.get("symmetryWeight", 0.18)),
        "balance": float(payload.get("balanceWeight", 0.22)),
        "stability": float(payload.get("stabilityWeight", 0.17)),
        "depth": float(payload.get("depthWeight", 0.15)),
    }
    minimums = {
        "detection": int(payload.get("detectionMin", 55)),
        "symmetry": int(payload.get("symmetryMin", 60)),
        "balance": int(payload.get("balanceMin", 60)),
        "stability": int(payload.get("stabilityMin", 60)),
        "depth": int(payload.get("depthMin", 55)),
    }
    issue = {
        "title": payload.get("issueTitle") or "",
        "bodyPart": payload.get("issueBodyPart") or "",
        "cause": payload.get("issueCause") or "",
        "correction": payload.get("issueCorrection") or "",
        "priority": int(payload.get("issuePriority", 2)),
    }
    db.execute(
        text(
            "INSERT INTO action_standards "
            "(id, code, name, category_id, level, purpose, version, target_reps, target_score, set_count, "
            "rest_seconds, arm_threshold, release_threshold, debounce_ms, weights, minimums, phases, key_points, "
            "issue, reference_video_source, reference_repetition_id, reference_notes, active, updated_at) "
            "VALUES (:id, :code, :name, :category_id, :level, :purpose, :version, :target_reps, :target_score, "
            ":set_count, :rest_seconds, :arm_threshold, :release_threshold, :debounce_ms, CAST(:weights AS jsonb), "
            "CAST(:minimums AS jsonb), CAST(:phases AS jsonb), CAST(:key_points AS jsonb), CAST(:issue AS jsonb), "
            ":reference_video_source, :reference_repetition_id, :reference_notes, true, now()) "
            "ON CONFLICT (id) DO UPDATE SET code=excluded.code, name=excluded.name, category_id=excluded.category_id, "
            "level=excluded.level, purpose=excluded.purpose, version=:version, target_reps=excluded.target_reps, "
            "target_score=excluded.target_score, set_count=excluded.set_count, rest_seconds=excluded.rest_seconds, "
            "arm_threshold=excluded.arm_threshold, release_threshold=excluded.release_threshold, debounce_ms=excluded.debounce_ms, "
            "weights=excluded.weights, minimums=excluded.minimums, phases=excluded.phases, key_points=excluded.key_points, "
            "issue=excluded.issue, reference_video_source=excluded.reference_video_source, "
            "reference_repetition_id=excluded.reference_repetition_id, reference_notes=excluded.reference_notes, updated_at=now()"
        ),
        {
            "id": standard_id,
            "code": payload.get("code") or f"standard_{standard_id}",
            "name": payload.get("name") or "未命名动作",
            "category_id": category_id,
            "level": payload.get("level") or None,
            "purpose": payload.get("purpose") or None,
            "version": version,
            "target_reps": int(payload.get("targetReps", 10)),
            "target_score": int(payload.get("targetScore", 80)),
            "set_count": int(payload.get("setCount", 1)),
            "rest_seconds": int(payload.get("restSeconds", 60)),
            "arm_threshold": float(payload.get("armThreshold", 0.28)),
            "release_threshold": float(payload.get("releaseThreshold", 0.18)),
            "debounce_ms": int(payload.get("debounceMs", 900)),
            "weights": __import__("json").dumps(weights, ensure_ascii=False),
            "minimums": __import__("json").dumps(minimums, ensure_ascii=False),
            "phases": __import__("json").dumps(payload.get("phases") or "", ensure_ascii=False),
            "key_points": __import__("json").dumps(payload.get("keyPoints") or "", ensure_ascii=False),
            "issue": __import__("json").dumps(issue, ensure_ascii=False),
            "reference_video_source": payload.get("referenceVideoSource") or None,
            "reference_repetition_id": parse_uuid(payload.get("referenceRepetitionId")),
            "reference_notes": payload.get("referenceNotes") or None,
        },
    )
    row = db.execute(
        text(
            "SELECT s.*, c.name AS category_name FROM action_standards s "
            "JOIN action_categories c ON c.id = s.category_id WHERE s.id = :id"
        ),
        {"id": standard_id},
    ).mappings().one()
    return standard_row(dict(row))


def refresh_baseline(db: Session, athlete_id: str, action_standard_id: str) -> None:
    row = db.execute(
        text(
            "SELECT COUNT(*) AS session_count, COALESCE(ROUND(AVG(average_score)), 0) AS average_score, "
            "COALESCE(ROUND(AVG(valid_reps)), 0) AS average_valid_reps "
            "FROM training_sessions WHERE athlete_id = :athlete_id AND action_standard_id = :action_standard_id"
        ),
        {"athlete_id": athlete_id, "action_standard_id": action_standard_id},
    ).mappings().one()
    db.execute(
        text(
            "INSERT INTO athlete_action_baselines "
            "(athlete_id, action_standard_id, session_count, average_score, average_valid_reps, updated_at) "
            "VALUES (:athlete_id, :action_standard_id, :session_count, :average_score, :average_valid_reps, now()) "
            "ON CONFLICT (athlete_id, action_standard_id) DO UPDATE SET session_count=excluded.session_count, "
            "average_score=excluded.average_score, average_valid_reps=excluded.average_valid_reps, updated_at=now()"
        ),
        {
            "athlete_id": athlete_id,
            "action_standard_id": action_standard_id,
            "session_count": int(row["session_count"] or 0),
            "average_score": int(row["average_score"] or 0),
            "average_valid_reps": int(row["average_valid_reps"] or 0),
        },
    )


@app.post("/training/tasks/ensure-daily")
def ensure_daily_task(payload: dict[str, Any] = Body(...),
                      db: Session = Depends(db_session),
                      _: dict[str, Any] = Depends(require_user)) -> dict[str, Any]:
    athlete_id = parse_uuid(payload.get("athleteId"))
    action_standard_id = parse_uuid(payload.get("actionStandardId"))
    if not athlete_id or not action_standard_id:
        raise HTTPException(status_code=400, detail="athleteId and actionStandardId are required")
    coach_id = parse_uuid(payload.get("coachId"))
    training_date = parse_date(payload.get("date"))
    plan_name = f"{training_date.isoformat()} 训练计划"
    plan_id = db.execute(
        text(
            "SELECT id::text FROM training_plans WHERE athlete_id = :athlete_id AND training_date = :training_date "
            "ORDER BY created_at LIMIT 1"
        ),
        {"athlete_id": athlete_id, "training_date": training_date},
    ).scalar()
    if not plan_id:
        plan_id = new_uuid()
        db.execute(
            text(
                "INSERT INTO training_plans "
                "(id, athlete_id, coach_id, name, training_date, site, training_phase, goal, status, updated_at) "
                "VALUES (:id, :athlete_id, :coach_id, :name, :training_date, :site, :training_phase, :goal, 'active', now())"
            ),
            {
                "id": plan_id,
                "athlete_id": athlete_id,
                "coach_id": coach_id,
                "name": plan_name,
                "training_date": training_date,
                "site": payload.get("site") or None,
                "training_phase": payload.get("trainingPhase") or None,
                "goal": payload.get("goal") or None,
            },
        )
    else:
        db.execute(
            text(
                "UPDATE training_plans SET coach_id = :coach_id, site = :site, training_phase = :training_phase, "
                "goal = :goal, updated_at = now() WHERE id = :id"
            ),
            {
                "id": plan_id,
                "coach_id": coach_id,
                "site": payload.get("site") or None,
                "training_phase": payload.get("trainingPhase") or None,
                "goal": payload.get("goal") or None,
            },
        )
    task_id = db.execute(
        text(
            "SELECT id::text FROM training_tasks WHERE plan_id = :plan_id AND action_standard_id = :action_standard_id "
            "ORDER BY created_at LIMIT 1"
        ),
        {"plan_id": plan_id, "action_standard_id": action_standard_id},
    ).scalar()
    task_values = {
        "standard_version": int(payload.get("standardVersion", 1)),
        "target_reps": int(payload.get("targetReps", 10)),
        "target_score": int(payload.get("targetScore", 80)),
        "set_count": int(payload.get("setCount", 1)),
        "rest_seconds": int(payload.get("restSeconds", 60)),
    }
    if not task_id:
        task_id = new_uuid()
        db.execute(
            text(
                "INSERT INTO training_tasks "
                "(id, plan_id, action_standard_id, standard_version, target_reps, target_score, set_count, rest_seconds, status, updated_at) "
                "VALUES (:id, :plan_id, :action_standard_id, :standard_version, :target_reps, :target_score, :set_count, :rest_seconds, 'active', now())"
            ),
            {"id": task_id, "plan_id": plan_id, "action_standard_id": action_standard_id, **task_values},
        )
    else:
        db.execute(
            text(
                "UPDATE training_tasks SET standard_version=:standard_version, target_reps=:target_reps, "
                "target_score=:target_score, set_count=:set_count, rest_seconds=:rest_seconds, updated_at=now() WHERE id=:id"
            ),
            {"id": task_id, **task_values},
        )
    return {"planId": plan_id, "taskId": task_id}


@app.post("/offline-analysis/tasks")
def save_offline_analysis_task(payload: dict[str, Any] = Body(...),
                               db: Session = Depends(db_session),
                               _: dict[str, Any] = Depends(require_user)) -> dict[str, Any]:
    task_id = parse_uuid(payload.get("id")) or new_uuid()
    video_path = str(payload.get("videoPath") or "").strip()
    if not video_path:
        raise HTTPException(status_code=400, detail="videoPath is required")
    status_value = payload.get("status") or "imported"
    if status_value not in {"imported", "analyzing", "completed", "failed", "archived"}:
        status_value = "imported"
    db.execute(
        text(
            "INSERT INTO offline_analysis_tasks "
            "(id, batch_id, camera_id, time_offset_ms, video_path, file_name, file_size_bytes, file_modified_at, "
            "duration_ms, status, probe_metadata, summary_metadata, updated_at) "
            "VALUES (:id, :batch_id, :camera_id, :time_offset_ms, :video_path, :file_name, :file_size_bytes, :file_modified_at, "
            ":duration_ms, :status, CAST(:probe_metadata AS jsonb), CAST(:summary_metadata AS jsonb), now()) "
            "ON CONFLICT (id) DO UPDATE SET batch_id=excluded.batch_id, camera_id=excluded.camera_id, "
            "time_offset_ms=excluded.time_offset_ms, video_path=excluded.video_path, file_name=excluded.file_name, "
            "file_size_bytes=excluded.file_size_bytes, file_modified_at=excluded.file_modified_at, "
            "duration_ms=excluded.duration_ms, status=excluded.status, probe_metadata=excluded.probe_metadata, "
            "summary_metadata=excluded.summary_metadata, updated_at=now() RETURNING *"
        ),
        {
            "id": task_id,
            "batch_id": parse_uuid(payload.get("batchId")),
            "camera_id": int(payload.get("cameraId") or 0),
            "time_offset_ms": int(payload.get("timeOffsetMs") or 0),
            "video_path": video_path,
            "file_name": payload.get("fileName") or None,
            "file_size_bytes": int(payload.get("fileSizeBytes")) if str(payload.get("fileSizeBytes", "")).strip() not in {"", "-1"} else None,
            "file_modified_at": parse_dt(payload.get("fileModifiedAt")) if payload.get("fileModifiedAt") else None,
            "duration_ms": int(payload.get("durationMs") or 0),
            "status": status_value,
            "probe_metadata": json.dumps(metadata_from_payload(payload.get("probeMetadata")), ensure_ascii=False),
            "summary_metadata": json.dumps(metadata_from_payload(payload.get("summaryMetadata")), ensure_ascii=False),
        },
    )
    row = db.execute(text("SELECT * FROM offline_analysis_tasks WHERE id = :id"), {"id": task_id}).mappings().one()
    return offline_analysis_task_row(dict(row))


@app.get("/offline-analysis/tasks")
def offline_analysis_tasks(batchId: str = "",
                           status: str = "",
                           db: Session = Depends(db_session),
                           _: dict[str, Any] = Depends(require_user)) -> list[dict[str, Any]]:
    where: list[str] = []
    args: dict[str, Any] = {}
    if batchId:
        where.append("batch_id = :batch_id")
        args["batch_id"] = parse_uuid(batchId)
    if status:
        where.append("status = :status")
        args["status"] = status
    where_sql = "WHERE " + " AND ".join(where) if where else ""
    rows = db.execute(
        text(f"SELECT * FROM offline_analysis_tasks {where_sql} ORDER BY created_at DESC, id DESC"),
        args,
    ).mappings()
    return [offline_analysis_task_row(dict(row)) for row in rows]


def insert_repetition(db: Session,
                      session_id: str,
                      action_standard_id: str,
                      repetition: dict[str, Any],
                      video_file_by_index: dict[int, str] | None = None) -> str:
    rep_id = parse_uuid(repetition.get("id")) or new_uuid()
    import json
    video_index = int(repetition.get("videoIndex") or 1)
    video_file_id = parse_uuid(repetition.get("videoFileId"))
    if not video_file_id and video_file_by_index:
        video_file_id = parse_uuid(video_file_by_index.get(video_index))

    db.execute(
        text(
            "INSERT INTO action_repetitions "
            "(id, session_id, action_standard_id, standard_version, started_ms, ended_ms, valid, score, scores, "
            "error_codes, feedback, key_frame_ms, video_file_id, video_index, video_clip_start_ms, video_clip_end_ms, source, review_status, "
            "reviewer_coach_id, reviewed_at, manual_started_ms, manual_ended_ms, manual_valid, manual_score, manual_scores, "
            "manual_error_codes, manual_feedback, coach_note, key_frame_pose, participant_id, athlete_id, track_id, "
            "camera_id, frame_time_ms, identity_status, identity_confidence, identity_source) "
            "VALUES (:id, :session_id, :action_standard_id, :standard_version, :started_ms, :ended_ms, :valid, :score, "
            "CAST(:scores AS jsonb), CAST(:error_codes AS jsonb), :feedback, :key_frame_ms, :video_file_id, :video_index, :video_clip_start_ms, "
            ":video_clip_end_ms, :source, :review_status, :reviewer_coach_id, :reviewed_at, :manual_started_ms, "
            ":manual_ended_ms, :manual_valid, :manual_score, CAST(:manual_scores AS jsonb), CAST(:manual_error_codes AS jsonb), "
            ":manual_feedback, :coach_note, CAST(:key_frame_pose AS jsonb), :participant_id, :athlete_id, :track_id, "
            ":camera_id, :frame_time_ms, :identity_status, :identity_confidence, :identity_source)"
        ),
        {
            "id": rep_id,
            "session_id": session_id,
            "action_standard_id": parse_uuid(repetition.get("actionStandardId")) or action_standard_id,
            "standard_version": int(repetition.get("standardVersion", 1)),
            "started_ms": int(repetition.get("startedMs", 0)),
            "ended_ms": int(repetition.get("endedMs", 0)),
            "valid": bool(repetition.get("valid", False)),
            "score": int(repetition.get("score", 0)),
            "scores": json.dumps(scores_from_payload(repetition), ensure_ascii=False),
            "error_codes": json.dumps(repetition.get("errorCodes") or "", ensure_ascii=False),
            "feedback": repetition.get("feedback") or None,
            "key_frame_ms": int(repetition.get("keyFrameMs", 0)),
            "video_file_id": video_file_id,
            "video_index": video_index,
            "video_clip_start_ms": int(repetition.get("videoClipStartMs", 0)),
            "video_clip_end_ms": int(repetition.get("videoClipEndMs", 0)),
            "source": repetition.get("source") or "ai",
            "review_status": repetition.get("reviewStatus") or "unreviewed",
            "reviewer_coach_id": parse_uuid(repetition.get("reviewerCoachId")),
            "reviewed_at": parse_dt(repetition.get("reviewedAt")) if repetition.get("reviewedAt") else None,
            "manual_started_ms": repetition.get("manualStartedMs") if int(repetition.get("manualStartedMs", -1)) >= 0 else None,
            "manual_ended_ms": repetition.get("manualEndedMs") if int(repetition.get("manualEndedMs", -1)) >= 0 else None,
            "manual_valid": bool(repetition.get("manualValid")) if int(repetition.get("manualValid", -1)) >= 0 else None,
            "manual_score": repetition.get("manualScore") if int(repetition.get("manualScore", -1)) >= 0 else None,
            "manual_scores": json.dumps(scores_from_payload(repetition, "manual"), ensure_ascii=False),
            "manual_error_codes": json.dumps(repetition.get("manualErrorCodes") or "", ensure_ascii=False),
            "manual_feedback": repetition.get("manualFeedback") or None,
            "coach_note": repetition.get("coachNote") or None,
            "key_frame_pose": json.dumps(repetition.get("keyFramePoseJson") or "", ensure_ascii=False),
            "participant_id": parse_uuid(repetition.get("participantId")),
            "athlete_id": parse_uuid(repetition.get("athleteId")),
            "track_id": int(repetition.get("trackId")) if int(repetition.get("trackId", -1)) >= 0 else None,
            "camera_id": int(repetition.get("cameraId")) if int(repetition.get("cameraId", -1)) >= 0 else None,
            "frame_time_ms": int(repetition.get("frameTimeMs")) if str(repetition.get("frameTimeMs", "")).strip() else None,
            "identity_status": repetition.get("identityStatus") or "unknown",
            "identity_confidence": repetition.get("identityConfidence") if float(repetition.get("identityConfidence", -1)) >= 0 else None,
            "identity_source": repetition.get("identitySource") or None,
        },
    )
    return rep_id


def insert_participant_repetition(db: Session,
                                  session_id: str,
                                  action_standard_id: str,
                                  repetition: dict[str, Any],
                                  participant_by_athlete: dict[str, str],
                                  video_file_by_index: dict[int, str] | None = None,
                                  action_repetition_id: str | None = None) -> str:
    import json

    rep_id = parse_uuid(repetition.get("participantRepetitionId")) or parse_uuid(repetition.get("id")) or new_uuid()
    video_index = int(repetition.get("videoIndex") or 1)
    video_file_id = parse_uuid(repetition.get("videoFileId"))
    if not video_file_id and video_file_by_index:
        video_file_id = parse_uuid(video_file_by_index.get(video_index))
    athlete_id = parse_uuid(repetition.get("athleteId"))
    participant_id = parse_uuid(repetition.get("participantId"))
    if athlete_id and not participant_id:
        participant_id = parse_uuid(participant_by_athlete.get(str(athlete_id)))

    db.execute(
        text(
            "INSERT INTO participant_repetitions "
            "(id, session_id, participant_id, athlete_id, action_repetition_id, action_standard_id, standard_version, "
            "started_ms, ended_ms, valid, score, scores, error_codes, feedback, key_frame_ms, video_file_id, video_index, "
            "video_clip_start_ms, video_clip_end_ms, source, review_status, reviewer_coach_id, reviewed_at, "
            "manual_started_ms, manual_ended_ms, manual_valid, manual_score, manual_scores, manual_error_codes, "
            "manual_feedback, coach_note, key_frame_pose, track_id, camera_id, frame_time_ms, identity_status, "
            "identity_confidence, identity_source) "
            "VALUES (:id, :session_id, :participant_id, :athlete_id, :action_repetition_id, :action_standard_id, "
            ":standard_version, :started_ms, :ended_ms, :valid, :score, CAST(:scores AS jsonb), CAST(:error_codes AS jsonb), "
            ":feedback, :key_frame_ms, :video_file_id, :video_index, :video_clip_start_ms, :video_clip_end_ms, :source, "
            ":review_status, :reviewer_coach_id, :reviewed_at, :manual_started_ms, :manual_ended_ms, :manual_valid, "
            ":manual_score, CAST(:manual_scores AS jsonb), CAST(:manual_error_codes AS jsonb), :manual_feedback, "
            ":coach_note, CAST(:key_frame_pose AS jsonb), :track_id, :camera_id, :frame_time_ms, :identity_status, "
            ":identity_confidence, :identity_source)"
        ),
        {
            "id": rep_id,
            "session_id": session_id,
            "participant_id": participant_id,
            "athlete_id": athlete_id,
            "action_repetition_id": parse_uuid(action_repetition_id),
            "action_standard_id": parse_uuid(repetition.get("actionStandardId")) or action_standard_id,
            "standard_version": int(repetition.get("standardVersion", 1)),
            "started_ms": int(repetition.get("startedMs", 0)),
            "ended_ms": int(repetition.get("endedMs", 0)),
            "valid": bool(repetition.get("valid", False)),
            "score": int(repetition.get("score", 0)),
            "scores": json.dumps(scores_from_payload(repetition), ensure_ascii=False),
            "error_codes": json.dumps(repetition.get("errorCodes") or "", ensure_ascii=False),
            "feedback": repetition.get("feedback") or None,
            "key_frame_ms": int(repetition.get("keyFrameMs", 0)),
            "video_file_id": video_file_id,
            "video_index": video_index,
            "video_clip_start_ms": int(repetition.get("videoClipStartMs", 0)),
            "video_clip_end_ms": int(repetition.get("videoClipEndMs", 0)),
            "source": repetition.get("source") or "ai",
            "review_status": repetition.get("reviewStatus") or "unreviewed",
            "reviewer_coach_id": parse_uuid(repetition.get("reviewerCoachId")),
            "reviewed_at": parse_dt(repetition.get("reviewedAt")) if repetition.get("reviewedAt") else None,
            "manual_started_ms": repetition.get("manualStartedMs") if int(repetition.get("manualStartedMs", -1)) >= 0 else None,
            "manual_ended_ms": repetition.get("manualEndedMs") if int(repetition.get("manualEndedMs", -1)) >= 0 else None,
            "manual_valid": bool(repetition.get("manualValid")) if int(repetition.get("manualValid", -1)) >= 0 else None,
            "manual_score": repetition.get("manualScore") if int(repetition.get("manualScore", -1)) >= 0 else None,
            "manual_scores": json.dumps(scores_from_payload(repetition, "manual"), ensure_ascii=False),
            "manual_error_codes": json.dumps(repetition.get("manualErrorCodes") or "", ensure_ascii=False),
            "manual_feedback": repetition.get("manualFeedback") or None,
            "coach_note": repetition.get("coachNote") or None,
            "key_frame_pose": json.dumps(repetition.get("keyFramePoseJson") or "", ensure_ascii=False),
            "track_id": int(repetition.get("trackId")) if int(repetition.get("trackId", -1)) >= 0 else None,
            "camera_id": int(repetition.get("cameraId")) if int(repetition.get("cameraId", -1)) >= 0 else None,
            "frame_time_ms": int(repetition.get("frameTimeMs")) if str(repetition.get("frameTimeMs", "")).strip() else None,
            "identity_status": repetition.get("identityStatus") or "unknown",
            "identity_confidence": repetition.get("identityConfidence") if float(repetition.get("identityConfidence", -1)) >= 0 else None,
            "identity_source": repetition.get("identitySource") or None,
        },
    )
    return rep_id


def insert_participant_pose_frame(db: Session,
                                  session_id: str,
                                  frame: dict[str, Any],
                                  participant_by_athlete: dict[str, str],
                                  video_file_by_index: dict[int, str] | None = None) -> str:
    frame_id = parse_uuid(frame.get("id")) or new_uuid()
    video_index = int(frame.get("videoIndex") or 1)
    video_file_id = parse_uuid(frame.get("videoFileId"))
    if not video_file_id and video_file_by_index:
        video_file_id = parse_uuid(video_file_by_index.get(video_index))
    athlete_id = parse_uuid(frame.get("athleteId"))
    participant_id = parse_uuid(frame.get("participantId"))
    if athlete_id and not participant_id:
        participant_id = parse_uuid(participant_by_athlete.get(str(athlete_id)))
    pose_summary = frame.get("poseSummary") or frame.get("poseSummaryJson") or {}
    if isinstance(pose_summary, str):
        try:
            pose_summary = json.loads(pose_summary)
        except json.JSONDecodeError:
            pose_summary = {}

    db.execute(
        text(
            "INSERT INTO participant_pose_frames "
            "(id, session_id, participant_id, athlete_id, video_file_id, video_index, frame_time_ms, camera_id, "
            "track_id, identity_status, identity_confidence, identity_source, bbox_x, bbox_y, bbox_width, bbox_height, "
            "anchor_x, anchor_y, field_x, field_y, has_field_point, pose_confidence, pose_summary) "
            "VALUES (:id, :session_id, :participant_id, :athlete_id, :video_file_id, :video_index, :frame_time_ms, "
            ":camera_id, :track_id, :identity_status, :identity_confidence, :identity_source, :bbox_x, :bbox_y, "
            ":bbox_width, :bbox_height, :anchor_x, :anchor_y, :field_x, :field_y, :has_field_point, "
            ":pose_confidence, CAST(:pose_summary AS jsonb))"
        ),
        {
            "id": frame_id,
            "session_id": session_id,
            "participant_id": participant_id,
            "athlete_id": athlete_id,
            "video_file_id": video_file_id,
            "video_index": video_index,
            "frame_time_ms": int(frame.get("frameTimeMs") or 0),
            "camera_id": int(frame.get("cameraId") or 0),
            "track_id": int(frame.get("trackId")) if int(frame.get("trackId", -1)) >= 0 else None,
            "identity_status": frame.get("identityStatus") or "unknown",
            "identity_confidence": frame.get("identityConfidence") if float(frame.get("identityConfidence", -1)) >= 0 else None,
            "identity_source": frame.get("identitySource") or None,
            "bbox_x": frame.get("bboxX"),
            "bbox_y": frame.get("bboxY"),
            "bbox_width": frame.get("bboxWidth"),
            "bbox_height": frame.get("bboxHeight"),
            "anchor_x": frame.get("anchorX"),
            "anchor_y": frame.get("anchorY"),
            "field_x": frame.get("fieldX") if frame.get("hasFieldPoint") else None,
            "field_y": frame.get("fieldY") if frame.get("hasFieldPoint") else None,
            "has_field_point": bool(frame.get("hasFieldPoint", False)),
            "pose_confidence": frame.get("poseConfidence") if float(frame.get("poseConfidence", -1)) >= 0 else None,
            "pose_summary": json.dumps(pose_summary, ensure_ascii=False),
        },
    )
    return frame_id


def participant_row(row: dict[str, Any]) -> dict[str, Any]:
    return {
        "id": str(row["id"]),
        "sessionId": str(row["session_id"]),
        "athleteId": str(row["athlete_id"]),
        "athleteName": row.get("athlete_name") or "",
        "slotIndex": row["slot_index"],
        "role": row["role"],
        "trackLabel": row.get("track_label") or "",
        "notes": row.get("notes") or "",
        "active": bool(row.get("active", True)),
    }


def participants_for_sessions(db: Session, session_ids: list[str]) -> dict[str, list[dict[str, Any]]]:
    if not session_ids:
        return {}
    rows = db.execute(
        text(
            "SELECT tsp.*, a.name AS athlete_name FROM training_session_participants tsp "
            "JOIN athletes a ON a.id = tsp.athlete_id "
            "WHERE tsp.session_id = ANY(:session_ids) ORDER BY tsp.session_id, tsp.slot_index"
        ),
        {"session_ids": session_ids},
    ).mappings()
    grouped: dict[str, list[dict[str, Any]]] = {}
    for row in rows:
        item = participant_row(dict(row))
        grouped.setdefault(item["sessionId"], []).append(item)
    return grouped


def video_file_row(row: dict[str, Any]) -> dict[str, Any]:
    metadata = row.get("metadata") or {}
    if isinstance(metadata, str):
        try:
            metadata = json.loads(metadata)
        except json.JSONDecodeError:
            metadata = {}
    return {
        "id": str(row["id"]),
        "sessionId": str(row["session_id"]),
        "videoIndex": row["video_index"],
        "camera": row["camera"],
        "cameraName": row["camera_name"] or "",
        "sourceUrl": row["source_url"] or "",
        "fallbackUrl": row["fallback_url"] or "",
        "storageRoot": row["storage_root"] or "",
        "relativeDir": row["relative_dir"] or "",
        "fileName": row["file_name"] or "",
        "filePath": row["file_path"] or "",
        "metadataPath": row["metadata_path"] or "",
        "status": row["status"] or "planned",
        "sessionStartMs": row["session_start_ms"],
        "sessionEndMs": row["session_end_ms"],
        "durationMs": row["duration_ms"],
        "fileSizeBytes": row["file_size_bytes"] if row["file_size_bytes"] is not None else -1,
        "fileModifiedAt": row["file_modified_at"].isoformat() if row["file_modified_at"] else "",
        "checksumAlgorithm": row["checksum_algorithm"] or "",
        "checksumValue": row["checksum_value"] or "",
        "metadata": metadata,
    }


def video_files_for_sessions(db: Session, session_ids: list[str]) -> dict[str, list[dict[str, Any]]]:
    if not session_ids:
        return {}
    rows = db.execute(
        text("SELECT * FROM training_video_files WHERE session_id = ANY(:session_ids) ORDER BY session_id, video_index"),
        {"session_ids": session_ids},
    ).mappings()
    grouped: dict[str, list[dict[str, Any]]] = {}
    for row in rows:
        item = video_file_row(dict(row))
        grouped.setdefault(item["sessionId"], []).append(item)
    return grouped


def video_file_cleanup_row(row: dict[str, Any]) -> dict[str, Any]:
    item = video_file_row(row)
    item.update({
        "athleteName": row.get("athlete_name") or "",
        "sessionStartedAt": row["session_started_at"].isoformat() if row.get("session_started_at") else "",
        "actionCount": int(row.get("action_count") or 0),
    })
    return item


@app.get("/training/video-files")
def training_video_files(status: str = "",
                         withLocalPathOnly: bool = False,
                         modifiedBefore: str = "",
                         db: Session = Depends(db_session),
                         _: dict[str, Any] = Depends(require_user)) -> list[dict[str, Any]]:
    where: list[str] = []
    args: dict[str, Any] = {}
    if status.strip():
        where.append("tvf.status = :status")
        args["status"] = status.strip()
    if withLocalPathOnly:
        where.append("COALESCE(tvf.file_path, '') <> ''")
    if modifiedBefore.strip():
        where.append("COALESCE(tvf.file_modified_at, tvf.created_at) <= :modified_before")
        args["modified_before"] = parse_dt(modifiedBefore)
    where_sql = "WHERE " + " AND ".join(where) if where else ""
    rows = db.execute(
        text(
            "SELECT tvf.*, ts.started_at AS session_started_at, COALESCE(a.name, '') AS athlete_name, "
            "COUNT(ar.id) AS action_count "
            "FROM training_video_files tvf "
            "JOIN training_sessions ts ON ts.id = tvf.session_id "
            "JOIN athletes a ON a.id = ts.athlete_id "
            "LEFT JOIN action_repetitions ar ON ar.video_file_id = tvf.id "
            f"{where_sql} "
            "GROUP BY tvf.id, ts.started_at, a.name "
            "ORDER BY COALESCE(tvf.file_modified_at, tvf.created_at) ASC, tvf.created_at ASC"
        ),
        args,
    ).mappings()
    return [video_file_cleanup_row(dict(row)) for row in rows]


@app.post("/training/video-files/{video_file_id}/cleanup")
def cleanup_video_file(video_file_id: str,
                       payload: dict[str, Any] = Body(default={}),
                       db: Session = Depends(db_session),
                       _: dict[str, Any] = Depends(require_user)) -> dict[str, Any]:
    row = db.execute(
        text("SELECT metadata FROM training_video_files WHERE id = :id"),
        {"id": parse_uuid(video_file_id)},
    ).mappings().first()
    if not row:
        raise HTTPException(status_code=404, detail="Video file not found")
    metadata = row["metadata"] or {}
    if isinstance(metadata, str):
        try:
            metadata = json.loads(metadata)
        except json.JSONDecodeError:
            metadata = {}
    metadata["cleanupDeletedAt"] = now().isoformat()
    metadata["cleanupReason"] = payload.get("reason") or "manual_cleanup"
    metadata["cleanupMissing"] = True
    db.execute(
        text(
            "UPDATE training_video_files SET metadata = CAST(:metadata AS jsonb), updated_at = now() "
            "WHERE id = :id"
        ),
        {"id": parse_uuid(video_file_id), "metadata": json.dumps(metadata, ensure_ascii=False)},
    )
    return {"ok": True}


def save_training_video_files(db: Session, session_id: str, files: list[dict[str, Any]]) -> dict[int, str]:
    db.execute(text("DELETE FROM training_video_files WHERE session_id = :session_id"), {"session_id": session_id})
    video_file_by_index: dict[int, str] = {}
    for index, item in enumerate(files, start=1):
        status_value = item.get("status") or "planned"
        if status_value not in {"planned", "external", "recorded"}:
            status_value = "planned"
        video_index = int(item.get("videoIndex") or index)
        file_id = parse_uuid(item.get("id")) or new_uuid()
        metadata = item.get("metadata") or {}
        if isinstance(metadata, str):
            try:
                metadata = json.loads(metadata)
            except json.JSONDecodeError:
                metadata = {}
        db.execute(
            text(
                "INSERT INTO training_video_files "
                "(id, session_id, video_index, camera, camera_name, source_url, fallback_url, storage_root, relative_dir, "
                "file_name, file_path, metadata_path, status, session_start_ms, session_end_ms, duration_ms, "
                "file_size_bytes, file_modified_at, checksum_algorithm, checksum_value, metadata, updated_at) "
                "VALUES (:id, :session_id, :video_index, :camera, :camera_name, :source_url, :fallback_url, :storage_root, "
                ":relative_dir, :file_name, :file_path, :metadata_path, :status, :session_start_ms, :session_end_ms, "
                ":duration_ms, :file_size_bytes, :file_modified_at, :checksum_algorithm, :checksum_value, CAST(:metadata AS jsonb), now())"
            ),
            {
                "id": file_id,
                "session_id": session_id,
                "video_index": video_index,
                "camera": int(item.get("camera") or 0),
                "camera_name": item.get("cameraName") or None,
                "source_url": item.get("sourceUrl") or None,
                "fallback_url": item.get("fallbackUrl") or None,
                "storage_root": item.get("storageRoot") or None,
                "relative_dir": item.get("relativeDir") or None,
                "file_name": item.get("fileName") or None,
                "file_path": item.get("filePath") or None,
                "metadata_path": item.get("metadataPath") or None,
                "status": status_value,
                "session_start_ms": int(item.get("sessionStartMs") or 0),
                "session_end_ms": int(item.get("sessionEndMs") or 0),
                "duration_ms": int(item.get("durationMs") or 0),
                "file_size_bytes": int(item.get("fileSizeBytes")) if str(item.get("fileSizeBytes", "")).strip() not in {"", "-1"} else None,
                "file_modified_at": parse_dt(item.get("fileModifiedAt")) if item.get("fileModifiedAt") else None,
                "checksum_algorithm": item.get("checksumAlgorithm") or None,
                "checksum_value": item.get("checksumValue") or None,
                "metadata": json.dumps(metadata, ensure_ascii=False),
            },
        )
        video_file_by_index[video_index] = str(file_id)
    return video_file_by_index


def save_session_participants(db: Session, session_id: str, primary_athlete_id: str, participants: list[dict[str, Any]]) -> dict[str, str]:
    normalized: list[dict[str, Any]] = []
    seen: set[str] = set()
    primary_key = str(primary_athlete_id)
    for item in [{"athleteId": primary_key, "slotIndex": 1, "role": "primary"}, *participants]:
        athlete_id = parse_uuid(item.get("athleteId"))
        if not athlete_id:
            continue
        athlete_key = str(athlete_id)
        if athlete_key in seen:
            continue
        seen.add(athlete_key)
        normalized.append({
            "id": parse_uuid(item.get("id")) or new_uuid(),
            "session_id": session_id,
            "athlete_id": athlete_id,
            "slot_index": len(normalized) + 1,
            "role": "primary" if not normalized else "participant",
            "track_label": item.get("trackLabel") or None,
            "notes": item.get("notes") or None,
            "active": bool(item.get("active", True)),
        })
        if len(normalized) >= 4:
            break

    db.execute(text("DELETE FROM training_session_participants WHERE session_id = :session_id"), {"session_id": session_id})
    mapping: dict[str, str] = {}
    for item in normalized:
        db.execute(
            text(
                "INSERT INTO training_session_participants "
                "(id, session_id, athlete_id, slot_index, role, track_label, notes, active, updated_at) "
                "VALUES (:id, :session_id, :athlete_id, :slot_index, :role, :track_label, :notes, :active, now())"
            ),
            item,
        )
        mapping[str(item["athlete_id"])] = str(item["id"])
    return mapping


@app.post("/training/sessions")
def save_training_session(payload: dict[str, Any] = Body(...),
                          db: Session = Depends(db_session),
                          _: dict[str, Any] = Depends(require_user)) -> dict[str, Any]:
    session = payload.get("session", payload)
    repetitions = payload.get("repetitions", [])
    participant_repetitions = payload.get("participantRepetitions", [])
    participant_pose_frames = payload.get("participantPoseFrames", [])
    session_id = parse_uuid(session.get("id")) or new_uuid()
    athlete_id = parse_uuid(session.get("athleteId"))
    action_standard_id = parse_uuid(session.get("actionStandardId"))
    if not athlete_id or not action_standard_id:
        raise HTTPException(status_code=400, detail="athleteId and actionStandardId are required")
    competition_event_id = parse_uuid(session.get("competitionEventId"))
    event_athlete_id = parse_uuid(session.get("eventAthleteId"))
    competition_id = parse_uuid(session.get("competitionId"))
    if competition_event_id:
        event_row = db.execute(
            text("SELECT competition_id::text FROM competition_events WHERE id = :id"),
            {"id": competition_event_id},
        ).first()
        if not event_row:
            raise HTTPException(status_code=400, detail="competitionEventId is invalid")
        competition_id = event_row[0]
    source_type, source_ref = session_source(session, competition_id, competition_event_id)
    analysis_task_id = parse_uuid(session.get("analysisTaskId"))
    db.execute(
        text(
            "INSERT INTO training_sessions "
            "(id, athlete_id, coach_id, competition_id, competition_event_id, event_athlete_id, plan_id, task_id, action_standard_id, standard_version, legacy_qsettings_id, "
            "started_at, saved_at, duration_sec, total_reps, valid_reps, average_score, best_score, camera, "
            "model_precision, fps, scores, site, training_phase, goal, target_reps, target_score, set_count, rest_seconds, "
            "video_source, video_fallback_source, video_camera_name, analysis_task_id, source_type, source_ref, feedback, notes, coach_comment) "
            "VALUES (:id, :athlete_id, :coach_id, :competition_id, :competition_event_id, :event_athlete_id, :plan_id, :task_id, :action_standard_id, :standard_version, "
            ":legacy_qsettings_id, :started_at, :saved_at, :duration_sec, :total_reps, :valid_reps, :average_score, "
            ":best_score, :camera, :model_precision, :fps, CAST(:scores AS jsonb), :site, :training_phase, :goal, "
            ":target_reps, :target_score, :set_count, :rest_seconds, :video_source, :video_fallback_source, "
            ":video_camera_name, :analysis_task_id, :source_type, :source_ref, :feedback, :notes, :coach_comment) "
            "ON CONFLICT (id) DO UPDATE SET coach_id=excluded.coach_id, competition_id=excluded.competition_id, "
            "competition_event_id=excluded.competition_event_id, event_athlete_id=excluded.event_athlete_id, "
            "plan_id=excluded.plan_id, task_id=excluded.task_id, "
            "saved_at=excluded.saved_at, duration_sec=excluded.duration_sec, total_reps=excluded.total_reps, "
            "valid_reps=excluded.valid_reps, average_score=excluded.average_score, best_score=excluded.best_score, "
            "scores=excluded.scores, analysis_task_id=excluded.analysis_task_id, source_type=excluded.source_type, source_ref=excluded.source_ref, "
            "feedback=excluded.feedback, notes=excluded.notes, coach_comment=excluded.coach_comment"
        ),
        {
            "id": session_id,
            "athlete_id": athlete_id,
            "coach_id": parse_uuid(session.get("coachId")),
            "competition_id": competition_id,
            "competition_event_id": competition_event_id,
            "event_athlete_id": event_athlete_id,
            "plan_id": parse_uuid(session.get("planId")),
            "task_id": parse_uuid(session.get("taskId")),
            "action_standard_id": action_standard_id,
            "standard_version": int(session.get("standardVersion", 1)),
            "legacy_qsettings_id": int(session.get("legacyQsettingsId", 0) or 0),
            "started_at": parse_dt(session.get("startedAt")),
            "saved_at": parse_dt(session.get("savedAt")),
            "duration_sec": int(session.get("durationSec", 0)),
            "total_reps": int(session.get("totalReps", 0)),
            "valid_reps": int(session.get("validReps", 0)),
            "average_score": int(session.get("averageScore", 0)),
            "best_score": int(session.get("bestScore", 0)),
            "camera": int(session.get("camera", 1)),
            "model_precision": session.get("modelPrecision") or None,
            "fps": int(session.get("fps", 30)),
            "scores": json.dumps(scores_from_payload(session), ensure_ascii=False),
            "site": session.get("site") or None,
            "training_phase": session.get("trainingPhase") or None,
            "goal": session.get("goal") or None,
            "target_reps": int(session.get("targetReps", 0)),
            "target_score": int(session.get("targetScore", 0)),
            "set_count": int(session.get("setCount", 1)),
            "rest_seconds": int(session.get("restSeconds", 60)),
            "video_source": session.get("videoSource") or None,
            "video_fallback_source": session.get("videoFallbackSource") or None,
            "video_camera_name": session.get("videoCameraName") or None,
            "analysis_task_id": analysis_task_id,
            "source_type": source_type,
            "source_ref": source_ref,
            "feedback": session.get("feedback") or None,
            "notes": session.get("notes") or None,
            "coach_comment": session.get("coachComment") or None,
        },
    )
    participant_by_athlete = save_session_participants(db, session_id, athlete_id, session.get("participants", []))
    video_file_by_index = save_training_video_files(db, session_id, session.get("videoFiles", []))
    db.execute(text("DELETE FROM participant_pose_frames WHERE session_id = :session_id"), {"session_id": session_id})
    db.execute(text("DELETE FROM participant_repetitions WHERE session_id = :session_id"), {"session_id": session_id})
    db.execute(text("DELETE FROM action_repetitions WHERE session_id = :session_id"), {"session_id": session_id})
    action_repetition_ids: list[str] = []
    for repetition in repetitions:
        rep_athlete_id = parse_uuid(repetition.get("athleteId"))
        if rep_athlete_id and not parse_uuid(repetition.get("participantId")):
            repetition["participantId"] = participant_by_athlete.get(str(rep_athlete_id), "")
        action_repetition_ids.append(insert_repetition(db, session_id, action_standard_id, repetition, video_file_by_index))
    source_participant_repetitions = participant_repetitions or repetitions
    for index, repetition in enumerate(source_participant_repetitions):
        rep_athlete_id = parse_uuid(repetition.get("athleteId"))
        if rep_athlete_id and not parse_uuid(repetition.get("participantId")):
            repetition["participantId"] = participant_by_athlete.get(str(rep_athlete_id), "")
        linked_action_id = action_repetition_ids[index] if index < len(action_repetition_ids) else None
        insert_participant_repetition(db, session_id, action_standard_id, repetition, participant_by_athlete, video_file_by_index, linked_action_id)
    for frame in participant_pose_frames:
        insert_participant_pose_frame(db, session_id, frame, participant_by_athlete, video_file_by_index)
    if session.get("taskId"):
        status_value = "completed" if int(session.get("validReps", 0)) >= int(session.get("targetReps", 0)) else "active"
        db.execute(text("UPDATE training_tasks SET status = :status, updated_at = now() WHERE id = :id"), {"id": parse_uuid(session.get("taskId")), "status": status_value})
    refresh_baseline(db, athlete_id, action_standard_id)
    return {"id": session_id}


def history_row(row: dict[str, Any]) -> dict[str, Any]:
    scores = row["scores"] or {}
    source_type = row.get("source_type") or "training"
    source_label = "训练"
    if source_type == "competition":
        source_label = row.get("race_name") or row.get("competition_name") or "比赛"
    elif source_type == "offline_import":
        source_label = row.get("video_camera_name") or row.get("source_ref") or "导入视频"
    return {
        "id": str(row["id"]),
        "athleteId": str(row["athlete_id"]),
        "coachId": str(row["coach_id"] or ""),
        "competitionId": str(row["competition_id"] or ""),
        "competitionEventId": str(row["competition_event_id"] or ""),
        "eventAthleteId": str(row["event_athlete_id"] or ""),
        "planId": str(row["plan_id"] or ""),
        "taskId": str(row["task_id"] or ""),
        "actionStandardId": str(row["action_standard_id"]),
        "athleteName": row["athlete_name"] or "",
        "coachName": row["coach_name"] or "",
        "competitionName": row.get("competition_name") or "",
        "competitionLocation": row.get("competition_location") or "",
        "competitionDate": row["competition_date"].isoformat() if row.get("competition_date") else "",
        "competitionType": row.get("competition_type") or "",
        "competitionNotes": row.get("competition_notes") or "",
        "raceName": row.get("race_name") or "",
        "eventName": row.get("event_name") or "",
        "heatName": row.get("heat_name") or "",
        "groupName": row.get("group_name") or "",
        "eventScheduledAt": row["event_scheduled_at"].isoformat() if row.get("event_scheduled_at") else "",
        "eventNotes": row.get("event_notes") or "",
        "bibNumber": row.get("bib_number") or "",
        "laneNumber": row.get("lane_number") or "",
        "resultScore": row.get("result_score") if row.get("result_score") is not None else -1,
        "resultRank": row.get("result_rank") if row.get("result_rank") is not None else -1,
        "eventAthleteNotes": row.get("event_athlete_notes") or "",
        "actionName": row["action_name"] or "",
        "actionCategory": row["action_category"] or "",
        "standardVersion": row["standard_version"],
        "time": row["saved_at"].strftime("%Y-%m-%d %H:%M:%S") if row["saved_at"] else "",
        "startedAt": row["started_at"].isoformat() if row["started_at"] else "",
        "duration": row["duration_sec"],
        "totalReps": row["total_reps"],
        "validReps": row["valid_reps"],
        "targetReps": row["target_reps"],
        "targetScore": row["target_score"],
        "score": row["average_score"],
        "bestScore": row["best_score"],
        "camera": row["camera"],
        "modelPrecision": row["model_precision"] or "",
        "fps": row["fps"],
        "detectionScore": scores.get("detection", 0),
        "symmetryScore": scores.get("symmetry", 0),
        "balanceScore": scores.get("balance", 0),
        "stabilityScore": scores.get("stability", 0),
        "depthScore": scores.get("depth", 0),
        "site": row["site"] or "",
        "trainingPhase": row["training_phase"] or "",
        "goal": row["goal"] or "",
        "videoSource": row["video_source"] or "",
        "videoFallbackSource": row["video_fallback_source"] or "",
        "videoCameraName": row["video_camera_name"] or "",
        "analysisTaskId": str(row.get("analysis_task_id") or ""),
        "analysisTaskStatus": row.get("analysis_task_status") or "",
        "analysisTaskBatchId": str(row.get("analysis_task_batch_id") or ""),
        "analysisTaskCameraId": row.get("analysis_task_camera_id") if row.get("analysis_task_camera_id") is not None else 0,
        "analysisTaskTimeOffsetMs": row.get("analysis_task_time_offset_ms") if row.get("analysis_task_time_offset_ms") is not None else 0,
        "sourceType": source_type,
        "sourceRef": row.get("source_ref") or "",
        "sourceLabel": source_label,
        "feedback": row["feedback"] or "",
        "notes": row["notes"] or "",
        "coachComment": row["coach_comment"] or "",
    }


@app.get("/training/sessions")
def search_sessions(
    athleteId: str = "",
    coachId: str = "",
    actionStandardId: str = "",
    competitionId: str = "",
    competitionEventId: str = "",
    eventAthleteId: str = "",
    sourceType: str = "",
    savedFrom: str = "",
    savedTo: str = "",
    competitionText: str = "",
    minScore: int = -1,
    maxScore: int = -1,
    pageNumber: int = 1,
    pageSize: int = 10,
    sortField: str = "savedAt",
    descending: bool = True,
    db: Session = Depends(db_session),
    _: dict[str, Any] = Depends(require_user),
) -> dict[str, Any]:
    where: list[str] = []
    args: dict[str, Any] = {}
    for query_name, column in [
        ("coachId", "ts.coach_id"),
        ("actionStandardId", "ts.action_standard_id"),
        ("competitionId", "ts.competition_id"),
        ("competitionEventId", "ts.competition_event_id"),
        ("eventAthleteId", "ts.event_athlete_id"),
    ]:
        value = locals()[query_name]
        if value:
            where.append(f"{column} = :{query_name}")
            args[query_name] = parse_uuid(value)
    if athleteId:
        where.append("(ts.athlete_id = :athleteId OR EXISTS (SELECT 1 FROM training_session_participants tsp WHERE tsp.session_id = ts.id AND tsp.athlete_id = :athleteId))")
        args["athleteId"] = parse_uuid(athleteId)
    if sourceType:
        where.append("ts.source_type = :sourceType")
        args["sourceType"] = sourceType
    if savedFrom:
        where.append("ts.saved_at >= :savedFrom")
        args["savedFrom"] = parse_dt(savedFrom)
    if savedTo:
        where.append("ts.saved_at <= :savedTo")
        args["savedTo"] = parse_dt(savedTo)
    if minScore >= 0:
        where.append("ts.average_score >= :minScore")
        args["minScore"] = minScore
    if maxScore >= 0:
        where.append("ts.average_score <= :maxScore")
        args["maxScore"] = maxScore
    if competitionText.strip():
        where.append("(COALESCE(comp.name,'') ILIKE :q OR COALESCE(comp.location,'') ILIKE :q OR COALESCE(comp.competition_type,'') ILIKE :q OR COALESCE(comp.notes,'') ILIKE :q OR COALESCE(ce.race_name,'') ILIKE :q OR COALESCE(ce.event_name,'') ILIKE :q OR COALESCE(ce.heat_name,'') ILIKE :q OR COALESCE(ce.group_name,'') ILIKE :q OR COALESCE(ce.notes,'') ILIKE :q OR COALESCE(ea.bib_number,'') ILIKE :q OR COALESCE(ea.lane_number,'') ILIKE :q OR COALESCE(ea.notes,'') ILIKE :q OR COALESCE(ts.source_type,'') ILIKE :q OR COALESCE(ts.source_ref,'') ILIKE :q OR COALESCE(ts.site,'') ILIKE :q OR COALESCE(ts.training_phase,'') ILIKE :q OR COALESCE(ts.goal,'') ILIKE :q OR COALESCE(ts.notes,'') ILIKE :q OR COALESCE(ts.feedback,'') ILIKE :q OR COALESCE(ts.coach_comment,'') ILIKE :q)")
        args["q"] = f"%{competitionText.strip()}%"
    where_sql = "WHERE " + " AND ".join(where) if where else ""
    from_sql = (
        "FROM training_sessions ts JOIN athletes a ON a.id = ts.athlete_id "
        "LEFT JOIN coaches c ON c.id = ts.coach_id "
        "LEFT JOIN competitions comp ON comp.id = ts.competition_id "
        "LEFT JOIN competition_events ce ON ce.id = ts.competition_event_id "
        "LEFT JOIN event_athletes ea ON ea.id = ts.event_athlete_id "
        "LEFT JOIN offline_analysis_tasks oat ON oat.id = ts.analysis_task_id "
        "JOIN action_standards s ON s.id = ts.action_standard_id "
        "JOIN action_categories ac ON ac.id = s.category_id "
    )
    total = int(db.execute(text(f"SELECT COUNT(*) {from_sql} {where_sql}"), args).scalar() or 0)
    page_size = max(1, min(pageSize, 500))
    page_number = max(1, pageNumber)
    max_page = max(1, (total + page_size - 1) // page_size)
    page_number = min(page_number, max_page)
    sort_map = {
        "savedAt": "ts.saved_at",
        "averageScore": "ts.average_score",
        "bestScore": "ts.best_score",
        "validReps": "ts.valid_reps",
        "durationSec": "ts.duration_sec",
    }
    order = sort_map.get(sortField, "ts.saved_at")
    direction = "DESC" if descending else "ASC"
    args.update({"limit": page_size, "offset": (page_number - 1) * page_size})
    rows = list(db.execute(
        text(
            "SELECT ts.*, a.name AS athlete_name, COALESCE(c.name, '') AS coach_name, "
            "COALESCE(comp.name, '') AS competition_name, COALESCE(comp.location, '') AS competition_location, "
            "comp.competition_date AS competition_date, COALESCE(comp.competition_type, '') AS competition_type, "
            "COALESCE(comp.notes, '') AS competition_notes, COALESCE(ce.race_name, '') AS race_name, "
            "COALESCE(ce.event_name, '') AS event_name, COALESCE(ce.heat_name, '') AS heat_name, "
            "COALESCE(ce.group_name, '') AS group_name, ce.scheduled_at AS event_scheduled_at, "
            "COALESCE(ce.notes, '') AS event_notes, COALESCE(ea.bib_number, '') AS bib_number, "
            "COALESCE(ea.lane_number, '') AS lane_number, ea.result_score AS result_score, "
            "ea.result_rank AS result_rank, COALESCE(ea.notes, '') AS event_athlete_notes, "
            "oat.status AS analysis_task_status, oat.batch_id AS analysis_task_batch_id, "
            "oat.camera_id AS analysis_task_camera_id, oat.time_offset_ms AS analysis_task_time_offset_ms, "
            "s.name AS action_name, ac.name AS action_category "
            f"{from_sql} {where_sql} ORDER BY {order} {direction}, ts.id DESC LIMIT :limit OFFSET :offset"
        ),
        args,
    ).mappings())
    session_ids = [str(row["id"]) for row in rows]
    participant_map = participants_for_sessions(db, session_ids)
    video_file_map = video_files_for_sessions(db, session_ids)
    items = []
    for row in rows:
        item = history_row(dict(row))
        item["participants"] = participant_map.get(item["id"], [])
        item["videoFiles"] = video_file_map.get(item["id"], [])
        items.append(item)
    return {"items": items, "totalCount": total, "pageNumber": page_number, "pageSize": page_size}


def repetition_row(row: dict[str, Any]) -> dict[str, Any]:
    scores = row["scores"] or {}
    manual_scores = row["manual_scores"] or {}
    return {
        "id": str(row["id"]),
        "sessionId": str(row["session_id"]),
        "actionStandardId": str(row["action_standard_id"]),
        "standardVersion": row["standard_version"],
        "startedMs": row["started_ms"],
        "endedMs": row["ended_ms"],
        "valid": bool(row["valid"]),
        "score": row["score"],
        "detectionScore": scores.get("detection", 0),
        "symmetryScore": scores.get("symmetry", 0),
        "balanceScore": scores.get("balance", 0),
        "stabilityScore": scores.get("stability", 0),
        "depthScore": scores.get("depth", 0),
        "errorCodes": row["error_codes"] if isinstance(row["error_codes"], str) else "",
        "feedback": row["feedback"] or "",
        "keyFrameMs": row["key_frame_ms"],
        "videoFileId": str(row["video_file_id"] or ""),
        "videoIndex": row["video_index"],
        "videoClipStartMs": row["video_clip_start_ms"],
        "videoClipEndMs": row["video_clip_end_ms"],
        "source": row["source"],
        "reviewStatus": row["review_status"],
        "reviewerCoachId": str(row["reviewer_coach_id"] or ""),
        "reviewedAt": row["reviewed_at"].isoformat() if row["reviewed_at"] else "",
        "manualStartedMs": row["manual_started_ms"] if row["manual_started_ms"] is not None else -1,
        "manualEndedMs": row["manual_ended_ms"] if row["manual_ended_ms"] is not None else -1,
        "manualValid": int(row["manual_valid"]) if row["manual_valid"] is not None else -1,
        "manualScore": row["manual_score"] if row["manual_score"] is not None else -1,
        "manualDetectionScore": manual_scores.get("detection", -1),
        "manualSymmetryScore": manual_scores.get("symmetry", -1),
        "manualBalanceScore": manual_scores.get("balance", -1),
        "manualStabilityScore": manual_scores.get("stability", -1),
        "manualDepthScore": manual_scores.get("depth", -1),
        "manualErrorCodes": row["manual_error_codes"] if isinstance(row["manual_error_codes"], str) else "",
        "manualFeedback": row["manual_feedback"] or "",
        "coachNote": row["coach_note"] or "",
        "keyFramePoseJson": row["key_frame_pose"] if isinstance(row["key_frame_pose"], str) else "",
        "participantId": str(row.get("participant_id") or ""),
        "athleteId": str(row.get("repetition_athlete_id") or row.get("athlete_id") or ""),
        "athleteName": row.get("repetition_athlete_name") or row.get("athlete_name") or "",
        "trackId": row["track_id"] if row.get("track_id") is not None else -1,
        "cameraId": row["camera_id"] if row.get("camera_id") is not None else -1,
        "frameTimeMs": str(row["frame_time_ms"]) if row.get("frame_time_ms") is not None else "",
        "identityStatus": row.get("identity_status") or "unknown",
        "identityConfidence": row["identity_confidence"] if row.get("identity_confidence") is not None else -1,
        "identitySource": row.get("identity_source") or "",
    }


def effective_repetition_row(row: dict[str, Any]) -> dict[str, Any]:
    item = repetition_row(row)
    item.update({
        "time": row["saved_at"].strftime("%Y-%m-%d %H:%M:%S") if row.get("saved_at") else "",
        "startedAt": row["session_started_at"].isoformat() if row.get("session_started_at") else "",
        "athleteId": str(row.get("effective_athlete_id") or row["athlete_id"]),
        "athleteName": row.get("effective_athlete_name") or row.get("athlete_name") or "",
        "participantId": str(row["participant_id"] or ""),
        "coachId": str(row["coach_id"] or ""),
        "coachName": row.get("coach_name") or "",
        "competitionId": str(row["competition_id"] or ""),
        "competitionName": row.get("competition_name") or "",
        "competitionEventId": str(row["competition_event_id"] or ""),
        "raceName": row.get("race_name") or "",
        "eventName": row.get("event_name") or "",
        "heatName": row.get("heat_name") or "",
        "groupName": row.get("group_name") or "",
        "eventAthleteId": str(row["event_athlete_id"] or ""),
        "bibNumber": row.get("bib_number") or "",
        "laneNumber": row.get("lane_number") or "",
        "actionName": row.get("action_name") or "",
        "actionCategory": row.get("action_category") or "",
        "videoSource": row.get("video_source") or "",
        "videoFallbackSource": row.get("video_fallback_source") or "",
        "videoCameraName": row.get("video_camera_name") or "",
        "videoFileStatus": row.get("video_file_status") or "",
        "videoFilePath": row.get("video_file_path") or "",
        "sessionSourceType": row.get("source_type") or "training",
        "sessionSourceRef": row.get("source_ref") or "",
        "effectiveStartedMs": row["effective_started_ms"],
        "effectiveEndedMs": row["effective_ended_ms"],
        "effectiveValid": bool(row["effective_valid"]),
        "effectiveScore": row["effective_score"],
        "effectiveDetectionScore": row["effective_detection_score"],
        "effectiveSymmetryScore": row["effective_symmetry_score"],
        "effectiveBalanceScore": row["effective_balance_score"],
        "effectiveStabilityScore": row["effective_stability_score"],
        "effectiveDepthScore": row["effective_depth_score"],
        "effectiveErrorCodes": row["effective_error_codes"] if isinstance(row["effective_error_codes"], str) else "",
        "effectiveFeedback": row.get("effective_feedback") or "",
    })
    return item


def repetition_search_from_sql() -> str:
    repetition_columns = (
        "id, session_id, participant_id, athlete_id AS repetition_athlete_id, action_standard_id, standard_version, "
        "started_ms, ended_ms, valid, score, scores, error_codes, feedback, key_frame_ms, video_file_id, video_index, "
        "video_clip_start_ms, video_clip_end_ms, source, review_status, reviewer_coach_id, reviewed_at, "
        "manual_started_ms, manual_ended_ms, manual_valid, manual_score, manual_scores, manual_error_codes, "
        "manual_feedback, coach_note, key_frame_pose, track_id, camera_id, frame_time_ms, identity_status, "
        "identity_confidence, identity_source"
    )
    return (
        "FROM ("
        "SELECT rep.*, COALESCE(ra.name, '') AS repetition_athlete_name, "
        "COALESCE(rep.repetition_athlete_id, ts.athlete_id) AS effective_athlete_id, "
        "COALESCE(ra.name, a.name, '') AS effective_athlete_name, "
        "ts.athlete_id, ts.coach_id, ts.competition_id, ts.competition_event_id, ts.event_athlete_id, "
        "ts.saved_at, ts.started_at AS session_started_at, ts.video_source, ts.video_fallback_source, "
        "ts.video_camera_name, ts.source_type, ts.source_ref, "
        "COALESCE(tvf.status, '') AS video_file_status, COALESCE(tvf.file_path, '') AS video_file_path, "
        "a.name AS athlete_name, COALESCE(c.name, '') AS coach_name, COALESCE(comp.name, '') AS competition_name, "
        "COALESCE(ce.race_name, '') AS race_name, COALESCE(ce.event_name, '') AS event_name, "
        "COALESCE(ce.heat_name, '') AS heat_name, COALESCE(ce.group_name, '') AS group_name, "
        "COALESCE(ea.bib_number, '') AS bib_number, COALESCE(ea.lane_number, '') AS lane_number, "
        "s.name AS action_name, ac.name AS action_category, "
        "COALESCE(rep.manual_started_ms, rep.started_ms) AS effective_started_ms, "
        "COALESCE(rep.manual_ended_ms, rep.ended_ms) AS effective_ended_ms, "
        "COALESCE(rep.manual_valid, rep.valid) AS effective_valid, "
        "COALESCE(rep.manual_score, rep.score) AS effective_score, "
        "COALESCE((rep.manual_scores->>'detection')::int, (rep.scores->>'detection')::int, 0) AS effective_detection_score, "
        "COALESCE((rep.manual_scores->>'symmetry')::int, (rep.scores->>'symmetry')::int, 0) AS effective_symmetry_score, "
        "COALESCE((rep.manual_scores->>'balance')::int, (rep.scores->>'balance')::int, 0) AS effective_balance_score, "
        "COALESCE((rep.manual_scores->>'stability')::int, (rep.scores->>'stability')::int, 0) AS effective_stability_score, "
        "COALESCE((rep.manual_scores->>'depth')::int, (rep.scores->>'depth')::int, 0) AS effective_depth_score, "
        "COALESCE(NULLIF(rep.manual_error_codes #>> '{}', ''), rep.error_codes #>> '{}', '') AS effective_error_codes, "
        "COALESCE(NULLIF(rep.manual_feedback, ''), rep.feedback, '') AS effective_feedback "
        "FROM ("
        f"SELECT {repetition_columns} FROM participant_repetitions "
        "UNION ALL "
        f"SELECT {repetition_columns} FROM action_repetitions ar "
        "WHERE NOT EXISTS ("
        "SELECT 1 FROM participant_repetitions pr "
        "WHERE pr.action_repetition_id = ar.id OR (pr.action_repetition_id IS NULL AND pr.session_id = ar.session_id)"
        ")"
        ") rep JOIN training_sessions ts ON ts.id = rep.session_id "
        "JOIN athletes a ON a.id = ts.athlete_id LEFT JOIN athletes ra ON ra.id = rep.repetition_athlete_id "
        "LEFT JOIN coaches c ON c.id = ts.coach_id "
        "LEFT JOIN training_video_files tvf ON tvf.id = rep.video_file_id "
        "LEFT JOIN competitions comp ON comp.id = ts.competition_id "
        "LEFT JOIN competition_events ce ON ce.id = ts.competition_event_id "
        "LEFT JOIN event_athletes ea ON ea.id = ts.event_athlete_id "
        "JOIN action_standards s ON s.id = rep.action_standard_id "
        "JOIN action_categories ac ON ac.id = s.category_id"
        ") rep "
    )


@app.get("/training/repetitions")
def search_repetitions(
    sessionId: str = "",
    athleteId: str = "",
    coachId: str = "",
    actionStandardId: str = "",
    competitionId: str = "",
    competitionEventId: str = "",
    eventAthleteId: str = "",
    sourceType: str = "",
    validState: str = "all",
    reviewStatus: str = "",
    repetitionSource: str = "",
    minScore: int = -1,
    maxScore: int = -1,
    savedFrom: str = "",
    savedTo: str = "",
    clipFromMs: int = -1,
    clipToMs: int = -1,
    errorText: str = "",
    pageNumber: int = 1,
    pageSize: int = 50,
    db: Session = Depends(db_session),
    _: dict[str, Any] = Depends(require_user),
) -> dict[str, Any]:
    where: list[str] = []
    args: dict[str, Any] = {}
    for query_name, column in [
        ("sessionId", "session_id"),
        ("coachId", "coach_id"),
        ("actionStandardId", "action_standard_id"),
        ("competitionId", "competition_id"),
        ("competitionEventId", "competition_event_id"),
        ("eventAthleteId", "event_athlete_id"),
    ]:
        value = locals()[query_name]
        if value:
            where.append(f"{column} = :{query_name}")
            args[query_name] = parse_uuid(value)
    if athleteId:
        where.append("(effective_athlete_id = :athleteId OR (repetition_athlete_id IS NULL AND athlete_id = :athleteId))")
        args["athleteId"] = parse_uuid(athleteId)
    if sourceType:
        where.append("source_type = :sourceType")
        args["sourceType"] = sourceType
    if reviewStatus:
        where.append("review_status = :reviewStatus")
        args["reviewStatus"] = reviewStatus
    if repetitionSource:
        where.append("source = :repetitionSource")
        args["repetitionSource"] = repetitionSource
    if savedFrom:
        where.append("saved_at >= :savedFrom")
        args["savedFrom"] = parse_dt(savedFrom)
    if savedTo:
        where.append("saved_at <= :savedTo")
        args["savedTo"] = parse_dt(savedTo)
    if validState == "valid":
        where.append("effective_valid = true")
    elif validState == "invalid":
        where.append("effective_valid = false")
    if minScore >= 0:
        where.append("effective_score >= :minScore")
        args["minScore"] = minScore
    if maxScore >= 0:
        where.append("effective_score <= :maxScore")
        args["maxScore"] = maxScore
    if clipFromMs >= 0:
        where.append("video_clip_end_ms >= :clipFromMs")
        args["clipFromMs"] = clipFromMs
    if clipToMs >= 0:
        where.append("video_clip_start_ms <= :clipToMs")
        args["clipToMs"] = clipToMs
    if errorText.strip():
        where.append("(COALESCE(effective_error_codes, '') ILIKE :errorText OR COALESCE(effective_feedback, '') ILIKE :errorText OR COALESCE(coach_note, '') ILIKE :errorText)")
        args["errorText"] = f"%{errorText.strip()}%"
    where_sql = "WHERE " + " AND ".join(where) if where else ""
    from_sql = repetition_search_from_sql()
    total = int(db.execute(text(f"SELECT COUNT(*) {from_sql} {where_sql}"), args).scalar() or 0)
    page_size = max(1, min(pageSize, 1000))
    page_number = min(max(1, pageNumber), max(1, (total + page_size - 1) // page_size))
    args.update({"limit": page_size, "offset": (page_number - 1) * page_size})
    rows = db.execute(
        text(f"SELECT * {from_sql} {where_sql} ORDER BY saved_at DESC, effective_started_ms ASC, id DESC LIMIT :limit OFFSET :offset"),
        args,
    ).mappings()
    return {"items": [effective_repetition_row(dict(row)) for row in rows], "totalCount": total, "pageNumber": page_number, "pageSize": page_size}


@app.get("/training/sessions/{session_id}/repetitions")
def repetitions(session_id: str,
                db: Session = Depends(db_session),
                _: dict[str, Any] = Depends(require_user)) -> list[dict[str, Any]]:
    rows = db.execute(
        text(
            f"SELECT * {repetition_search_from_sql()} "
            "WHERE session_id = :session_id "
            "ORDER BY COALESCE(manual_started_ms, started_ms), started_ms"
        ),
        {"session_id": session_id},
    ).mappings()
    return [repetition_row(dict(row)) for row in rows]


def pose_frame_row(row: dict[str, Any]) -> dict[str, Any]:
    pose_summary = row.get("pose_summary") or {}
    if isinstance(pose_summary, str):
        try:
            pose_summary = json.loads(pose_summary)
        except json.JSONDecodeError:
            pose_summary = {}
    return {
        "id": str(row["id"]),
        "sessionId": str(row["session_id"]),
        "participantId": str(row.get("participant_id") or ""),
        "athleteId": str(row.get("athlete_id") or ""),
        "athleteName": row.get("athlete_name") or "",
        "videoFileId": str(row.get("video_file_id") or ""),
        "videoIndex": row["video_index"],
        "frameTimeMs": str(row["frame_time_ms"]),
        "cameraId": row["camera_id"],
        "trackId": row["track_id"] if row.get("track_id") is not None else -1,
        "identityStatus": row.get("identity_status") or "unknown",
        "identityConfidence": row["identity_confidence"] if row.get("identity_confidence") is not None else -1,
        "identitySource": row.get("identity_source") or "",
        "bboxX": row["bbox_x"] if row.get("bbox_x") is not None else 0,
        "bboxY": row["bbox_y"] if row.get("bbox_y") is not None else 0,
        "bboxWidth": row["bbox_width"] if row.get("bbox_width") is not None else 0,
        "bboxHeight": row["bbox_height"] if row.get("bbox_height") is not None else 0,
        "anchorX": row["anchor_x"] if row.get("anchor_x") is not None else 0,
        "anchorY": row["anchor_y"] if row.get("anchor_y") is not None else 0,
        "fieldX": row["field_x"] if row.get("field_x") is not None else 0,
        "fieldY": row["field_y"] if row.get("field_y") is not None else 0,
        "hasFieldPoint": bool(row.get("has_field_point", False)),
        "poseConfidence": row["pose_confidence"] if row.get("pose_confidence") is not None else -1,
        "poseSummary": pose_summary,
    }


@app.get("/training/sessions/{session_id}/pose-frames")
def participant_pose_frames(session_id: str,
                            participantId: str = "",
                            athleteId: str = "",
                            fromMs: int = -1,
                            toMs: int = -1,
                            limit: int = 5000,
                            db: Session = Depends(db_session),
                            _: dict[str, Any] = Depends(require_user)) -> list[dict[str, Any]]:
    where = ["ppf.session_id = :session_id"]
    args: dict[str, Any] = {"session_id": parse_uuid(session_id)}
    if participantId:
        where.append("ppf.participant_id = :participant_id")
        args["participant_id"] = parse_uuid(participantId)
    if athleteId:
        where.append("ppf.athlete_id = :athlete_id")
        args["athlete_id"] = parse_uuid(athleteId)
    if fromMs >= 0:
        where.append("ppf.frame_time_ms >= :from_ms")
        args["from_ms"] = fromMs
    if toMs >= 0:
        where.append("ppf.frame_time_ms <= :to_ms")
        args["to_ms"] = toMs
    args["limit"] = max(1, min(limit, 20000))
    rows = db.execute(
        text(
            "SELECT ppf.*, COALESCE(a.name, '') AS athlete_name "
            "FROM participant_pose_frames ppf "
            "LEFT JOIN athletes a ON a.id = ppf.athlete_id "
            f"WHERE {' AND '.join(where)} "
            "ORDER BY ppf.frame_time_ms ASC, ppf.camera_id ASC, COALESCE(ppf.track_id, -1) ASC LIMIT :limit"
        ),
        args,
    ).mappings()
    return [pose_frame_row(dict(row)) for row in rows]


@app.post("/training/repetitions/{repetition_id}/review")
def save_review(repetition_id: str,
                payload: dict[str, Any] = Body(...),
                db: Session = Depends(db_session),
                _: dict[str, Any] = Depends(require_user)) -> dict[str, Any]:
    import json

    row = db.execute(
        text(
            "SELECT session_id::text, id::text AS action_id FROM action_repetitions WHERE id = :id "
            "UNION ALL "
            "SELECT session_id::text, action_repetition_id::text AS action_id FROM participant_repetitions WHERE id = :id "
            "LIMIT 1"
        ),
        {"id": repetition_id},
    ).mappings().first()
    if not row:
        raise HTTPException(status_code=404, detail="Repetition not found")
    review_values = {
        "id": repetition_id,
        "review_status": payload.get("reviewStatus") or "reviewed",
        "reviewer_coach_id": parse_uuid(payload.get("reviewerCoachId")),
        "reviewed_at": parse_dt(payload.get("reviewedAt")),
        "manual_started_ms": payload.get("manualStartedMs") if int(payload.get("manualStartedMs", -1)) >= 0 else None,
        "manual_ended_ms": payload.get("manualEndedMs") if int(payload.get("manualEndedMs", -1)) >= 0 else None,
        "manual_valid": bool(payload.get("manualValid")) if int(payload.get("manualValid", -1)) >= 0 else None,
        "manual_score": payload.get("manualScore") if int(payload.get("manualScore", -1)) >= 0 else None,
        "manual_scores": json.dumps(scores_from_payload(payload, "manual"), ensure_ascii=False),
        "manual_error_codes": json.dumps(payload.get("manualErrorCodes") or "", ensure_ascii=False),
        "manual_feedback": payload.get("manualFeedback") or None,
        "coach_note": payload.get("coachNote") or None,
        "video_clip_start_ms": int(payload.get("videoClipStartMs", 0)),
        "video_clip_end_ms": int(payload.get("videoClipEndMs", 0)),
    }
    db.execute(
        text(
            "UPDATE action_repetitions SET review_status=:review_status, reviewer_coach_id=:reviewer_coach_id, "
            "reviewed_at=:reviewed_at, manual_started_ms=:manual_started_ms, manual_ended_ms=:manual_ended_ms, "
            "manual_valid=:manual_valid, manual_score=:manual_score, manual_scores=CAST(:manual_scores AS jsonb), "
            "manual_error_codes=CAST(:manual_error_codes AS jsonb), manual_feedback=:manual_feedback, coach_note=:coach_note, "
            "video_clip_start_ms=:video_clip_start_ms, video_clip_end_ms=:video_clip_end_ms "
            "WHERE id=:id OR id = :action_id"
        ),
        {**review_values, "action_id": parse_uuid(row["action_id"])},
    )
    db.execute(
        text(
            "UPDATE participant_repetitions SET review_status=:review_status, reviewer_coach_id=:reviewer_coach_id, "
            "reviewed_at=:reviewed_at, manual_started_ms=:manual_started_ms, manual_ended_ms=:manual_ended_ms, "
            "manual_valid=:manual_valid, manual_score=:manual_score, manual_scores=CAST(:manual_scores AS jsonb), "
            "manual_error_codes=CAST(:manual_error_codes AS jsonb), manual_feedback=:manual_feedback, coach_note=:coach_note, "
            "video_clip_start_ms=:video_clip_start_ms, video_clip_end_ms=:video_clip_end_ms "
            "WHERE id=:id OR action_repetition_id = :action_id"
        ),
        {**review_values, "action_id": parse_uuid(row["action_id"])},
    )
    recalculate_session_summary(db, row["session_id"])
    return {"ok": True}


def recalculate_session_summary(db: Session, session_id: str) -> None:
    reps = db.execute(text("SELECT * FROM participant_repetitions WHERE session_id = :session_id"), {"session_id": session_id}).mappings().all()
    if not reps:
        reps = db.execute(text("SELECT * FROM action_repetitions WHERE session_id = :session_id"), {"session_id": session_id}).mappings().all()
    total = len(reps)
    valid = 0
    best = 0
    score_sum = 0
    metric_sums = {"detection": 0, "symmetry": 0, "balance": 0, "stability": 0, "depth": 0}
    for row in reps:
        manual_scores = row["manual_scores"] or {}
        use_manual_scores = any(int(value or -1) >= 0 for value in manual_scores.values())
        scores = manual_scores if use_manual_scores else row["scores"] or {}
        rep_valid = row["manual_valid"] if row["manual_valid"] is not None else row["valid"]
        rep_score = row["manual_score"] if row["manual_score"] is not None else row["score"]
        if rep_valid:
            valid += 1
        score_sum += int(rep_score or 0)
        best = max(best, int(rep_score or 0))
        for key in metric_sums:
            metric_sums[key] += int(scores.get(key, 0))
    divisor = max(1, total)
    average = round(score_sum / divisor)
    metric_avg = {key: round(value / divisor) for key, value in metric_sums.items()}
    row = db.execute(
        text("SELECT athlete_id::text, action_standard_id::text FROM training_sessions WHERE id = :id"),
        {"id": session_id},
    ).first()
    db.execute(
        text(
            "UPDATE training_sessions SET total_reps=:total, valid_reps=:valid, average_score=:average, "
            "best_score=:best, scores=CAST(:scores AS jsonb) WHERE id=:id"
        ),
        {"id": session_id, "total": total, "valid": valid, "average": average, "best": best, "scores": __import__("json").dumps(metric_avg)},
    )
    if row:
        refresh_baseline(db, row[0], row[1])


@app.post("/training/sessions/{session_id}/manual-repetitions")
def manual_repetition(session_id: str,
                      payload: dict[str, Any] = Body(...),
                      db: Session = Depends(db_session),
                      _: dict[str, Any] = Depends(require_user)) -> dict[str, Any]:
    session = db.execute(
        text("SELECT athlete_id::text, action_standard_id::text, standard_version FROM training_sessions WHERE id = :id"),
        {"id": session_id},
    ).mappings().first()
    if not session:
        raise HTTPException(status_code=404, detail="Session not found")
    payload["source"] = "coach"
    payload["reviewStatus"] = payload.get("reviewStatus") or "reviewed"
    payload["sessionId"] = session_id
    payload["actionStandardId"] = payload.get("actionStandardId") or session["action_standard_id"]
    payload["standardVersion"] = payload.get("standardVersion") or session["standard_version"]
    video_rows = db.execute(
        text("SELECT video_index, id::text FROM training_video_files WHERE session_id = :session_id"),
        {"session_id": session_id},
    ).mappings()
    video_file_by_index = {int(row["video_index"]): row["id"] for row in video_rows}
    db.execute(
        text(
            "INSERT INTO training_session_participants (id, session_id, athlete_id, slot_index, role, active, updated_at) "
            "VALUES (gen_random_uuid(), :session_id, :athlete_id, 1, 'primary', true, now()) "
            "ON CONFLICT (session_id, athlete_id) DO NOTHING"
        ),
        {"session_id": session_id, "athlete_id": parse_uuid(session["athlete_id"])},
    )
    participants = participants_for_sessions(db, [session_id]).get(session_id, [])
    participant_by_athlete = {item["athleteId"]: item["id"] for item in participants}
    rep_id = insert_repetition(db, session_id, session["action_standard_id"], payload, video_file_by_index)
    insert_participant_repetition(db, session_id, session["action_standard_id"], payload, participant_by_athlete, video_file_by_index, rep_id)
    recalculate_session_summary(db, session_id)
    return {"id": rep_id}


@app.post("/training/sessions/{session_id}/coach-comment")
def save_coach_comment(session_id: str,
                       payload: dict[str, Any] = Body(...),
                       db: Session = Depends(db_session),
                       _: dict[str, Any] = Depends(require_user)) -> dict[str, Any]:
    db.execute(text("UPDATE training_sessions SET coach_comment = :comment WHERE id = :id"), {"id": session_id, "comment": payload.get("comment") or ""})
    return {"ok": True}


@app.get("/training/trends")
def trends(days: int = 7,
           db: Session = Depends(db_session),
           _: dict[str, Any] = Depends(require_user)) -> dict[str, Any]:
    since = now() - timedelta(days=max(1, days))
    session = db.execute(
        text(
            "SELECT COUNT(*) AS session_count, COALESCE(ROUND(AVG(average_score)), 0) AS average_score, "
            "COALESCE(MAX(best_score), 0) AS best_score, COALESCE(SUM(total_reps), 0) AS completed_reps "
            "FROM training_sessions WHERE saved_at >= :since"
        ),
        {"since": since},
    ).mappings().one()
    metrics = db.execute(
        text(
            "SELECT COALESCE(ROUND(AVG((scores->>'detection')::numeric)), 0) AS detection, "
            "COALESCE(ROUND(AVG((scores->>'symmetry')::numeric)), 0) AS symmetry, "
            "COALESCE(ROUND(AVG((scores->>'balance')::numeric)), 0) AS balance, "
            "COALESCE(ROUND(AVG((scores->>'stability')::numeric)), 0) AS stability, "
            "COALESCE(ROUND(AVG((scores->>'depth')::numeric)), 0) AS depth "
            "FROM ("
            "SELECT scores, session_id FROM participant_repetitions "
            "UNION ALL "
            "SELECT scores, session_id FROM action_repetitions ar "
            "WHERE NOT EXISTS (SELECT 1 FROM participant_repetitions pr WHERE pr.session_id = ar.session_id)"
            ") reps WHERE session_id IN (SELECT id FROM training_sessions WHERE saved_at >= :since)"
        ),
        {"since": since},
    ).mappings().one()
    metric_names = {"detection": "关键点", "symmetry": "对称", "balance": "重心", "stability": "稳定", "depth": "3D"}
    weakest = min(metric_names, key=lambda key: int(metrics[key] or 0))
    return {
        "days": days,
        "sessionCount": int(session["session_count"] or 0),
        "averageScore": int(session["average_score"] or 0),
        "bestScore": int(session["best_score"] or 0),
        "completedReps": int(session["completed_reps"] or 0),
        "detectionScore": int(metrics["detection"] or 0),
        "symmetryScore": int(metrics["symmetry"] or 0),
        "balanceScore": int(metrics["balance"] or 0),
        "stabilityScore": int(metrics["stability"] or 0),
        "depthScore": int(metrics["depth"] or 0),
        "weakestMetricName": metric_names[weakest],
        "weakestMetricScore": int(metrics[weakest] or 0),
    }


@app.get("/training/baselines")
def baseline(athleteId: str,
             actionStandardId: str,
             db: Session = Depends(db_session),
             _: dict[str, Any] = Depends(require_user)) -> dict[str, Any]:
    row = db.execute(
        text(
            "SELECT session_count, average_score, average_valid_reps FROM athlete_action_baselines "
            "WHERE athlete_id = :athlete_id AND action_standard_id = :action_standard_id"
        ),
        {"athlete_id": parse_uuid(athleteId), "action_standard_id": parse_uuid(actionStandardId)},
    ).mappings().first()
    if not row:
        return {"sessionCount": 0, "averageScore": 0, "averageValidReps": 0}
    return {"sessionCount": row["session_count"], "averageScore": row["average_score"], "averageValidReps": row["average_valid_reps"]}

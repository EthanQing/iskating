from __future__ import annotations

from sqlalchemy import text
from sqlalchemy.engine import Engine


BUSINESS_TABLES = [
    "athlete_action_baselines",
    "action_repetitions",
    "training_video_files",
    "training_session_participants",
    "training_sessions",
    "offline_analysis_tasks",
    "training_tasks",
    "training_plans",
    "event_athletes",
    "competition_events",
    "competitions",
    "action_standards",
    "action_categories",
    "coach_athletes",
    "coaches",
    "athletes",
    "users",
]


SCHEMA_SQL = """
CREATE EXTENSION IF NOT EXISTS "pgcrypto";

CREATE TABLE users (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    username text NOT NULL UNIQUE,
    password_hash text NOT NULL,
    role text NOT NULL DEFAULT 'coach',
    active boolean NOT NULL DEFAULT true,
    created_at timestamptz NOT NULL DEFAULT now(),
    updated_at timestamptz NOT NULL DEFAULT now()
);

CREATE TABLE athletes (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    name text NOT NULL,
    code text,
    age_group text,
    height_cm double precision NOT NULL DEFAULT 0,
    weight_kg double precision NOT NULL DEFAULT 0,
    discipline text,
    level text,
    preferred_rotation text,
    preferred_takeoff_foot text,
    injury_notes text,
    goals text,
    active boolean NOT NULL DEFAULT true,
    created_at timestamptz NOT NULL DEFAULT now(),
    updated_at timestamptz NOT NULL DEFAULT now()
);

CREATE TABLE coaches (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    name text NOT NULL,
    code text,
    specialty text,
    phone text,
    notes text,
    active boolean NOT NULL DEFAULT true,
    created_at timestamptz NOT NULL DEFAULT now(),
    updated_at timestamptz NOT NULL DEFAULT now()
);

CREATE TABLE coach_athletes (
    coach_id uuid NOT NULL REFERENCES coaches(id),
    athlete_id uuid NOT NULL REFERENCES athletes(id),
    created_at timestamptz NOT NULL DEFAULT now(),
    PRIMARY KEY (coach_id, athlete_id)
);

CREATE TABLE competitions (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    name text NOT NULL,
    location text,
    competition_date date,
    competition_type text,
    notes text,
    active boolean NOT NULL DEFAULT true,
    created_at timestamptz NOT NULL DEFAULT now(),
    updated_at timestamptz NOT NULL DEFAULT now()
);

CREATE TABLE competition_events (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    competition_id uuid NOT NULL REFERENCES competitions(id),
    race_name text,
    event_name text,
    heat_name text,
    group_name text,
    scheduled_at timestamptz,
    notes text,
    active boolean NOT NULL DEFAULT true,
    created_at timestamptz NOT NULL DEFAULT now(),
    updated_at timestamptz NOT NULL DEFAULT now()
);

CREATE TABLE event_athletes (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    event_id uuid NOT NULL REFERENCES competition_events(id),
    athlete_id uuid NOT NULL REFERENCES athletes(id),
    bib_number text,
    lane_number text,
    sort_order integer NOT NULL DEFAULT 0,
    result_score integer,
    result_rank integer,
    notes text,
    active boolean NOT NULL DEFAULT true,
    created_at timestamptz NOT NULL DEFAULT now(),
    updated_at timestamptz NOT NULL DEFAULT now(),
    CONSTRAINT uq_event_athletes_event_athlete UNIQUE (event_id, athlete_id)
);

CREATE TABLE action_categories (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    code text NOT NULL UNIQUE,
    name text NOT NULL,
    sort_order integer NOT NULL DEFAULT 0
);

CREATE TABLE action_standards (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    code text NOT NULL UNIQUE,
    name text NOT NULL,
    category_id uuid NOT NULL REFERENCES action_categories(id),
    level text,
    purpose text,
    version integer NOT NULL DEFAULT 1,
    target_reps integer NOT NULL DEFAULT 10,
    target_score integer NOT NULL DEFAULT 80,
    set_count integer NOT NULL DEFAULT 1,
    rest_seconds integer NOT NULL DEFAULT 60,
    arm_threshold double precision NOT NULL DEFAULT 0.28,
    release_threshold double precision NOT NULL DEFAULT 0.18,
    debounce_ms integer NOT NULL DEFAULT 900,
    weights jsonb NOT NULL DEFAULT '{}'::jsonb,
    minimums jsonb NOT NULL DEFAULT '{}'::jsonb,
    phases jsonb,
    key_points jsonb,
    issue jsonb NOT NULL DEFAULT '{}'::jsonb,
    reference_video_source text,
    reference_repetition_id uuid,
    reference_notes text,
    active boolean NOT NULL DEFAULT true,
    created_at timestamptz NOT NULL DEFAULT now(),
    updated_at timestamptz NOT NULL DEFAULT now()
);

CREATE TABLE training_plans (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    athlete_id uuid NOT NULL REFERENCES athletes(id),
    coach_id uuid REFERENCES coaches(id),
    name text NOT NULL,
    training_date date NOT NULL,
    site text,
    training_phase text,
    goal text,
    status text NOT NULL DEFAULT 'active',
    created_at timestamptz NOT NULL DEFAULT now(),
    updated_at timestamptz NOT NULL DEFAULT now()
);

CREATE TABLE training_tasks (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    plan_id uuid NOT NULL REFERENCES training_plans(id),
    action_standard_id uuid NOT NULL REFERENCES action_standards(id),
    standard_version integer NOT NULL DEFAULT 1,
    target_reps integer NOT NULL DEFAULT 10,
    target_score integer NOT NULL DEFAULT 80,
    set_count integer NOT NULL DEFAULT 1,
    rest_seconds integer NOT NULL DEFAULT 60,
    status text NOT NULL DEFAULT 'active',
    sort_order integer NOT NULL DEFAULT 0,
    created_at timestamptz NOT NULL DEFAULT now(),
    updated_at timestamptz NOT NULL DEFAULT now()
);

CREATE TABLE offline_analysis_tasks (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    batch_id uuid,
    camera_id integer NOT NULL DEFAULT 0,
    time_offset_ms integer NOT NULL DEFAULT 0,
    video_path text NOT NULL,
    file_name text,
    file_size_bytes bigint,
    file_modified_at timestamptz,
    duration_ms integer NOT NULL DEFAULT 0,
    status text NOT NULL DEFAULT 'imported',
    probe_metadata jsonb NOT NULL DEFAULT '{}'::jsonb,
    summary_metadata jsonb NOT NULL DEFAULT '{}'::jsonb,
    created_at timestamptz NOT NULL DEFAULT now(),
    updated_at timestamptz NOT NULL DEFAULT now(),
    CONSTRAINT ck_offline_analysis_tasks_status CHECK (status IN ('imported', 'analyzing', 'completed', 'failed', 'archived')),
    CONSTRAINT ck_offline_analysis_tasks_camera CHECK (camera_id >= 0)
);

CREATE TABLE training_sessions (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    athlete_id uuid NOT NULL REFERENCES athletes(id),
    coach_id uuid REFERENCES coaches(id),
    competition_id uuid REFERENCES competitions(id),
    competition_event_id uuid REFERENCES competition_events(id),
    event_athlete_id uuid REFERENCES event_athletes(id),
    plan_id uuid REFERENCES training_plans(id),
    task_id uuid REFERENCES training_tasks(id),
    action_standard_id uuid NOT NULL REFERENCES action_standards(id),
    standard_version integer NOT NULL DEFAULT 1,
    legacy_qsettings_id bigint NOT NULL DEFAULT 0,
    started_at timestamptz NOT NULL,
    saved_at timestamptz NOT NULL,
    duration_sec integer NOT NULL DEFAULT 0,
    total_reps integer NOT NULL DEFAULT 0,
    valid_reps integer NOT NULL DEFAULT 0,
    average_score integer NOT NULL DEFAULT 0,
    best_score integer NOT NULL DEFAULT 0,
    camera integer NOT NULL DEFAULT 1,
    model_precision text,
    fps integer NOT NULL DEFAULT 30,
    scores jsonb NOT NULL DEFAULT '{}'::jsonb,
    site text,
    training_phase text,
    goal text,
    target_reps integer NOT NULL DEFAULT 0,
    target_score integer NOT NULL DEFAULT 0,
    set_count integer NOT NULL DEFAULT 1,
    rest_seconds integer NOT NULL DEFAULT 60,
    video_source text,
    video_fallback_source text,
    video_camera_name text,
    analysis_task_id uuid REFERENCES offline_analysis_tasks(id) ON DELETE SET NULL,
    source_type text NOT NULL DEFAULT 'training',
    source_ref text,
    feedback text,
    notes text,
    coach_comment text
);

CREATE TABLE training_session_participants (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    session_id uuid NOT NULL REFERENCES training_sessions(id) ON DELETE CASCADE,
    athlete_id uuid NOT NULL REFERENCES athletes(id),
    slot_index integer NOT NULL,
    role text NOT NULL DEFAULT 'participant',
    track_label text,
    notes text,
    active boolean NOT NULL DEFAULT true,
    created_at timestamptz NOT NULL DEFAULT now(),
    updated_at timestamptz NOT NULL DEFAULT now(),
    CONSTRAINT ck_training_session_participants_slot CHECK (slot_index >= 1 AND slot_index <= 4),
    CONSTRAINT ck_training_session_participants_role CHECK (role IN ('primary', 'participant')),
    CONSTRAINT uq_training_session_participants_athlete UNIQUE (session_id, athlete_id),
    CONSTRAINT uq_training_session_participants_slot UNIQUE (session_id, slot_index)
);

CREATE TABLE training_video_files (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    session_id uuid NOT NULL REFERENCES training_sessions(id) ON DELETE CASCADE,
    video_index integer NOT NULL DEFAULT 1,
    camera integer NOT NULL DEFAULT 0,
    camera_name text,
    source_url text,
    fallback_url text,
    storage_root text,
    relative_dir text,
    file_name text,
    file_path text,
    metadata_path text,
    status text NOT NULL DEFAULT 'planned',
    session_start_ms integer NOT NULL DEFAULT 0,
    session_end_ms integer NOT NULL DEFAULT 0,
    duration_ms integer NOT NULL DEFAULT 0,
    file_size_bytes bigint,
    file_modified_at timestamptz,
    checksum_algorithm text,
    checksum_value text,
    metadata jsonb NOT NULL DEFAULT '{}'::jsonb,
    created_at timestamptz NOT NULL DEFAULT now(),
    updated_at timestamptz NOT NULL DEFAULT now(),
    CONSTRAINT ck_training_video_files_status CHECK (status IN ('planned', 'external', 'recorded')),
    CONSTRAINT uq_training_video_files_session_index UNIQUE (session_id, video_index)
);

CREATE TABLE action_repetitions (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    session_id uuid NOT NULL REFERENCES training_sessions(id) ON DELETE CASCADE,
    participant_id uuid REFERENCES training_session_participants(id),
    athlete_id uuid REFERENCES athletes(id),
    action_standard_id uuid NOT NULL REFERENCES action_standards(id),
    standard_version integer NOT NULL DEFAULT 1,
    started_ms integer NOT NULL DEFAULT 0,
    ended_ms integer NOT NULL DEFAULT 0,
    valid boolean NOT NULL DEFAULT false,
    score integer NOT NULL DEFAULT 0,
    scores jsonb NOT NULL DEFAULT '{}'::jsonb,
    error_codes jsonb,
    feedback text,
    key_frame_ms integer NOT NULL DEFAULT 0,
    video_file_id uuid REFERENCES training_video_files(id) ON DELETE SET NULL,
    video_index integer NOT NULL DEFAULT 1,
    video_clip_start_ms integer NOT NULL DEFAULT 0,
    video_clip_end_ms integer NOT NULL DEFAULT 0,
    source text NOT NULL DEFAULT 'ai',
    review_status text NOT NULL DEFAULT 'unreviewed',
    reviewer_coach_id uuid REFERENCES coaches(id),
    reviewed_at timestamptz,
    manual_started_ms integer,
    manual_ended_ms integer,
    manual_valid boolean,
    manual_score integer,
    manual_scores jsonb,
    manual_error_codes jsonb,
    manual_feedback text,
    coach_note text,
    key_frame_pose jsonb,
    track_id integer,
    camera_id integer,
    frame_time_ms bigint,
    identity_status text NOT NULL DEFAULT 'unknown',
    identity_confidence double precision,
    identity_source text
);

CREATE TABLE athlete_action_baselines (
    athlete_id uuid NOT NULL REFERENCES athletes(id),
    action_standard_id uuid NOT NULL REFERENCES action_standards(id),
    session_count integer NOT NULL DEFAULT 0,
    average_score integer NOT NULL DEFAULT 0,
    average_valid_reps integer NOT NULL DEFAULT 0,
    updated_at timestamptz NOT NULL DEFAULT now(),
    PRIMARY KEY (athlete_id, action_standard_id)
);

CREATE INDEX ix_competition_events_competition ON competition_events(competition_id);
CREATE INDEX ix_event_athletes_event ON event_athletes(event_id);
CREATE INDEX ix_event_athletes_athlete ON event_athletes(athlete_id);
CREATE INDEX ix_training_sessions_saved_at ON training_sessions(saved_at);
CREATE INDEX ix_training_sessions_athlete ON training_sessions(athlete_id);
CREATE INDEX ix_training_sessions_coach ON training_sessions(coach_id);
CREATE INDEX ix_training_sessions_action ON training_sessions(action_standard_id);
CREATE INDEX ix_training_sessions_competition ON training_sessions(competition_id);
CREATE INDEX ix_training_sessions_competition_event ON training_sessions(competition_event_id);
CREATE INDEX ix_training_sessions_event_athlete ON training_sessions(event_athlete_id);
CREATE INDEX ix_training_sessions_source_type ON training_sessions(source_type);
CREATE INDEX ix_training_sessions_source_ref ON training_sessions(source_ref);
CREATE INDEX ix_training_sessions_analysis_task ON training_sessions(analysis_task_id);
CREATE INDEX ix_offline_analysis_tasks_batch ON offline_analysis_tasks(batch_id);
CREATE INDEX ix_offline_analysis_tasks_video_path ON offline_analysis_tasks(video_path);
CREATE INDEX ix_training_tasks_plan ON training_tasks(plan_id);
CREATE INDEX ix_training_plans_date ON training_plans(training_date);
CREATE INDEX ix_training_session_participants_session ON training_session_participants(session_id);
CREATE INDEX ix_training_session_participants_athlete ON training_session_participants(athlete_id);
CREATE INDEX ix_training_video_files_session ON training_video_files(session_id);
CREATE INDEX ix_action_repetitions_session ON action_repetitions(session_id);
CREATE INDEX ix_action_repetitions_video_file ON action_repetitions(video_file_id);
CREATE INDEX ix_action_repetitions_review ON action_repetitions(review_status);
CREATE INDEX ix_action_repetitions_participant ON action_repetitions(participant_id);
CREATE INDEX ix_action_repetitions_athlete ON action_repetitions(athlete_id);
CREATE INDEX ix_action_repetitions_track ON action_repetitions(track_id);
CREATE INDEX ix_action_repetitions_camera ON action_repetitions(camera_id);
CREATE INDEX ix_action_repetitions_frame_time ON action_repetitions(frame_time_ms);
"""


def create_schema(engine: Engine) -> None:
    with engine.begin() as connection:
        connection.execute(text(SCHEMA_SQL))


def drop_schema(engine: Engine) -> None:
    tables = ", ".join(BUSINESS_TABLES)
    with engine.begin() as connection:
        connection.execute(text(f"DROP TABLE IF EXISTS {tables} CASCADE"))

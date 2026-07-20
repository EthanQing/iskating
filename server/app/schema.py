from __future__ import annotations

from sqlalchemy import text
from sqlalchemy.engine import Engine


BUSINESS_TABLES = [
    "athlete_identity_embeddings",
    "athlete_identity_samples",
    "athlete_action_baselines",
    "joint_metrics",
    "speed_metrics",
    "track_points",
    "participant_pose_frames",
    "participant_repetitions",
    "action_repetitions",
    "training_video_files",
    "training_session_participants",
    "training_sessions",
    "offline_analysis_result_chunks",
    "offline_analysis_run_sources",
    "offline_analysis_runs",
    "offline_analysis_tasks",
    "offline_analysis_batches",
    "analysis_tasks",
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

CREATE TABLE athlete_identity_samples (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    athlete_id uuid NOT NULL REFERENCES athletes(id) ON DELETE CASCADE,
    file_path text NOT NULL,
    file_name text NOT NULL,
    model_version text NOT NULL,
    preprocessing_version text NOT NULL,
    created_at timestamptz NOT NULL DEFAULT now()
);

CREATE TABLE athlete_identity_embeddings (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    sample_id uuid NOT NULL UNIQUE REFERENCES athlete_identity_samples(id) ON DELETE CASCADE,
    athlete_id uuid NOT NULL REFERENCES athletes(id) ON DELETE CASCADE,
    embedding double precision[] NOT NULL,
    embedding_dimension integer NOT NULL,
    model_version text NOT NULL,
    preprocessing_version text NOT NULL,
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

CREATE TABLE analysis_tasks (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    type text NOT NULL CHECK (type IN ('offline_import', 'full_rate_batch')),
    status text NOT NULL DEFAULT 'queued' CHECK (status IN ('queued', 'running', 'paused', 'completed', 'failed', 'cancelled')),
    progress double precision NOT NULL DEFAULT 0 CHECK (progress >= 0 AND progress <= 100),
    input jsonb NOT NULL DEFAULT '{}'::jsonb,
    output_session_id uuid,
    error text,
    created_at timestamptz NOT NULL DEFAULT now(),
    updated_at timestamptz NOT NULL DEFAULT now()
);

CREATE TABLE offline_analysis_batches (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    status text NOT NULL DEFAULT 'imported',
    source_started_at timestamptz,
    active_run_id uuid,
    analysis_task_id uuid REFERENCES analysis_tasks(id) ON DELETE SET NULL,
    metadata jsonb NOT NULL DEFAULT '{}'::jsonb,
    created_at timestamptz NOT NULL DEFAULT now(),
    updated_at timestamptz NOT NULL DEFAULT now(),
    CONSTRAINT ck_offline_analysis_batches_status
        CHECK (status IN ('imported', 'queued', 'running', 'partial', 'completed', 'failed', 'cancelled', 'archived'))
);

CREATE TABLE offline_analysis_tasks (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    analysis_task_id uuid REFERENCES analysis_tasks(id) ON DELETE SET NULL,
    batch_id uuid REFERENCES offline_analysis_batches(id) ON DELETE CASCADE,
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

CREATE TABLE offline_analysis_runs (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    batch_id uuid NOT NULL REFERENCES offline_analysis_batches(id) ON DELETE CASCADE,
    status text NOT NULL DEFAULT 'queued',
    model_version text NOT NULL,
    preprocessing_version text NOT NULL,
    gallery_snapshot_hash text,
    configuration jsonb NOT NULL DEFAULT '{}'::jsonb,
    total_frames bigint NOT NULL DEFAULT 0,
    processed_frames bigint NOT NULL DEFAULT 0,
    error_message text,
    worker_id text,
    lease_expires_at timestamptz,
    cancel_requested boolean NOT NULL DEFAULT false,
    artifact_root_uri text NOT NULL,
    started_at timestamptz,
    completed_at timestamptz,
    created_at timestamptz NOT NULL DEFAULT now(),
    updated_at timestamptz NOT NULL DEFAULT now(),
    CONSTRAINT ck_offline_analysis_runs_status
        CHECK (status IN ('queued', 'running', 'partial', 'completed', 'failed', 'cancelled', 'archived')),
    CONSTRAINT ck_offline_analysis_runs_progress
        CHECK (total_frames >= 0 AND processed_frames >= 0 AND processed_frames <= total_frames OR total_frames = 0)
);

CREATE TABLE offline_analysis_run_sources (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    run_id uuid NOT NULL REFERENCES offline_analysis_runs(id) ON DELETE CASCADE,
    task_id uuid NOT NULL REFERENCES offline_analysis_tasks(id) ON DELETE CASCADE,
    camera_id integer NOT NULL,
    source_uri text NOT NULL,
    source_started_at timestamptz,
    manual_correction_ms integer NOT NULL DEFAULT 0,
    status text NOT NULL DEFAULT 'queued',
    total_frames bigint NOT NULL DEFAULT 0,
    processed_frames bigint NOT NULL DEFAULT 0,
    last_frame_index bigint NOT NULL DEFAULT -1,
    last_pts_ms bigint,
    completed_through_ms bigint,
    error_message text,
    retry_count integer NOT NULL DEFAULT 0,
    created_at timestamptz NOT NULL DEFAULT now(),
    updated_at timestamptz NOT NULL DEFAULT now(),
    CONSTRAINT uq_offline_analysis_run_sources_task UNIQUE (run_id, task_id),
    CONSTRAINT uq_offline_analysis_run_sources_camera UNIQUE (run_id, camera_id),
    CONSTRAINT ck_offline_analysis_run_sources_camera CHECK (camera_id >= 0),
    CONSTRAINT ck_offline_analysis_run_sources_status
        CHECK (status IN ('queued', 'running', 'partial', 'completed', 'failed', 'cancelled')),
    CONSTRAINT ck_offline_analysis_run_sources_progress
        CHECK (total_frames >= 0 AND processed_frames >= 0 AND processed_frames <= total_frames OR total_frames = 0)
);

CREATE TABLE offline_analysis_result_chunks (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    run_source_id uuid NOT NULL REFERENCES offline_analysis_run_sources(id) ON DELETE CASCADE,
    start_frame_index bigint NOT NULL,
    end_frame_index bigint NOT NULL,
    start_pts_ms bigint NOT NULL,
    end_pts_ms bigint NOT NULL,
    frame_count integer NOT NULL,
    object_count integer NOT NULL DEFAULT 0,
    artifact_uri text NOT NULL,
    checksum_sha256 text NOT NULL,
    schema_version integer NOT NULL DEFAULT 1,
    created_at timestamptz NOT NULL DEFAULT now(),
    CONSTRAINT uq_offline_analysis_result_chunks_range UNIQUE (run_source_id, start_frame_index),
    CONSTRAINT ck_offline_analysis_result_chunks_frames
        CHECK (start_frame_index >= 0 AND end_frame_index >= start_frame_index AND frame_count > 0),
    CONSTRAINT ck_offline_analysis_result_chunks_pts CHECK (end_pts_ms >= start_pts_ms)
);

ALTER TABLE offline_analysis_batches
    ADD CONSTRAINT fk_offline_analysis_batches_active_run
    FOREIGN KEY (active_run_id) REFERENCES offline_analysis_runs(id) ON DELETE SET NULL;

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
    analysis_batch_id uuid REFERENCES offline_analysis_batches(id) ON DELETE SET NULL,
    analysis_run_id uuid REFERENCES offline_analysis_runs(id) ON DELETE SET NULL,
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

CREATE TABLE participant_repetitions (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    session_id uuid NOT NULL REFERENCES training_sessions(id) ON DELETE CASCADE,
    participant_id uuid REFERENCES training_session_participants(id) ON DELETE SET NULL,
    athlete_id uuid REFERENCES athletes(id),
    action_repetition_id uuid REFERENCES action_repetitions(id) ON DELETE SET NULL,
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

CREATE TABLE participant_pose_frames (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    session_id uuid NOT NULL REFERENCES training_sessions(id) ON DELETE CASCADE,
    participant_id uuid REFERENCES training_session_participants(id) ON DELETE SET NULL,
    athlete_id uuid REFERENCES athletes(id),
    video_file_id uuid REFERENCES training_video_files(id) ON DELETE SET NULL,
    video_index integer NOT NULL DEFAULT 1,
    frame_time_ms bigint NOT NULL,
    camera_id integer NOT NULL DEFAULT 0,
    track_id integer,
    identity_status text NOT NULL DEFAULT 'unknown',
    identity_confidence double precision,
    identity_source text,
    bbox_x double precision,
    bbox_y double precision,
    bbox_width double precision,
    bbox_height double precision,
    anchor_x double precision,
    anchor_y double precision,
    field_x double precision,
    field_y double precision,
    has_field_point boolean NOT NULL DEFAULT false,
    pose_confidence double precision,
    pose_summary jsonb NOT NULL DEFAULT '{}'::jsonb,
    created_at timestamptz NOT NULL DEFAULT now()
);

CREATE TABLE track_points (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    participant_id uuid NOT NULL REFERENCES training_session_participants(id) ON DELETE CASCADE,
    t_ms bigint NOT NULL,
    x double precision NOT NULL,
    y double precision NOT NULL,
    z double precision NOT NULL DEFAULT 0,
    speed_source text NOT NULL DEFAULT 'position_delta',
    camera_id integer NOT NULL DEFAULT 0,
    confidence double precision,
    created_at timestamptz NOT NULL DEFAULT now(),
    UNIQUE (participant_id, t_ms, camera_id)
);

CREATE TABLE speed_metrics (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    participant_id uuid NOT NULL REFERENCES training_session_participants(id) ON DELETE CASCADE,
    track_point_id uuid NOT NULL REFERENCES track_points(id) ON DELETE CASCADE,
    t_ms bigint NOT NULL,
    camera_id integer NOT NULL DEFAULT 0,
    instantaneous_speed_mps double precision NOT NULL DEFAULT 0,
    smoothed_speed_mps double precision NOT NULL DEFAULT 0,
    smoothing_window_ms integer NOT NULL DEFAULT 1000,
    unit text NOT NULL DEFAULT 'm/s',
    algorithm_version text NOT NULL,
    valid boolean NOT NULL DEFAULT false,
    created_at timestamptz NOT NULL DEFAULT now(),
    UNIQUE (track_point_id)
);

CREATE TABLE joint_metrics (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    participant_id uuid NOT NULL REFERENCES training_session_participants(id) ON DELETE CASCADE,
    t_ms bigint NOT NULL,
    camera_id integer NOT NULL DEFAULT 0,
    joint text NOT NULL,
    side text NOT NULL CHECK (side IN ('left', 'right')),
    angle_deg double precision NOT NULL DEFAULT 0,
    angular_velocity_deg_per_sec double precision NOT NULL DEFAULT 0,
    valid boolean NOT NULL DEFAULT false,
    confidence double precision,
    algorithm_version text NOT NULL,
    created_at timestamptz NOT NULL DEFAULT now(),
    UNIQUE (participant_id, t_ms, camera_id, joint, side)
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
CREATE INDEX ix_athlete_identity_samples_athlete ON athlete_identity_samples(athlete_id);
CREATE INDEX ix_athlete_identity_embeddings_athlete ON athlete_identity_embeddings(athlete_id);
CREATE INDEX ix_training_sessions_athlete ON training_sessions(athlete_id);
CREATE INDEX ix_training_sessions_coach ON training_sessions(coach_id);
CREATE INDEX ix_training_sessions_action ON training_sessions(action_standard_id);
CREATE INDEX ix_training_sessions_competition ON training_sessions(competition_id);
CREATE INDEX ix_training_sessions_competition_event ON training_sessions(competition_event_id);
CREATE INDEX ix_training_sessions_event_athlete ON training_sessions(event_athlete_id);
CREATE INDEX ix_training_sessions_source_type ON training_sessions(source_type);
CREATE INDEX ix_training_sessions_source_ref ON training_sessions(source_ref);
CREATE INDEX ix_training_sessions_analysis_task ON training_sessions(analysis_task_id);
CREATE INDEX ix_training_sessions_analysis_batch ON training_sessions(analysis_batch_id);
CREATE INDEX ix_training_sessions_analysis_run ON training_sessions(analysis_run_id);
CREATE INDEX ix_offline_analysis_batches_active_run ON offline_analysis_batches(active_run_id);
CREATE INDEX ix_offline_analysis_tasks_batch ON offline_analysis_tasks(batch_id);
CREATE INDEX ix_offline_analysis_tasks_analysis_task ON offline_analysis_tasks(analysis_task_id);
CREATE INDEX ix_offline_analysis_batches_analysis_task ON offline_analysis_batches(analysis_task_id);
CREATE INDEX ix_analysis_tasks_type_status ON analysis_tasks(type, status, created_at DESC);
CREATE INDEX ix_analysis_tasks_output_session ON analysis_tasks(output_session_id);
CREATE INDEX ix_offline_analysis_tasks_video_path ON offline_analysis_tasks(video_path);
CREATE INDEX ix_offline_analysis_runs_batch ON offline_analysis_runs(batch_id, created_at DESC);
CREATE INDEX ix_offline_analysis_runs_status ON offline_analysis_runs(status, created_at);
CREATE INDEX ix_offline_analysis_run_sources_run ON offline_analysis_run_sources(run_id, camera_id);
CREATE INDEX ix_offline_analysis_result_chunks_source_pts
    ON offline_analysis_result_chunks(run_source_id, start_pts_ms, end_pts_ms);
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
CREATE INDEX ix_participant_repetitions_session ON participant_repetitions(session_id);
CREATE INDEX ix_participant_repetitions_participant ON participant_repetitions(participant_id);
CREATE INDEX ix_participant_repetitions_athlete ON participant_repetitions(athlete_id);
CREATE INDEX ix_participant_repetitions_action_repetition ON participant_repetitions(action_repetition_id);
CREATE INDEX ix_participant_repetitions_video_file ON participant_repetitions(video_file_id);
CREATE INDEX ix_participant_repetitions_review ON participant_repetitions(review_status);
CREATE INDEX ix_participant_repetitions_track ON participant_repetitions(track_id);
CREATE INDEX ix_participant_repetitions_camera ON participant_repetitions(camera_id);
CREATE INDEX ix_participant_repetitions_frame_time ON participant_repetitions(frame_time_ms);
CREATE INDEX ix_participant_pose_frames_session_time ON participant_pose_frames(session_id, frame_time_ms);
CREATE INDEX ix_participant_pose_frames_participant ON participant_pose_frames(participant_id);
CREATE INDEX ix_participant_pose_frames_athlete ON participant_pose_frames(athlete_id);
CREATE INDEX ix_participant_pose_frames_camera_track ON participant_pose_frames(camera_id, track_id);
CREATE INDEX ix_participant_pose_frames_video_file ON participant_pose_frames(video_file_id);
CREATE INDEX ix_track_points_participant_time ON track_points(participant_id, t_ms);
CREATE INDEX ix_track_points_camera_time ON track_points(camera_id, t_ms);
CREATE INDEX ix_speed_metrics_participant_time ON speed_metrics(participant_id, t_ms);
CREATE INDEX ix_joint_metrics_participant_time ON joint_metrics(participant_id, t_ms);
CREATE INDEX ix_joint_metrics_filter ON joint_metrics(participant_id, joint, side, t_ms);
"""


def create_schema(engine: Engine) -> None:
    with engine.begin() as connection:
        connection.execute(text(SCHEMA_SQL))


def drop_schema(engine: Engine) -> None:
    tables = ", ".join(BUSINESS_TABLES)
    with engine.begin() as connection:
        connection.execute(text(f"DROP TABLE IF EXISTS {tables} CASCADE"))

from __future__ import annotations

import hashlib
import os
import subprocess
import tempfile
import threading
import time
from datetime import datetime
from pathlib import Path
from urllib.parse import urlsplit

from .api import AnalysisApi


def required_env(name: str) -> str:
    value = os.getenv(name, "").strip()
    if not value:
        raise RuntimeError(f"{name} is required")
    return value


def nas_path(root: Path, uri: str) -> Path:
    parts = urlsplit(uri)
    if parts.scheme != "nas":
        raise ValueError(f"unsupported source URI: {uri}")
    relative = "/".join(part for part in (parts.netloc, parts.path.lstrip("/")) if part)
    resolved_root = root.resolve()
    resolved = (resolved_root / relative).resolve()
    if resolved_root not in resolved.parents:
        raise ValueError(f"source URI escapes NAS root: {uri}")
    return resolved


def write_gallery(path: Path, entries: list[dict]) -> None:
    with path.open("w", encoding="utf-8", newline="\n") as stream:
        for entry in entries:
            embedding = ",".join(str(float(value)) for value in entry["embedding"])
            label = str(entry.get("label") or entry["athleteId"]).replace("\t", " ")
            stream.write(f"{entry['athleteId']}\t{label}\t{embedding}\n")


def chunk_event(parts: list[str], artifact_root_uri: str) -> tuple[str, dict]:
    if len(parts) != 9:
        raise ValueError("invalid CHUNK event")
    source_id, relative_path = parts[1], parts[2]
    artifact_uri = artifact_root_uri.rstrip("/") + "/" + relative_path.replace("\\", "/")
    return source_id, {
        "artifactUri": artifact_uri,
        "startFrameIndex": int(parts[3]),
        "endFrameIndex": int(parts[4]),
        "startPtsMs": int(parts[5]),
        "endPtsMs": int(parts[6]),
        "frameCount": int(parts[7]),
        "objectCount": int(parts[8]),
    }


def run_analysis(api: AnalysisApi, run: dict, nas_root: Path, binary: Path, worker_id: str) -> None:
    artifact_root = nas_path(nas_root, run["artifactRootUri"])
    artifact_root.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="iskating-gallery-") as directory:
        gallery_path = Path(directory) / "gallery.tsv"
        write_gallery(gallery_path, api.gallery(run["id"]))
        command = [
            str(binary),
            "--output-root", str(artifact_root),
            "--model-version", run["modelVersion"],
            "--preprocessing-version", run["preprocessingVersion"],
            "--gallery", str(gallery_path),
        ]
        started_values = [
            datetime.fromisoformat(source["sourceStartedAt"].replace("Z", "+00:00"))
            for source in run["sources"] if source.get("sourceStartedAt")
        ]
        batch_started_at = min(started_values) if started_values else None
        for source in run["sources"]:
            source_path = nas_path(nas_root, source["sourceUri"])
            source_started_at = (
                datetime.fromisoformat(source["sourceStartedAt"].replace("Z", "+00:00"))
                if source.get("sourceStartedAt") else batch_started_at
            )
            source_offset_ms = (
                int((source_started_at - batch_started_at).total_seconds() * 1000)
                if source_started_at and batch_started_at else 0
            )
            command.extend([
                "--source",
                ",".join([
                    source["id"], str(source["cameraId"]), str(source_path),
                    str(source_offset_ms), str(source["manualCorrectionMs"]),
                    str(source["lastFrameIndex"]),
                ]),
            ])
        process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1)
        heartbeat_stop = threading.Event()

        def send_heartbeats() -> None:
            while not heartbeat_stop.wait(30):
                if api.heartbeat(run["id"], worker_id):
                    process.terminate()
                    return

        heartbeat_thread = threading.Thread(target=send_heartbeats, name="analysis-heartbeat", daemon=True)
        heartbeat_thread.start()
        assert process.stdout is not None
        totals: dict[str, int] = {}
        try:
            for raw_line in process.stdout:
                line = raw_line.rstrip("\r\n")
                parts = line.split("\t")
                if parts[0] == "CHUNK":
                    source_id, payload = chunk_event(parts, run["artifactRootUri"])
                    artifact_path = nas_path(nas_root, payload["artifactUri"])
                    payload["checksumSha256"] = hashlib.sha256(artifact_path.read_bytes()).hexdigest()
                    api.register_chunk(source_id, payload)
                elif parts[0] == "FINISH" and len(parts) == 3:
                    totals[parts[1]] = int(parts[2])
                else:
                    print(line, flush=True)
            return_code = process.wait()
            if return_code != 0:
                raise RuntimeError(f"DeepStream pipeline exited with code {return_code}")
            for source in run["sources"]:
                api.finish_source(source["id"], totals.get(source["id"], 0))
        except Exception as exc:
            process.terminate()
            process.wait(timeout=30)
            for source in run["sources"]:
                if source["id"] not in totals:
                    api.finish_source(source["id"], 0, str(exc))
            raise
        finally:
            heartbeat_stop.set()
            heartbeat_thread.join(timeout=5)


def main() -> int:
    api = AnalysisApi(required_env("ISKATING_API_BASE_URL"), required_env("ISKATING_ANALYSIS_WORKER_TOKEN"))
    worker_id = os.getenv("ISKATING_ANALYSIS_WORKER_ID", os.uname().nodename)
    nas_root = Path(required_env("ISKATING_ANALYSIS_NAS_ROOT"))
    binary = Path(os.getenv("ISKATING_DEEPSTREAM_BINARY", "/opt/iskating/bin/iskating-fullrate"))
    poll_seconds = max(1, int(os.getenv("ISKATING_ANALYSIS_POLL_SECONDS", "5")))
    while True:
        run = api.claim(worker_id)
        if run is None:
            time.sleep(poll_seconds)
            continue
        try:
            run_analysis(api, run, nas_root, binary, worker_id)
        except Exception as exc:
            print(f"analysis run {run['id']} failed: {exc}", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

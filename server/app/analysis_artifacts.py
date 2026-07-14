from __future__ import annotations

import gzip
import hashlib
import json
from pathlib import Path
from typing import Any, Iterable
from urllib.parse import urlsplit


def resolve_nas_uri(root: Path, uri: str) -> Path:
    parts = urlsplit(uri)
    if parts.scheme != "nas":
        raise ValueError("analysis assets must use nas:// URIs")
    relative = "/".join(part for part in (parts.netloc, parts.path.lstrip("/")) if part)
    if not relative:
        raise ValueError("NAS URI path is empty")
    resolved_root = root.expanduser().resolve()
    resolved = (resolved_root / relative).resolve()
    if resolved != resolved_root and resolved_root not in resolved.parents:
        raise ValueError("NAS URI escapes the configured root")
    return resolved


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().lower()


def read_frame_chunks(paths: Iterable[Path], from_ms: int, to_ms: int) -> list[dict[str, Any]]:
    frames: list[dict[str, Any]] = []
    seen: set[int] = set()
    for path in paths:
        with gzip.open(path, "rt", encoding="utf-8") as stream:
            header = json.loads(stream.readline())
            if header.get("type") != "header" or int(header.get("schemaVersion", 0)) != 1:
                raise ValueError(f"unsupported analysis chunk: {path.name}")
            for line in stream:
                if not line.strip():
                    continue
                frame = json.loads(line)
                batch_time_ms = int(frame.get("batchTimeMs", -1))
                frame_index = int(frame.get("frameIndex", -1))
                if batch_time_ms < from_ms or batch_time_ms > to_ms or frame_index in seen:
                    continue
                seen.add(frame_index)
                frames.append(frame)
    frames.sort(key=lambda item: (int(item.get("batchTimeMs", -1)), int(item.get("frameIndex", -1))))
    return frames


def coverage_gaps(ranges: Iterable[tuple[int, int]], from_ms: int, to_ms: int) -> list[dict[str, int]]:
    cursor = from_ms
    gaps: list[dict[str, int]] = []
    for start_ms, end_ms in sorted(ranges):
        if end_ms < cursor:
            continue
        if start_ms > cursor:
            gaps.append({"fromMs": cursor, "toMs": min(start_ms - 1, to_ms)})
        cursor = max(cursor, end_ms + 1)
        if cursor > to_ms:
            break
    if cursor <= to_ms:
        gaps.append({"fromMs": cursor, "toMs": to_ms})
    return gaps

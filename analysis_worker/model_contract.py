from __future__ import annotations

import math
from collections.abc import Sequence


def parse_yolo26x(rows: Sequence[Sequence[float]], score_threshold: float = 0.35) -> list[tuple[float, float, float, float, float]]:
    detections = []
    for row in rows:
        if len(row) != 6 or int(row[5]) != 0 or row[4] < score_threshold:
            continue
        detections.append((float(row[0]), float(row[1]), float(row[2]), float(row[3]), float(row[4])))
    return detections


def scale_box(box: Sequence[float], network_size: tuple[int, int], source_size: tuple[int, int]) -> tuple[float, float, float, float]:
    network_width, network_height = network_size
    source_width, source_height = source_size
    scale = min(network_width / source_width, network_height / source_height)
    pad_x = (network_width - source_width * scale) / 2.0
    pad_y = (network_height - source_height * scale) / 2.0
    x1 = max(0.0, min(source_width, (box[0] - pad_x) / scale))
    y1 = max(0.0, min(source_height, (box[1] - pad_y) / scale))
    x2 = max(0.0, min(source_width, (box[2] - pad_x) / scale))
    y2 = max(0.0, min(source_height, (box[3] - pad_y) / scale))
    return x1, y1, x2, y2


def personvit_value(channel: int) -> float:
    return (channel / 255.0 - 0.5) / 0.5


def gallery_match(
    embedding: Sequence[float],
    gallery: Sequence[tuple[str, Sequence[float]]],
    threshold: float = 0.60,
    ambiguous_margin: float = 0.05,
) -> tuple[str | None, str, float]:
    def cosine(left: Sequence[float], right: Sequence[float]) -> float:
        dot = sum(a * b for a, b in zip(left, right))
        norm = math.sqrt(sum(value * value for value in left) * sum(value * value for value in right))
        return dot / norm if norm else 0.0

    scores = sorted(((cosine(embedding, sample), athlete_id) for athlete_id, sample in gallery), reverse=True)
    if not scores or scores[0][0] < threshold:
        return None, "unknown", scores[0][0] if scores else 0.0
    if len(scores) > 1 and scores[0][0] - scores[1][0] < ambiguous_margin:
        return None, "ambiguous", scores[0][0]
    return scores[0][1], "identified", scores[0][0]


def accepts_dynamic_batch(input_shape: Sequence[int | str | None], maximum: int) -> bool:
    if len(input_shape) != 4 or maximum < 1:
        return False
    batch = input_shape[0]
    return batch in (-1, None, "batch")

from __future__ import annotations

import gzip
import hashlib
import json
import tempfile
import unittest
from pathlib import Path

from server.app.analysis_artifacts import coverage_gaps, read_frame_chunks, resolve_nas_uri, sha256_file


class AnalysisArtifactTests(unittest.TestCase):
    def test_nas_uri_stays_inside_root(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.assertEqual(resolve_nas_uri(root, "nas://videos/cam01.mp4"), root / "videos" / "cam01.mp4")
            with self.assertRaises(ValueError):
                resolve_nas_uri(root, "nas://../outside.mp4")
            with self.assertRaises(ValueError):
                resolve_nas_uri(root, "C:/recording.mp4")

    def test_reads_sorted_unique_frames_in_requested_window(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "chunk.jsonl.gz"
            with gzip.open(path, "wt", encoding="utf-8") as stream:
                stream.write(json.dumps({"type": "header", "schemaVersion": 1}) + "\n")
                stream.write(json.dumps({"frameIndex": 2, "batchTimeMs": 34, "objects": []}) + "\n")
                stream.write(json.dumps({"frameIndex": 1, "batchTimeMs": 17, "objects": []}) + "\n")
                stream.write(json.dumps({"frameIndex": 0, "batchTimeMs": 0, "objects": []}) + "\n")
            frames = read_frame_chunks([path, path], 10, 40)
            self.assertEqual([frame["frameIndex"] for frame in frames], [1, 2])
            self.assertEqual(sha256_file(path), hashlib.sha256(path.read_bytes()).hexdigest())

    def test_reports_uncovered_time_ranges(self) -> None:
        self.assertEqual(
            coverage_gaps([(0, 99), (200, 299), (300, 350)], 0, 400),
            [{"fromMs": 100, "toMs": 199}, {"fromMs": 351, "toMs": 400}],
        )


if __name__ == "__main__":
    unittest.main()

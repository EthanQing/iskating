from __future__ import annotations

import hashlib
import tempfile
import unittest
from pathlib import Path

from analysis_worker.worker import chunk_event, nas_path, write_gallery


class AnalysisWorkerTests(unittest.TestCase):
    def test_maps_portable_nas_uri(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.assertEqual(nas_path(root, "nas://videos/camera01.mp4"), root / "videos" / "camera01.mp4")
            with self.assertRaises(ValueError):
                nas_path(root, "nas://../escape.mp4")

    def test_parses_chunk_event(self) -> None:
        source_id, payload = chunk_event(
            "CHUNK\tsource-1\tcamera01/000000/chunk_0_599.jsonl.gz\t0\t599\t0\t9983\t600\t1200".split("\t"),
            "nas://analysis/batch/run",
        )
        self.assertEqual(source_id, "source-1")
        self.assertEqual(payload["artifactUri"], "nas://analysis/batch/run/camera01/000000/chunk_0_599.jsonl.gz")
        self.assertEqual(payload["frameCount"], 600)

    def test_writes_gallery_tsv(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "gallery.tsv"
            write_gallery(path, [{"athleteId": "a1", "label": "Alice", "embedding": [0.0, 1.0]}])
            self.assertEqual(path.read_text(encoding="utf-8"), "a1\tAlice\t0.0,1.0\n")
            self.assertEqual(len(hashlib.sha256(path.read_bytes()).hexdigest()), 64)

    def test_deepstream_configuration_is_strict_full_rate(self) -> None:
        root = Path(__file__).resolve().parents[2]
        pgie = (root / "analysis_worker/config/pgie_yolo26x.txt").read_text(encoding="utf-8")
        sgie = (root / "analysis_worker/config/sgie_personvit.txt").read_text(encoding="utf-8")
        native = (root / "analysis_worker/native/fullrate_pipeline.cpp").read_text(encoding="utf-8")
        self.assertIn("batch-size=12", pgie)
        self.assertIn("interval=0", pgie)
        self.assertIn("cluster-mode=4", pgie)
        self.assertIn("batch-size=32", sgie)
        self.assertIn("secondary-reinfer-interval=30", sgie)
        self.assertIn('"leaky", 0', native)


if __name__ == "__main__":
    unittest.main()

from __future__ import annotations

import unittest
from pathlib import Path


class JointMetricsSchemaTests(unittest.TestCase):
    def test_joint_metrics_persistence_and_query_contract(self) -> None:
        root = Path(__file__).parents[1]
        schema = (root / "app" / "schema.py").read_text(encoding="utf-8")
        api = (root / "app" / "main.py").read_text(encoding="utf-8")
        self.assertIn("CREATE TABLE joint_metrics", schema)
        self.assertIn('    "joint_metrics",', schema)
        for column in ("participant_id", "t_ms", "joint", "side", "angle_deg", "angular_velocity_deg_per_sec", "algorithm_version"):
            self.assertIn(column, schema)
        self.assertIn("UNIQUE (participant_id, t_ms, camera_id, joint, side)", schema)
        self.assertIn('"/training/sessions/{session_id}/joint-metrics"', api)
        self.assertIn("def insert_joint_metric", api)
        self.assertIn("fromMs", api)
        self.assertIn("toMs", api)


if __name__ == "__main__":
    unittest.main()

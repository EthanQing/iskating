from __future__ import annotations

import unittest
from pathlib import Path

class TrackPointsSchemaTests(unittest.TestCase):
    def test_track_points_has_required_columns_and_identity(self) -> None:
        schema = (Path(__file__).parents[1] / "app" / "schema.py").read_text(encoding="utf-8")
        self.assertIn('"track_points",', schema)
        self.assertIn("CREATE TABLE track_points", schema)
        for column in ("participant_id", "t_ms", "speed_source", "camera_id", "confidence"):
            self.assertIn(column, schema)
        self.assertIn("UNIQUE (participant_id, t_ms, camera_id)", schema)
        self.assertIn("ix_track_points_participant_time", schema)


if __name__ == "__main__":
    unittest.main()

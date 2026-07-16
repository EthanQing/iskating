from __future__ import annotations

import unittest
from pathlib import Path


class AnalysisTasksSchemaTests(unittest.TestCase):
    def test_generic_task_model_and_links_exist(self) -> None:
        root = Path(__file__).parents[1]
        schema = (root / "app" / "schema.py").read_text(encoding="utf-8")
        api = (root / "app" / "main.py").read_text(encoding="utf-8")
        self.assertIn("CREATE TABLE analysis_tasks", schema)
        for column in ("type", "status", "progress", "input", "output_session_id", "error", "created_at"):
            self.assertIn(column, schema)
        self.assertIn("analysis_task_id uuid REFERENCES analysis_tasks", schema)
        self.assertIn('"/analysis-tasks"', api)
        self.assertIn("offline_import", api)
        self.assertIn("full_rate_batch", api)
        self.assertIn("output_session_id=:session_id", api)


if __name__ == "__main__":
    unittest.main()

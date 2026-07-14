from __future__ import annotations

import unittest

from analysis_worker.model_contract import accepts_dynamic_batch, gallery_match, parse_yolo26x, personvit_value, scale_box


class AnalysisModelContractTests(unittest.TestCase):
    def test_yolo_parser_filters_score_and_person_class(self) -> None:
        rows = [[10, 20, 30, 40, 0.90, 0], [1, 2, 3, 4, 0.20, 0], [5, 6, 7, 8, 0.95, 1]]
        self.assertEqual(parse_yolo26x(rows), [(10.0, 20.0, 30.0, 40.0, 0.9)])

    def test_letterbox_box_scales_to_source_coordinates(self) -> None:
        self.assertEqual(scale_box((0, 140, 640, 500), (640, 640), (1920, 1080)), (0.0, 0.0, 1920.0, 1080.0))

    def test_personvit_normalization_keeps_current_contract(self) -> None:
        self.assertAlmostEqual(personvit_value(0), -1.0)
        self.assertAlmostEqual(personvit_value(255), 1.0)
        self.assertAlmostEqual(personvit_value(128), 0.0039215686)

    def test_gallery_match_reports_ambiguous_margin(self) -> None:
        athlete_id, status, confidence = gallery_match((1.0, 0.0), [("a", (1.0, 0.0)), ("b", (0.999, 0.001))])
        self.assertIsNone(athlete_id)
        self.assertEqual(status, "ambiguous")
        self.assertGreater(confidence, 0.99)

    def test_dynamic_batch_contract_accepts_yolo_and_personvit_profiles(self) -> None:
        self.assertTrue(accepts_dynamic_batch(("batch", 3, 640, 640), 12))
        self.assertTrue(accepts_dynamic_batch((-1, 3, 256, 128), 32))
        self.assertFalse(accepts_dynamic_batch((1, 3, 640, 640), 12))


if __name__ == "__main__":
    unittest.main()

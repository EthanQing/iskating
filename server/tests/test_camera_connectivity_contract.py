from __future__ import annotations

import unittest
from pathlib import Path


class CameraConnectivityContractTests(unittest.TestCase):
    def test_diagnostic_fields_and_error_mappings_exist(self) -> None:
        root = Path(__file__).parents[2]
        header = (root / "cameraconnectivitytester.h").read_text(encoding="utf-8")
        source = (root / "cameraconnectivitytester.cpp").read_text(encoding="utf-8")
        for field in (
            "addressStatus",
            "openElapsedMs",
            "firstFrameElapsedMs",
            "failureStage",
            "errorCode",
            "ffmpegErrorCode",
        ):
            self.assertIn(field, header)
        for error_code in (
            "rtsp_auth_failed",
            "rtsp_path_not_found",
            "rtsp_open_timeout",
            "address_resolution_failed",
            "rtsp_connection_refused",
            "first_frame_decode_failed",
        ):
            self.assertIn(error_code, source)

    def test_url_composition_keeps_password_out_of_logs(self) -> None:
        root = Path(__file__).parents[2]
        source = (root / "cameraconnectivitytester.cpp").read_text(encoding="utf-8")
        self.assertIn("composeCameraPreviewTestUrl", source)
        self.assertIn("safeCameraTestUrlForLog", source)
        self.assertIn('url.setPassword(QStringLiteral("***"))', source)


if __name__ == "__main__":
    unittest.main()

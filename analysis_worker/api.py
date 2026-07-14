from __future__ import annotations

import json
from typing import Any
from urllib.error import HTTPError
from urllib.request import Request, urlopen


class AnalysisApi:
    def __init__(self, base_url: str, worker_token: str) -> None:
        self.base_url = base_url.rstrip("/")
        self.worker_token = worker_token

    def _request(self, method: str, path: str, payload: dict[str, Any] | None = None) -> Any:
        body = json.dumps(payload).encode("utf-8") if payload is not None else None
        request = Request(
            self.base_url + path,
            data=body,
            method=method,
            headers={"Content-Type": "application/json", "X-Analysis-Worker-Token": self.worker_token},
        )
        try:
            with urlopen(request, timeout=120) as response:
                return json.loads(response.read().decode("utf-8"))
        except HTTPError as exc:
            detail = exc.read().decode("utf-8", errors="replace")
            raise RuntimeError(f"analysis API {method} {path} failed: {exc.code} {detail}") from exc

    def claim(self, worker_id: str) -> dict[str, Any] | None:
        return self._request("POST", "/analysis-worker/runs/claim", {"workerId": worker_id})["run"]

    def heartbeat(self, run_id: str, worker_id: str) -> bool:
        result = self._request("POST", f"/analysis-worker/runs/{run_id}/heartbeat", {"workerId": worker_id})
        return bool(result["cancelRequested"])

    def gallery(self, run_id: str) -> list[dict[str, Any]]:
        return self._request("GET", f"/analysis-worker/runs/{run_id}/gallery")["entries"]

    def register_chunk(self, source_id: str, chunk: dict[str, Any]) -> dict[str, Any]:
        return self._request("POST", f"/analysis-worker/sources/{source_id}/chunks", chunk)

    def finish_source(self, source_id: str, total_frames: int, error: str = "") -> dict[str, Any]:
        return self._request(
            "POST",
            f"/analysis-worker/sources/{source_id}/finish",
            {"totalFrames": total_frames, "errorMessage": error},
        )

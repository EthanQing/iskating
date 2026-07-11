from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


def main() -> None:
    parser = argparse.ArgumentParser(description="Check athlete model files and write their SHA256 manifest.")
    parser.add_argument("--model-dir", type=Path, default=Path(__file__).parents[1] / "models" / "athlete")
    parser.add_argument("--write", action="store_true")
    args = parser.parse_args()

    manifest = args.model_dir / "athlete_models.json"
    metadata = json.loads(manifest.read_text(encoding="utf-8"))
    files = [metadata["detector"]["onnx"], metadata["reid"]["onnx"]]
    lines = []
    for name in files:
        path = args.model_dir / name
        if not path.is_file():
            raise SystemExit(f"missing model: {path}")
        lines.append(f"{sha256(path)}  {name}")

    output = "\n".join(lines) + "\n"
    checksum_path = args.model_dir / "athlete_models.sha256"
    if args.write:
        checksum_path.write_text(output, encoding="utf-8")
    elif checksum_path.is_file() and checksum_path.read_text(encoding="utf-8") != output:
        raise SystemExit(f"checksum mismatch: {checksum_path}")
    print(output, end="")


if __name__ == "__main__":
    main()

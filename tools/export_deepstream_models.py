from __future__ import annotations

import argparse
import shutil
import tempfile
from pathlib import Path

import torch
from ultralytics import YOLO

from convert_personvit_msmt17 import PersonVitEmbedding, load_base_weights


def export_personvit(checkpoint: Path, output: Path) -> None:
    model = PersonVitEmbedding().eval()
    load_base_weights(model, checkpoint)
    output.parent.mkdir(parents=True, exist_ok=True)
    torch.onnx.export(
        model,
        torch.zeros(1, 3, 256, 128),
        output,
        input_names=["images"],
        output_names=["embedding"],
        opset_version=17,
        do_constant_folding=True,
        dynamo=False,
        dynamic_axes={"images": {0: "batch"}, "embedding": {0: "batch"}},
    )


def main() -> int:
    parser = argparse.ArgumentParser(description="Export dynamic-batch YOLO26x and PersonViT models for DeepStream.")
    parser.add_argument("--model-dir", type=Path, required=True)
    args = parser.parse_args()
    model_dir = args.model_dir.resolve()
    output_dir = model_dir / "deepstream"
    output_dir.mkdir(parents=True, exist_ok=True)

    yolo_output = output_dir / "yolo26x_dynamic.onnx"
    with tempfile.TemporaryDirectory(prefix="iskating-yolo-export-") as directory:
        temporary_model = Path(directory) / "yolo26x.pt"
        shutil.copy2(model_dir / "yolo26x.pt", temporary_model)
        exported_yolo = Path(YOLO(temporary_model).export(
            format="onnx", imgsz=640, batch=12, dynamic=True, nms=False, end2end=True, opset=17
        ))
        shutil.move(str(exported_yolo), yolo_output)
    export_personvit(model_dir / "transreid_msmt17_vit_base.pth",
                     output_dir / "personvit_msmt17_vit_base_dynamic.onnx")
    print(yolo_output)
    print(output_dir / "personvit_msmt17_vit_base_dynamic.onnx")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

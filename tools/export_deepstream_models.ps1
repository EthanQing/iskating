param(
    [string]$ModelDir = (Join-Path $PSScriptRoot '..\models\athlete')
)

$ErrorActionPreference = 'Stop'
python -m pip install --upgrade ultralytics onnx
python "$PSScriptRoot\export_deepstream_models.py" --model-dir $ModelDir
Get-FileHash (Join-Path $ModelDir 'deepstream\yolo26x_dynamic.onnx') -Algorithm SHA256
Get-FileHash (Join-Path $ModelDir 'deepstream\personvit_msmt17_vit_base_dynamic.onnx') -Algorithm SHA256

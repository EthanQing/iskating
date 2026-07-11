param(
    [string]$ModelDir = (Join-Path $PSScriptRoot '..\models\athlete')
)

$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path $ModelDir | Out-Null

$yolo = Join-Path $ModelDir 'yolo26x.pt'
if (-not (Test-Path $yolo)) {
    Invoke-WebRequest -UseBasicParsing `
        -Uri 'https://github.com/ultralytics/assets/releases/download/v8.4.0/yolo26x.pt' `
        -OutFile $yolo
}

$transReid = Join-Path $ModelDir 'transreid_msmt17_vit_base.pth'
if (-not (Test-Path $transReid)) {
    curl.exe -L --fail --retry 3 `
        --output $transReid `
        'https://drive.usercontent.google.com/download?id=1iF5JNPw9xi-rLY3Ri9EY-PFAkK6Vg_Pf&export=download&confirm=t'
}

$exportDir = Join-Path $ModelDir 'yolo26x_export'
python -m pip install --upgrade ultralytics onnx
python -c "from ultralytics import YOLO; YOLO(r'$yolo').export(format='onnx', imgsz=640, dynamic=False, nms=False, end2end=True, opset=17, project=r'$ModelDir', name='yolo26x_export')"
$exportedYolo = Join-Path $exportDir 'yolo26x.onnx'
if (-not (Test-Path $exportedYolo)) {
    throw "YOLO26x export did not produce $exportedYolo"
}
Copy-Item -Force $exportedYolo (Join-Path $ModelDir 'yolo26x.onnx')
Remove-Item -Recurse -Force $exportDir
python "$PSScriptRoot\convert_personvit_msmt17.py" `
    --checkpoint $transReid `
    --output (Join-Path $ModelDir 'personvit_msmt17_vit_base.onnx')

Get-FileHash (Join-Path $ModelDir 'yolo26x.onnx') -Algorithm SHA256
Get-FileHash (Join-Path $ModelDir 'personvit_msmt17_vit_base.onnx') -Algorithm SHA256

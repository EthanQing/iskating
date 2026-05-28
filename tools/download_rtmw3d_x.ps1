param(
    [string]$Destination = (Join-Path $PSScriptRoot '..\models\body\rtmw3d-x.onnx')
)

$ErrorActionPreference = 'Stop'

$url = 'https://huggingface.co/Soykaf/RTMW3D-x/resolve/main/onnx/rtmw3d-x_8xb64_cocktail14-384x288-b0a0eab7_20240626.onnx'
$resolvedDestination = [System.IO.Path]::GetFullPath($Destination)
$destinationDir = Split-Path -Parent $resolvedDestination

if (!(Test-Path $destinationDir)) {
    New-Item -ItemType Directory -Path $destinationDir | Out-Null
}

Write-Host "Downloading RTMW3D-x ONNX model..."
Write-Host "Source: $url"
Write-Host "Target: $resolvedDestination"

Invoke-WebRequest -Uri $url -OutFile $resolvedDestination

$file = Get-Item $resolvedDestination
Write-Host ("Done. Size: {0:N0} bytes" -f $file.Length)

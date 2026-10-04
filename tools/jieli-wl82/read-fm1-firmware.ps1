[CmdletBinding()]
param(
    [switch]$Full
)

$ErrorActionPreference = 'Stop'

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Raw SCSI access to the WL82 disk requires an elevated PowerShell. Reopen PowerShell as Administrator and run this script again.'
}

$deviceMatches = @(Get-CimInstance -ClassName Win32_DiskDrive | Where-Object {
    $_.PNPDeviceID -like 'USBSTOR\DISK&VEN_WL82&PROD_UBOOT1.00*'
})
if ($deviceMatches.Count -ne 1) {
    throw "Expected exactly one attached WL82 UBOOT1.00 disk; found $($deviceMatches.Count). No command was sent."
}

$workspaceRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$helperPath = Join-Path $PSScriptRoot 'read_fm1_firmware.py'
$python = (Get-Command python -ErrorAction Stop).Source
$outputRoot = Join-Path $workspaceRoot 'tmp'
[System.IO.Directory]::CreateDirectory($outputRoot) | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'

if ($Full) {
    $outputPath = Join-Path $outputRoot "fm1-stock-boot-app-$stamp.bin"
    $helperArgs = @('--device', $deviceMatches[0].DeviceID, '--full-output', $outputPath)
} else {
    $outputPath = Join-Path $outputRoot "fm1-read-probe-$stamp.bin"
    $helperArgs = @('--device', $deviceMatches[0].DeviceID, '--probe-output', $outputPath)
}

Write-Output "Matched device: $($deviceMatches[0].Model) at $($deviceMatches[0].DeviceID)"
Write-Output "Output: $outputPath"
Write-Output 'No flash-array erase/program command is requested; loader setup can change NOR status/protection bits.'
& $python $helperPath @helperArgs
if ($LASTEXITCODE -ne 0) {
    throw "FM-1 read helper exited with code $LASTEXITCODE."
}

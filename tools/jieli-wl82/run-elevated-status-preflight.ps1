[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$RunDirectory, [switch]$OriginalControl,
      [ValidateSet(0,10)][int]$ReadDelayMs=0)
$ErrorActionPreference = 'Stop'
$resultPath = Join-Path $RunDirectory 'result.json'
try {
    if (Test-Path -LiteralPath $resultPath) { throw 'Result exists; refusing repeated RAM upload.' }
    $fm1Principal = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
    if (-not $fm1Principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Administrator token required.'
    }
    $fm1Targets = @(Get-CimInstance Win32_DiskDrive | Where-Object {
        $_.PNPDeviceID -like 'USBSTOR\DISK&VEN_WL82&PROD_UBOOT1.00*'
    })
    if ($fm1Targets.Count -ne 1) { throw 'Expected exactly one FM-1 boot device.' }
    $helper = Join-Path $PSScriptRoot 'fm1_status_preflight.py'
    $fm1ExtraArgs = @()
    if ($OriginalControl) { $fm1ExtraArgs += '--original-control' }
    $fm1ExtraArgs += @('--read-delay-ms', [string]$ReadDelayMs)
    & 'C:\Python311\python.exe' -u $helper --device $fm1Targets[0].DeviceID --confirm-cold-uboot --result $resultPath @fm1ExtraArgs > (Join-Path $RunDirectory 'preflight.log') 2>&1
    if ($LASTEXITCODE -ne 0) { throw "Preflight failed: exit $LASTEXITCODE; inspect preflight.log." }
} catch {
    if (-not (Test-Path -LiteralPath $resultPath)) {
        @{status='failed';error=$_.Exception.Message} | ConvertTo-Json |
            Set-Content -LiteralPath $resultPath -Encoding UTF8
    }
    exit 1
}

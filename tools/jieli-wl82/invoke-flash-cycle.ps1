# Unprivileged coordinator. Existing compiled build -> validate -> plan ->
# optional UBOOT -> existing writer -> reset -> bounded serial observation.
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$SessionRoot,
    [Parameter(Mandatory=$true)][string]$Build,
    [switch]$Flash,
    [switch]$Retry
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ($Retry -and -not $Flash) { throw '-Retry requires -Flash; it requests one deliberate same-image retry' }
$repo = (Resolve-Path -LiteralPath "$PSScriptRoot\..\..").Path
$request = Join-Path "$repo\tmp\flash-sessions" ([Guid]::NewGuid().ToString('N') + '-request.json')
& 'C:\Python311\python.exe' "$PSScriptRoot\prepare_flash_request.py" --build $Build --session $SessionRoot --out $request
if ($LASTEXITCODE -ne 0) { throw 'Build/package validation failed; no device operations' }
& "$PSScriptRoot\flash-session-client.ps1" -SessionRoot $SessionRoot -Operation plan -RequestFile $request
if (-not $Flash) { Write-Output 'Plan only. No device operations. Add -Flash for one complete cycle.'; return }
$status = & "$PSScriptRoot\flash-session-client.ps1" -SessionRoot $SessionRoot -Operation status | ConvertFrom-Json
if ($status.data.blocked -and -not $Retry) { throw 'Session is blocked; inspect logs, then use -Flash -Retry for the exact failed image' }
if ($Retry -and -not $status.data.blocked) { throw 'No failed flash to retry; use an ordinary -Flash cycle' }
$uboot = @(Get-CimInstance Win32_DiskDrive | Where-Object {$_.Model -eq 'WL82 UBOOT1.00 USB Device'})
if ($uboot.Count -eq 0) {
    if ($Retry) { throw 'Retry requires the FM-1 already in UBOOT; do not boot a partially written image' }
    & "$PSScriptRoot\flash-session-client.ps1" -SessionRoot $SessionRoot -Operation enter_uboot
} elseif ($uboot.Count -ne 1) { throw 'Ambiguous UBOOT devices' }
$writeOperation = if ($Retry) { 'retry_flash' } else { 'flash' }
& "$PSScriptRoot\flash-session-client.ps1" -SessionRoot $SessionRoot -Operation $writeOperation -RequestFile $request
try {
    & "$PSScriptRoot\flash-session-client.ps1" -SessionRoot $SessionRoot -Operation reset
} catch {
    Write-Warning 'Reset outcome uncertain. Observing once; never resending reset or rewriting automatically.'
}
& "$PSScriptRoot\flash-session-client.ps1" -SessionRoot $SessionRoot -Operation observe
$status = & "$PSScriptRoot\flash-session-client.ps1" -SessionRoot $SessionRoot -Operation status | ConvertFrom-Json
if ($status.data.blocked) { throw 'Session remains blocked; inspect logs' }
Write-Output 'Cycle completed with serial identity and advancing NES frames. Physical LCD/audio/input acceptance is still separate.'

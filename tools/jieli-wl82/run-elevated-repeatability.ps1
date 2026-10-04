[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$RunDirectory, [switch]$ConfirmColdUboot)
$ErrorActionPreference = 'Stop'
$resultPath = Join-Path $RunDirectory 'result.json'
if ((Test-Path -LiteralPath $resultPath) -or (Test-Path -LiteralPath (Join-Path $RunDirectory 'readback.log'))) {
    throw 'Existing run artifacts; refusing repeat execution.'
}
if ((Test-Path -LiteralPath (Join-Path $RunDirectory 'cold-probe.bin')) -or (Test-Path -LiteralPath (Join-Path $RunDirectory 'cold-probe.log'))) {
    throw 'Existing cold-probe artifacts; refusing another helper upload.'
}
try {
    $principal = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Administrator token required.' }
    $targets = @(Get-CimInstance Win32_DiskDrive | Where-Object { $_.PNPDeviceID -like 'USBSTOR\DISK&VEN_WL82&PROD_UBOOT1.00*' })
    if ($targets.Count -ne 1) { throw 'Expected exactly one WL82 UBOOT disk.' }
    if ($ConfirmColdUboot) {
        $probePath = 'F:\dev\fm1\tmp\fm1-read-probe-20260926-024212.bin'
        $probeHash = (Get-FileHash -LiteralPath $probePath -Algorithm SHA256).Hash
        if ($probeHash -ne '3c0dee98aa08fb1a5b9ed59ca9a37bc9d149fff46ae7b282bb65d4b996ada0e5') { throw 'Reference probe changed.' }
        $coldProbePath = Join-Path $RunDirectory 'cold-probe.bin'
        $ErrorActionPreference = 'Continue'
        try {
            & 'C:\Python311\python.exe' -u (Join-Path $PSScriptRoot 'read_fm1_firmware.py') --device $targets[0].DeviceID --reference-probe $probePath --probe-output $coldProbePath > (Join-Path $RunDirectory 'cold-probe.log') 2>&1
            $probeExitCode = $LASTEXITCODE
        } finally { $ErrorActionPreference = 'Stop' }
        if ($probeExitCode -ne 0) { throw 'Cold probe failed; no retry or second process.' }
        if ((Get-FileHash -LiteralPath $coldProbePath -Algorithm SHA256).Hash -ne $probeHash) { throw 'Cold probe mismatch.' }
    }
    # Windows PowerShell wraps native stderr as ErrorRecords. Preserve the
    # complete Python diagnostic instead of aborting at its first traceback line.
    $ErrorActionPreference = 'Continue'
    try {
        & 'C:\Python311\python.exe' -u (Join-Path $PSScriptRoot 'read_repeatability.py') --device $targets[0].DeviceID --directory $RunDirectory > (Join-Path $RunDirectory 'readback.log') 2>&1
        $readerExitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = 'Stop'
    }
    if ($readerExitCode -ne 0) { throw "Reader failed with exit code $readerExitCode; inspect readback.log" }
    @{status='complete'; array_program_or_erase_commands_sent=$false; passes=10; cold_probe_first=[bool]$ConfirmColdUboot} | ConvertTo-Json | Set-Content -LiteralPath $resultPath -Encoding UTF8
} catch {
    @{status='failed'; error=$_.Exception.Message; array_program_or_erase_commands_sent=$false; cold_probe_first=[bool]$ConfirmColdUboot} | ConvertTo-Json | Set-Content -LiteralPath $resultPath -Encoding UTF8
    exit 1
}

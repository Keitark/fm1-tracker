[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$RunDirectory,
    [switch]$ConfirmColdUboot
)
$ErrorActionPreference = 'Stop'
$resultPath = Join-Path $RunDirectory 'result.json'
$logPath = Join-Path $RunDirectory 'readback.log'
try {
    foreach ($name in @('result.json','readback.log','cold-probe.log','cold-probe.bin','fm1-flash-1MiB.bin','fm1-flash-1MiB.bin.partial','fm1-flash-1MiB.json')) {
        if (Test-Path -LiteralPath (Join-Path $RunDirectory $name)) {
            throw 'Run artifacts already exist; refusing repeated device operations.'
        }
    }
    $principal = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Administrator token is required for raw SCSI access.'
    }
    $targets = @(Get-CimInstance Win32_DiskDrive | Where-Object {
        $_.PNPDeviceID -like 'USBSTOR\DISK&VEN_WL82&PROD_UBOOT1.00*'
    })
    if ($targets.Count -ne 1) { throw 'Expected exactly one WL82 UBOOT disk.' }
    $pythonPath = 'C:\Python311\python.exe'
    $helperPath = Join-Path $PSScriptRoot 'read_fm1_firmware.py'
    $outputPath = Join-Path $RunDirectory 'fm1-flash-1MiB.bin'
    $probePath = 'F:\dev\fm1\tmp\fm1-read-probe-20260926-024212.bin'
    $probeHash = (Get-FileHash -LiteralPath $probePath -Algorithm SHA256).Hash
    if ($probeHash -ne '3c0dee98aa08fb1a5b9ed59ca9a37bc9d149fff46ae7b282bb65d4b996ada0e5') {
        throw 'Reference probe changed; stopping.'
    }
    if ($ConfirmColdUboot) {
        $coldProbePath = Join-Path $RunDirectory 'cold-probe.bin'
        & $pythonPath -u $helperPath --device $targets[0].DeviceID --reference-probe $probePath --probe-output $coldProbePath > (Join-Path $RunDirectory 'cold-probe.log') 2>&1
        if ($LASTEXITCODE -ne 0) { throw 'Cold probe failed; no second process or automatic retry.' }
        if ((Get-FileHash -LiteralPath $coldProbePath -Algorithm SHA256).Hash -ne $probeHash) {
            throw 'Cold probe differs from the original; stopping.'
        }
    }
    & $pythonPath -u $helperPath --device $targets[0].DeviceID --reuse-loader --reference-probe $probePath --raw-output $outputPath > $logPath 2>&1
    $readExitCode = $LASTEXITCODE
    if ($readExitCode -ne 0) { throw "Read helper exited with code $readExitCode; inspect readback.log." }
    @{ status='complete'; output=$outputPath; bytes=(Get-Item -LiteralPath $outputPath).Length; sha256=(Get-FileHash -LiteralPath $outputPath -Algorithm SHA256).Hash; cold_probe_first=[bool]$ConfirmColdUboot; reader='read_fm1_firmware.py'; transport='upstream scsiio'; array_program_or_erase_commands_sent=$false } |
        ConvertTo-Json | Set-Content -LiteralPath $resultPath -Encoding UTF8
} catch {
    if (-not (Test-Path -LiteralPath $resultPath)) {
        @{ status='failed'; error=$_.Exception.Message; cold_probe_first=[bool]$ConfirmColdUboot; reader='read_fm1_firmware.py'; transport='upstream scsiio'; array_program_or_erase_commands_sent=$false } | ConvertTo-Json | Set-Content -LiteralPath $resultPath -Encoding UTF8
    }
    exit 1
}

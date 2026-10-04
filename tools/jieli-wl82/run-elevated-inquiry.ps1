[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$RunDirectory, [switch]$AlignedAdapter)
$ErrorActionPreference = 'Stop'
$resultPath = Join-Path $RunDirectory 'result.json'
try {
    if (Test-Path -LiteralPath $resultPath) { throw 'Result already exists; refusing repeat operation.' }
    $fm1Principal = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
    if (-not $fm1Principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Administrator token is required for this device handle.'
    }
    $fm1Targets = @(Get-CimInstance Win32_DiskDrive | Where-Object {
        $_.PNPDeviceID -like 'USBSTOR\DISK&VEN_WL82&PROD_UBOOT1.00*'
    })
    if ($fm1Targets.Count -ne 1) { throw 'Expected exactly one WL82 UBOOT disk; no command sent.' }
    $helper = Join-Path $PSScriptRoot 'inquiry_only.py'
    $fm1ExtraArgs = @()
    if ($AlignedAdapter) { $fm1ExtraArgs += '--aligned-adapter' }
    $result = & 'C:\Python311\python.exe' -u $helper --device $fm1Targets[0].DeviceID @fm1ExtraArgs 2> (Join-Path $RunDirectory 'stderr.log')
    if ($LASTEXITCODE -ne 0) { throw "Inquiry failed with exit code $LASTEXITCODE; see stderr.log." }
    $report = $result | ConvertFrom-Json
    if ($report.status -ne 'complete' -or $report.cdb_hex -ne '120000002400' -or $report.commands_sent -ne 1) {
        throw 'Unexpected diagnostic report.'
    }
    $report | Add-Member -NotePropertyName device -NotePropertyValue $fm1Targets[0].DeviceID
    $report | ConvertTo-Json | Set-Content -LiteralPath $resultPath -Encoding UTF8
} catch {
    if (-not (Test-Path -LiteralPath $resultPath)) {
        @{status='failed';error=$_.Exception.Message} | ConvertTo-Json |
            Set-Content -LiteralPath $resultPath -Encoding UTF8
    }
    exit 1
}

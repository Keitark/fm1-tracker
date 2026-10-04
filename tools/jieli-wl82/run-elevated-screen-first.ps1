[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$RunDirectory,[switch]$RestoreV14)
$ErrorActionPreference='Stop'
if (Test-Path -LiteralPath (Join-Path $RunDirectory 'console.log')) { throw 'Run already used.' }
$targets=@(Get-CimInstance Win32_DiskDrive | Where-Object {$_.PNPDeviceID -like 'USBSTOR\DISK&VEN_WL82&PROD_UBOOT1.00*'})
if ($targets.Count -ne 1) { throw 'Expected exactly one WL82 UBOOT device.' }
$direction=@()
if ($RestoreV14) { $direction=@('--restore-v14') }
# Original helper must already be running after a successful cold-UBOOT read.
$ErrorActionPreference='Continue'
& 'C:\Python311\python.exe' -u (Join-Path $PSScriptRoot 'deploy_screen_first_via_cli.py') --device $targets[0].DeviceID --directory $RunDirectory @direction > (Join-Path $RunDirectory 'console.log') 2>&1
exit $LASTEXITCODE

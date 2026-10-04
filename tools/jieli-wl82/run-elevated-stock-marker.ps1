[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$RunDirectory,[switch]$RestoreV14,[string]$VerifiedPreflight)
$ErrorActionPreference='Stop'
if (Test-Path -LiteralPath (Join-Path $RunDirectory 'console.log')) { throw 'Run already used.' }
$targets=@(Get-CimInstance Win32_DiskDrive | Where-Object {$_.PNPDeviceID -like 'USBSTOR\DISK&VEN_WL82&PROD_UBOOT1.00*'})
if ($targets.Count -ne 1) { throw 'Expected exactly one WL82 UBOOT device.' }
$direction=@()
if ($RestoreV14) { $direction=@('--restore-v14') }
if ($VerifiedPreflight) { $direction+=@('--verified-preflight',$VerifiedPreflight) }
# Original WL82 helper must already be running after a successful cold read.
$ErrorActionPreference='Continue'
& 'C:\Python311\python.exe' -u (Join-Path $PSScriptRoot 'deploy_stock_marker_via_cli.py') --device $targets[0].DeviceID --directory $RunDirectory @direction > (Join-Path $RunDirectory 'console.log') 2>&1
exit $LASTEXITCODE

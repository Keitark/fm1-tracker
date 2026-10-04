[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$RunDirectory)
$ErrorActionPreference='Stop'
if (Test-Path -LiteralPath (Join-Path $RunDirectory 'console.log')) { throw 'Run already used.' }
$targets=@(Get-CimInstance Win32_DiskDrive | Where-Object {$_.PNPDeviceID -like 'USBSTOR\DISK&VEN_WL82&PROD_UBOOT1.00*'})
if ($targets.Count -ne 1) { throw 'Expected exactly one WL82 UBOOT device.' }
# Only the Python CLI orchestrator can issue commands; it pins both images,
# validates source manifests, and requires two fresh full baseline reads.
$ErrorActionPreference='Continue'
& 'C:\Python311\python.exe' -u (Join-Path $PSScriptRoot 'deploy_v14_smb1_via_cli.py') --device $targets[0].DeviceID --directory $RunDirectory > (Join-Path $RunDirectory 'console.log') 2>&1
exit $LASTEXITCODE

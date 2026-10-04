[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$RunDirectory)
$ErrorActionPreference='Stop'
if (@(Get-ChildItem -LiteralPath $RunDirectory).Count) { throw 'Run already used.' }
$targets=@(Get-CimInstance Win32_DiskDrive | Where-Object {$_.PNPDeviceID -like 'USBSTOR\DISK&VEN_WL82&PROD_UBOOT1.00*'})
if ($targets.Count -ne 1) { throw 'Expected exactly one WL82 UBOOT device.' }
$selector=Join-Path $PSScriptRoot 'deploy_pre_os_via_cli.py'
& 'C:\Python311\python.exe' -u $selector --validate-only > (Join-Path $RunDirectory 'offline-validation.log') 2>&1
if ($LASTEXITCODE -ne 0) { throw 'Offline image validation failed; no device operation.' }
$loader=Join-Path $PSScriptRoot '..\jl-uboot-tool\data\loaderblobs\usb\wl82loader.bin'
if ((Get-FileHash -LiteralPath $loader -Algorithm SHA256).Hash -ne 'd41da6126760c9d66660bcc0cac8d27d221806c5e369a8036921efe68dca5376') { throw 'WL82 helper hash mismatch.' }
# User reports a normal boot, manual UBOOT re-entry and reconnection. Upload
# only the original RAM helper. "help" reads no flash array and writes none.
$cli=Join-Path $PSScriptRoot '..\jl-uboot-tool\jluboottool.py'
$helperLog=Join-Path $RunDirectory 'helper.log'
$ErrorActionPreference='Continue'
& 'C:\Python311\python.exe' -u $cli --device $targets[0].DeviceID --chip wl82 --io-size 256 help > $helperLog 2>&1
$helperExit=$LASTEXITCODE
$ErrorActionPreference='Stop'
$helperText=Get-Content -LiteralPath $helperLog -Raw
if ($helperExit -ne 0 -or $helperText -notmatch 'The Loader has been successfully installed\.' -or $helperText -notmatch 'ID: 0x856014') { throw 'Helper installation/flash identity not confirmed; no write started.' }
$savedSource='F:\dev\fm1\private-backups\stock-marker-write-20260928-023712\final-1.bin'
$ErrorActionPreference='Continue'
& 'C:\Python311\python.exe' -u $selector --device $targets[0].DeviceID --directory $RunDirectory --verified-preflight $savedSource > (Join-Path $RunDirectory 'console.log') 2>&1
exit $LASTEXITCODE

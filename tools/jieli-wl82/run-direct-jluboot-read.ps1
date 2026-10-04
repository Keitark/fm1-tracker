# Elevation/logging only. The actual reader is jluboottool.py's read command.
[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$RunDirectory,
      [ValidateRange(1,5)][int]$ReadCount = 1,
      [switch]$ConfirmColdUboot)
$ErrorActionPreference = 'Stop'
$resultPath = Join-Path $RunDirectory 'result.json'
$outputPath = Join-Path $RunDirectory 'fm1-direct-1MiB.bin'
$logPath = Join-Path $RunDirectory 'jluboottool.log'
foreach ($path in @($resultPath, $outputPath, $logPath)) {
    if (Test-Path -LiteralPath $path) { throw 'Existing run artifacts; refusing overwrite.' }
}
try {
    if ($ReadCount -gt 1) {
        $passes = @()
        for ($pass = 1; $pass -le $ReadCount; $pass++) {
            $passDir = Join-Path $RunDirectory ('pass-{0:D2}' -f $pass)
            New-Item -ItemType Directory -Path $passDir -ErrorAction Stop | Out-Null
            # Each child invokes jluboottool.py itself, once, with its read command.
            $coldArgs = @()
            if ($ConfirmColdUboot -and $pass -eq 1) { $coldArgs = @('-ConfirmColdUboot') }
            & "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath -RunDirectory $passDir @coldArgs
            if ($LASTEXITCODE -ne 0) { throw "Direct CLI pass $pass failed; inspect its log." }
            $passResult = Get-Content -LiteralPath (Join-Path $passDir 'result.json') -Raw | ConvertFrom-Json
            if ($passResult.status -ne 'complete') { throw "Direct CLI pass $pass incomplete." }
            $passes += @{pass=$pass; directory=$passDir; bytes=$passResult.bytes; sha256=$passResult.sha256}
            @{status='running'; completed_passes=$pass; requested_passes=$ReadCount; passes=$passes} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $RunDirectory 'progress.json') -Encoding UTF8
        }
        @{status='complete'; tool='jluboottool.py'; completed_passes=$ReadCount; passes=$passes; array_program_or_erase_commands_sent=$false} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $resultPath -Encoding UTF8
        return
    }
    $targets = @(Get-CimInstance Win32_DiskDrive | Where-Object { $_.PNPDeviceID -like 'USBSTOR\DISK&VEN_WL82&PROD_UBOOT1.00*' })
    if ($targets.Count -ne 1) { throw 'Expected exactly one WL82 UBOOT disk.' }
    $cli = Join-Path $PSScriptRoot '..\jl-uboot-tool\jluboottool.py'
    $readCommand = "read 0 1048576 $outputPath"
    $loaderArgs = @('--reuse-loader')
    if ($ConfirmColdUboot) {
        $loaderPath = Join-Path $PSScriptRoot '..\jl-uboot-tool\data\loaderblobs\usb\wl82loader.bin'
        if ((Get-FileHash -LiteralPath $loaderPath -Algorithm SHA256).Hash -ne 'd41da6126760c9d66660bcc0cac8d27d221806c5e369a8036921efe68dca5376') {
            throw 'Original WL82 helper hash mismatch.'
        }
        # The CLI's normal cold-boot path uploads the original helper to RAM.
        $loaderArgs = @()
    }
    $ErrorActionPreference = 'Continue'
    try {
        & 'C:\Python311\python.exe' -u $cli --device $targets[0].DeviceID --chip wl82 @loaderArgs --io-size 256 $readCommand > $logPath 2>&1
        $cliExitCode = $LASTEXITCODE
    } finally { $ErrorActionPreference = 'Stop' }
    if ($cliExitCode -ne 0) { throw "jluboottool.py exited with code $cliExitCode" }
    if ((Get-Item -LiteralPath $outputPath).Length -ne 1048576) { throw 'Incomplete output length.' }
    @{ status='complete'; tool='jluboottool.py'; bytes=1048576; sha256=(Get-FileHash -LiteralPath $outputPath -Algorithm SHA256).Hash; loader_reused=(-not [bool]$ConfirmColdUboot); confirmed_cold_uboot=[bool]$ConfirmColdUboot; array_program_or_erase_commands_sent=$false } | ConvertTo-Json | Set-Content -LiteralPath $resultPath -Encoding UTF8
} catch {
    @{status='failed'; tool='jluboottool.py'; error=$_.Exception.Message; array_program_or_erase_commands_sent=$false} | ConvertTo-Json | Set-Content -LiteralPath $resultPath -Encoding UTF8
    exit 1
}

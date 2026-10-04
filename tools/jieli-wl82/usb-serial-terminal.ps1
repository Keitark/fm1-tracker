# Windows-side receive-only CDC log monitor. This does not install drivers,
# upload a helper, flash firmware, or turn a UBOOT disk into a serial port.
[CmdletBinding()]
param(
    [ValidatePattern('^COM[1-9][0-9]*$')]
    [string]$Port,
    [ValidateRange(1, 3600)]
    [int]$Seconds = 60,
    [ValidateRange(1200, 3000000)]
    [int]$BaudRate = 115200,
    [string]$LogPath,
    [switch]$List
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$available = @([System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object)
if ($List -or -not $Port) {
    if ($available.Count -eq 0) {
        Write-Host 'No COM ports detected. WL82 UBOOT1.00 is not a CDC serial port.'
    } else {
        Write-Host ('Available ports: ' + ($available -join ', '))
        Get-CimInstance Win32_PnPEntity |
            Where-Object { $_.Name -match '\(COM[1-9][0-9]*\)' } |
            Select-Object Name, PNPDeviceID |
            Format-Table -AutoSize
    }
    Write-Host 'After CDC firmware is installed and boots, specify its verified port with -Port COMn.'
    return
}
if ($available -notcontains $Port) {
    throw "Port $Port is not present. Run with -List and verify the FM-1 CDC port. No port opened."
}

if (-not $LogPath) {
    $repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
    $captureDir = Join-Path $repoRoot 'tmp/usb-serial'
    $LogPath = Join-Path $captureDir ('fm1-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '.log')
}
$LogPath = [System.IO.Path]::GetFullPath($LogPath)
$captureDir = [System.IO.Path]::GetDirectoryName($LogPath)
if (-not (Test-Path -LiteralPath $captureDir)) {
    $null = New-Item -ItemType Directory -Path $captureDir
}

$stream = $null
$writer = $null
$serial = $null
try {
    # Never overwrite an existing capture, including when -LogPath is supplied.
    $stream = [System.IO.File]::Open($LogPath, [System.IO.FileMode]::CreateNew,
        [System.IO.FileAccess]::Write, [System.IO.FileShare]::Read)
    $writer = [System.IO.StreamWriter]::new($stream, [System.Text.UTF8Encoding]::new($false))
    $writer.AutoFlush = $true
    $serial = [System.IO.Ports.SerialPort]::new($Port, $BaudRate,
        [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
    $serial.Handshake = [System.IO.Ports.Handshake]::None
    $serial.Encoding = [System.Text.Encoding]::ASCII
    $serial.ReadTimeout = 100
    $serial.WriteTimeout = 100
    # Opening a CDC port sends line-coding/control requests. No application
    # bytes are sent. Explicit DTR/RTS also satisfies the vendor CDC handshake.
    $serial.DtrEnable = $true
    $serial.RtsEnable = $true
    Write-Host "Opening $Port at $BaudRate 8N1 for $Seconds seconds. Verify this is the FM-1."
    Write-Host 'Receive only; DTR/RTS asserted. Ctrl+C stops and closes the port.'
    Write-Host "Capture: $LogPath"
    $serial.Open()
    $timer = [System.Diagnostics.Stopwatch]::StartNew()
    while ($timer.Elapsed.TotalSeconds -lt $Seconds) {
        $chunk = $serial.ReadExisting()
        if ($chunk.Length -gt 0) {
            $writer.Write($chunk)
            [Console]::Write($chunk)
        }
        Start-Sleep -Milliseconds 20
    }
} finally {
    if ($null -ne $serial) { $serial.Dispose() }
    if ($null -ne $writer) { $writer.Dispose() }
    elseif ($null -ne $stream) { $stream.Dispose() }
}

Write-Host "`nCapture finished: $LogPath"

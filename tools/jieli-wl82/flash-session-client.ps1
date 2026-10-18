[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$SessionRoot,
    [ValidateSet('status','environment','plan','flash','retry_flash','recover_flash','reset','observe','serial_status','enter_uboot','read_firmware','quit')][string]$Operation = 'status',
    [string]$RequestFile,
    [ValidateSet('cold','reuse')][string]$LoaderState
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ($Operation -eq 'read_firmware') {
    if (-not $LoaderState) { throw 'read_firmware requires explicit -LoaderState cold or reuse from session history' }
} elseif ($PSBoundParameters.ContainsKey('LoaderState')) { throw 'LoaderState is allowed only with read_firmware' }
$config = Get-Content -LiteralPath "$SessionRoot\session.json" -Raw | ConvertFrom-Json
if (Test-Path -LiteralPath "$SessionRoot\stopped.txt") { throw 'Session has stopped; do not retry a previous flash blindly.' }
if ($Operation -in @('plan','flash','retry_flash','recover_flash')) {
    if (-not $RequestFile) { throw 'A locally validated request file is required' }
    $request = Get-Content -LiteralPath $RequestFile -Raw | ConvertFrom-Json
    if ($request.op -ne 'plan') { throw 'Input must be an offline plan request' }
    $request.op = $Operation
} else {
    if ($RequestFile) { throw 'This operation takes no request file' }
    $request = @{op=$Operation}
    if ($Operation -eq 'read_firmware') { $request.loader_state = $LoaderState }
}
$payload = [Text.Encoding]::UTF8.GetBytes(($request | ConvertTo-Json -Compress))
if ($payload.Length -gt 1500000) { throw 'Request too large' }
if (-not ('FM1PipePeer' -as [type])) {
    Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class FM1PipePeer {
    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern bool GetNamedPipeServerProcessId(IntPtr pipe, out uint pid);
}
'@
}
$pipe = [IO.Pipes.NamedPipeClientStream]::new('.', $config.pipe,
    [IO.Pipes.PipeDirection]::InOut, [IO.Pipes.PipeOptions]::Asynchronous,
    [Security.Principal.TokenImpersonationLevel]::Identification)
function Receive-Exact([int]$count) {
    $bytes = [byte[]]::new($count)
    $offset = 0
    while ($offset -lt $count) {
        $task = $pipe.ReadAsync($bytes,$offset,$count-$offset)
        # Waiting for a full verified flash is allowed; caller can monitor the
        # protected state/logs. Timeout is not permission to retry.
        if (-not $task.Wait(1200000)) { throw 'Response timeout; inspect session state before any further action' }
        if ($task.Result -eq 0) { throw 'Server disconnected; inspect session state' }
        $offset += $task.Result
    }
    return ,$bytes
}
try {
    $pipe.Connect(5000)
    $serverPid = [uint32]0
    if (-not [FM1PipePeer]::GetNamedPipeServerProcessId($pipe.SafePipeHandle.DangerousGetHandle(), [ref]$serverPid) -or
        $serverPid -ne $config.server_pid) { throw 'Pipe server PID does not match protected session descriptor' }
    $header = [BitConverter]::GetBytes([uint32]$payload.Length)
    if (-not $pipe.WriteAsync($header,0,4).Wait(5000)) { throw 'Request header timeout' }
    if (-not $pipe.WriteAsync($payload,0,$payload.Length).Wait(15000)) { throw 'Request body timeout' }
    $length = [BitConverter]::ToUInt32((Receive-Exact 4),0)
    if ($length -eq 0 -or $length -gt 1500000) { throw 'Invalid response length' }
    $raw = [Text.Encoding]::UTF8.GetString((Receive-Exact $length))
    Write-Output $raw
    if (-not ($raw | ConvertFrom-Json).ok) { throw 'Broker refused/failed operation; see response and logs. No automatic retry.' }
} finally { $pipe.Dispose() }

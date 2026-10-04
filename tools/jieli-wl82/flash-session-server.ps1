# Loaded ONLY from the protected session snapshot, after one UAC approval.
param([Parameter(Mandatory=$true)][string]$SessionRoot)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$config = Get-Content -LiteralPath "$SessionRoot\session.json" -Raw | ConvertFrom-Json
$pythonFlags = @('-I','-S','-u')
if ($config.PSObject.Properties['trusted_local'] -and $config.trusted_local -eq $true) {
    $pythonFlags = @('-u')
}
$config | Add-Member -NotePropertyName server_pid -NotePropertyValue $PID -Force
$config | Add-Member -NotePropertyName idle_timeout_seconds -NotePropertyValue 0 -Force
$config | ConvertTo-Json | Set-Content -LiteralPath "$SessionRoot\session.json" -Encoding UTF8
$acl = [System.IO.Pipes.PipeSecurity]::new()
$acl.SetSecurityDescriptorSddlForm("D:P(D;;GA;;;NU)(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;$($config.owner_sid))")
$quit = $false

function Read-Exact($pipe, [int]$count, [int]$timeoutMs) {
    $bytes = [byte[]]::new($count)
    $offset = 0
    $watch = [Diagnostics.Stopwatch]::StartNew()
    while ($offset -lt $count) {
        $task = $pipe.ReadAsync($bytes, $offset, $count - $offset)
        $remaining = $timeoutMs - [int]$watch.ElapsedMilliseconds
        if ($remaining -le 0 -or -not $task.Wait($remaining)) { throw 'IPC read timeout' }
        $n = $task.Result
        if ($n -eq 0) { throw 'IPC disconnected' }
        $offset += $n
    }
    return ,$bytes
}

try {
    while (-not $quit) {
        $pipe = [System.IO.Pipes.NamedPipeServerStream]::new($config.pipe,
            [IO.Pipes.PipeDirection]::InOut, 1, [IO.Pipes.PipeTransmissionMode]::Byte,
            [IO.Pipes.PipeOptions]::Asynchronous, 4096, 4096, $acl)
        try {
            # User-requested lifetime: no idle expiration. Remain until explicit
            # quit or process/Windows termination. Individual IPC deadlines stay.
            $pipe.WaitForConnectionAsync().GetAwaiter().GetResult()
            $length = [BitConverter]::ToUInt32((Read-Exact $pipe 4 5000), 0)
            if ($length -eq 0 -or $length -gt 1500000) { throw 'Invalid IPC message length' }
            $raw = [Text.Encoding]::UTF8.GetString((Read-Exact $pipe $length 15000))
            $request = $raw | ConvertFrom-Json
            if ($request.op -eq 'quit' -and @($request.PSObject.Properties).Count -eq 1) {
                $answer = '{"ok":true,"data":{"stopped":true}}'
                $quit = $true
            } else {
                # Paths are generated here, never supplied by a client.
                $id = [Guid]::NewGuid().ToString('N')
                $inputFile = "$SessionRoot\ipc\$id.json"
                $outputFile = "$SessionRoot\ipc\$id.out"
                $errorFile = "$SessionRoot\ipc\$id.err"
                [IO.File]::WriteAllText($inputFile, $raw, [Text.UTF8Encoding]::new($false))
                $worker = "$SessionRoot\tools\jieli-wl82\flash_session_worker.py"
                # Native invocation inherits this process's elevated token.
                & 'C:\Python311\python.exe' @pythonFlags $worker $inputFile 1> $outputFile 2> $errorFile
                if ($LASTEXITCODE -ne 0) { throw "Worker exited $LASTEXITCODE; inspect $errorFile" }
                $answer = Get-Content -LiteralPath $outputFile | Select-Object -Last 1
                $null = $answer | ConvertFrom-Json
            }
            $payload = [Text.Encoding]::UTF8.GetBytes($answer)
            $header = [BitConverter]::GetBytes([uint32]$payload.Length)
            if (-not $pipe.WriteAsync($header,0,4).Wait(5000)) { throw 'IPC write timeout' }
            if (-not $pipe.WriteAsync($payload,0,$payload.Length).Wait(5000)) { throw 'IPC write timeout' }
        } catch {
            # An IPC error never authorizes retrying a device operation. Worker
            # state/receipt is authoritative, even if the client disappeared.
            Add-Content -LiteralPath "$SessionRoot\server-errors.log" -Value $_.ToString()
        } finally { $pipe.Dispose() }
    }
} finally {
    [IO.File]::WriteAllText("$SessionRoot\stopped.txt", [DateTime]::UtcNow.ToString('o'))
}

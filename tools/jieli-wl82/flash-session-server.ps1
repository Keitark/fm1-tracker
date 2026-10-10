# Loaded ONLY from the protected session snapshot, after one UAC approval.
param([Parameter(Mandatory=$true)][string]$SessionRoot)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$config = Get-Content -LiteralPath "$SessionRoot\session.json" -Raw | ConvertFrom-Json
if ($config.PSObject.Properties['freeze_python_runtime'] -and $config.freeze_python_runtime -isnot [bool]) { throw 'Invalid frozen runtime mode' }
$python = 'C:\Python311\python.exe'
$pythonFlags = @('-I','-S','-u')
if ($config.PSObject.Properties['freeze_python_runtime'] -and $config.freeze_python_runtime -eq $true) {
    if ($config.PSObject.Properties['trusted_local'] -and $config.trusted_local -eq $true) { throw 'Conflicting runtime modes' }
    . "$SessionRoot\tools\jieli-wl82\frozen-python-runtime.ps1"
    Assert-FM1ReadOnlyTree $SessionRoot
    $python = Join-Path $SessionRoot 'runtime\python.exe'
    $pythonFlags = @('-I','-S','-B','-u')
    $env:PATH = "$SessionRoot\runtime;C:\Windows\System32;C:\Windows"
    $env:OPENSSL_CONF = "$SessionRoot\runtime\openssl-empty.cnf"
    $env:OPENSSL_MODULES = "$SessionRoot\runtime\openssl-modules"
    $env:OPENSSL_ENGINES = "$SessionRoot\runtime\openssl-engines"
    [Environment]::SetEnvironmentVariable('OPENSSL_CONF_INCLUDE',$null,'Process')
    Set-Location -LiteralPath $SessionRoot
}
if ($config.PSObject.Properties['trusted_local'] -and $config.trusted_local -eq $true) {
    $pythonFlags = @('-u')
}
$config | Add-Member -NotePropertyName server_pid -NotePropertyValue $PID -Force
$config | Add-Member -NotePropertyName idle_timeout_seconds -NotePropertyValue 0 -Force
$config | ConvertTo-Json | Set-Content -LiteralPath "$SessionRoot\session.json" -Encoding UTF8
$acl = [System.IO.Pipes.PipeSecurity]::new()
$acl.SetSecurityDescriptorSddlForm("D:P(D;;GA;;;NU)(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;$($config.owner_sid))")
$quit = $false
$explicitQuit = $false

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
            # The launcher waits for a configured pipe, after frozen self-test.
            if (-not (Test-Path -LiteralPath "$SessionRoot\ready.json")) {
                $readyText = @{server_pid=$PID;ready=$true} | ConvertTo-Json
                if ($config.PSObject.Properties['freeze_python_runtime'] -and $config.freeze_python_runtime -eq $true) {
                $readyAcl = [Security.AccessControl.FileSecurity]::new()
                $readyAcl.SetSecurityDescriptorSddlForm("O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;GRGX;;;$($config.owner_sid))")
                $readyFile = [IO.FileStream]::new("$SessionRoot\ready.json",[IO.FileMode]::CreateNew,[Security.AccessControl.FileSystemRights]::Write,
                    [IO.FileShare]::None,4096,[IO.FileOptions]::None,$readyAcl)
                try {
                    $readyBytes = [Text.UTF8Encoding]::new($false).GetBytes($readyText)
                    $readyFile.Write($readyBytes,0,$readyBytes.Length)
                } finally { $readyFile.Dispose() }
                } else {
                    [IO.File]::WriteAllText("$SessionRoot\ready.json",$readyText,[Text.UTF8Encoding]::new($false))
                }
            }
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
                $explicitQuit = $true
            } else {
                # Paths are generated here, never supplied by a client.
                $id = [Guid]::NewGuid().ToString('N')
                $inputFile = "$SessionRoot\ipc\$id.json"
                $outputFile = "$SessionRoot\ipc\$id.out"
                $errorFile = "$SessionRoot\ipc\$id.err"
                [IO.File]::WriteAllText($inputFile, $raw, [Text.UTF8Encoding]::new($false))
                $worker = "$SessionRoot\tools\jieli-wl82\flash_session_worker.py"
                # Native invocation inherits this process's elevated token.
                & $python @pythonFlags $worker $inputFile 1> $outputFile 2> $errorFile
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
    $stoppedAt = [DateTime]::UtcNow.ToString('o')
    $stopFiles = @{
        'stopped.txt' = $stoppedAt
        'stopped.json' = (@{root=$SessionRoot;pipe=$config.pipe;server_pid=$PID;
                            explicit_quit=$explicitQuit;stopped_at=$stoppedAt} | ConvertTo-Json)
    }
    foreach ($name in $stopFiles.Keys) {
        if ($config.PSObject.Properties['freeze_python_runtime'] -and $config.freeze_python_runtime -eq $true) {
            $stopAcl = [Security.AccessControl.FileSecurity]::new()
            $stopAcl.SetSecurityDescriptorSddlForm("O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;GRGX;;;$($config.owner_sid))")
            $stopFile = [IO.FileStream]::new("$SessionRoot\$name",[IO.FileMode]::CreateNew,[Security.AccessControl.FileSystemRights]::Write,
                [IO.FileShare]::None,4096,[IO.FileOptions]::None,$stopAcl)
            try {
                $stopBytes = [Text.UTF8Encoding]::new($false).GetBytes($stopFiles[$name])
                $stopFile.Write($stopBytes,0,$stopBytes.Length)
            } finally { $stopFile.Dispose() }
        } else {
            [IO.File]::WriteAllText("$SessionRoot\$name",$stopFiles[$name],[Text.UTF8Encoding]::new($false))
        }
    }
}

# Freeze existing tools, then elevate ONCE. No installation, service or new venv.
[CmdletBinding()]
param(
    [string]$Baseline = 'F:\dev\fm1\private-backups\nes-keyspi-dma2-enable-write-20260929-080037\final-2.bin',
    [ValidatePattern('^[0-9a-f]{64}$')][string]$BaselineSha256 = 'c60b423b0e640c96ae939a138811a880eab1f674fe2e8c78ab585c0bfc254532',
    [switch]$PrepareOnly,
    [switch]$TrustedLocal,
    [string]$ResumeFailedSession
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
function Assert-FM1PythonRuntime {
    # Check files AND directory owners/ACLs; a writable child or user-owned
    # object can defeat protection on the installation root. -S excludes site.
    $paths = [Collections.Generic.List[string]]::new()
    $paths.Add('C:\Python311')
    foreach ($file in Get-ChildItem -LiteralPath 'C:\Python311' -File) {
        if ($file.Extension -in @('.exe','.dll','._pth','.zip')) { $paths.Add($file.FullName) }
    }
    $todo = [Collections.Generic.Stack[string]]::new()
    $todo.Push('C:\Python311\Lib'); $todo.Push('C:\Python311\DLLs')
    # Check roots first, so an obvious failure needs no expensive recursive walk.
    foreach ($path in @('C:\Python311','C:\Python311\python.exe','C:\Python311\Lib','C:\Python311\DLLs')) {
        $acl = Get-Acl -LiteralPath $path
        foreach ($rule in $acl.Access) {
            $sid = $rule.IdentityReference.Translate([Security.Principal.SecurityIdentifier]).Value
            if ($rule.AccessControlType -eq 'Allow' -and $sid -notin @('S-1-5-18','S-1-5-32-544') -and
                ([int]$rule.FileSystemRights -band 0xD0116)) { throw "Existing Python is writable outside administrators: $path ($sid). No permissions were changed; no other Python will be installed." }
        }
    }
    while ($todo.Count) {
        $folder = $todo.Pop(); $paths.Add($folder)
        if ((Get-Item -LiteralPath $folder).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Python reparse path: $folder" }
        foreach ($item in Get-ChildItem -LiteralPath $folder -Force) {
            if ($item.FullName -eq 'C:\Python311\Lib\site-packages') { continue }
            if ($item.PSIsContainer) { $todo.Push($item.FullName) } else { $paths.Add($item.FullName) }
        }
    }
    foreach ($path in $paths) {
        if ((Get-Item -LiteralPath $path).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Python reparse path: $path" }
        $acl = Get-Acl -LiteralPath $path
        $owner = $acl.GetOwner([Security.Principal.SecurityIdentifier]).Value
        if ($owner -notin @('S-1-5-18','S-1-5-32-544')) { throw "Python object has a non-administrator owner: $path" }
        foreach ($rule in $acl.Access) {
            $sid = $rule.IdentityReference.Translate([Security.Principal.SecurityIdentifier]).Value
            if ($rule.AccessControlType -eq 'Allow' -and $sid -notin @('S-1-5-18','S-1-5-32-544') -and
                ([int]$rule.FileSystemRights -band 0xD0116)) { throw "Python object writable outside administrators: $path ($sid)" }
        }
    }
}
$repo = (Resolve-Path -LiteralPath "$PSScriptRoot\..\..").Path
$recovery = $null
if ($ResumeFailedSession) {
    if ($PSBoundParameters.ContainsKey('Baseline') -or $PSBoundParameters.ContainsKey('BaselineSha256')) {
        throw 'ResumeFailedSession obtains its baseline from failure evidence; do not override it'
    }
    $ResumeFailedSession = (Resolve-Path -LiteralPath $ResumeFailedSession).Path
    if ($ResumeFailedSession -notmatch '^C:\\Program Files\\FM1FlashSession-[0-9a-f]{32}$') { throw 'Recovery source must be a protected FM-1 session' }
    $recoveryJson = & 'C:\Python311\python.exe' "$PSScriptRoot\flash_session_worker.py" _resume_plan $ResumeFailedSession
    if ($LASTEXITCODE -ne 0) { throw 'Failed-session evidence validation failed; no elevation or device I/O' }
    $recovery = $recoveryJson | ConvertFrom-Json
    $Baseline = $recovery.baseline
    $BaselineSha256 = $recovery.baseline_sha256
}
$Baseline = (Resolve-Path -LiteralPath $Baseline).Path
if ((Get-Item -LiteralPath $Baseline).Length -ne 1048576 -or
    (Get-FileHash -LiteralPath $Baseline -Algorithm SHA256).Hash.ToLower() -ne $BaselineSha256) { throw 'Baseline hash/size mismatch' }
$baselineReceipt = Join-Path (Split-Path $Baseline) 'deployment.json'
if ((Split-Path $Baseline -Leaf) -eq 'baseline.bin') { $baselineReceipt = Join-Path (Split-Path $Baseline) 'baseline-deployment.json' }
$receipt = Get-Content -LiteralPath $baselineReceipt -Raw | ConvertFrom-Json
if ($receipt.status -notin @('written_and_twice_readback_verified','written_and_readback_verified') -or
    $receipt.full_readback_sha256 -ne $BaselineSha256) { throw 'Baseline requires a successful full-readback deployment receipt' }
if ($receipt.status -eq 'written_and_readback_verified' -and $receipt.full_readback_count -ne 1) {
    throw 'Invalid single-readback receipt'
}
$id = [Guid]::NewGuid().ToString('N')
$sessionRoot = "C:\Program Files\FM1FlashSession-$id"
$ownerSid = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value
$files = [Collections.Generic.List[object]]::new()
function Add-SnapshotFile([string]$source,[string]$relative) {
    $item = Get-Item -LiteralPath $source
    if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Reparse input: $source" }
    $files.Add(@{source=$item.FullName; relative=$relative; sha256=(Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLower()})
}
foreach ($name in @('flash-session-server.ps1','flash_session_worker.py','deploy_v14_smb1_via_cli.py')) {
    Add-SnapshotFile "$PSScriptRoot\$name" "tools\jieli-wl82\$name"
}
$cliRoot = "$repo\tools\jl-uboot-tool"
foreach ($item in Get-ChildItem -LiteralPath $cliRoot -Recurse -File) {
    $relative = $item.FullName.Substring($cliRoot.Length+1)
    if (($relative -notmatch '(^|\\)(\.git|__pycache__)(\\|$)') -and
        ($item.Extension -in @('.py','.yaml') -or $relative -eq 'data\loaderblobs\usb\wl82loader.bin')) {
        Add-SnapshotFile $item.FullName "tools\jl-uboot-tool\$relative"
    }
}
# Resolve existing dependencies before UAC. Trusted-local uses them in place;
# hardened mode copies them into the immutable session snapshot. No installs.
$deps = & 'C:\Python311\python.exe' -c "import json,pathlib,yaml,tqdm,crcmod,colorama,serial; print(json.dumps([str(pathlib.Path(m.__file__).parent) for m in (yaml,tqdm,crcmod,colorama,serial)]))"
if ($LASTEXITCODE -ne 0) { throw 'Existing Python dependencies unavailable' }
if (-not $TrustedLocal) { foreach ($folder in ($deps | ConvertFrom-Json)) {
    $name = Split-Path $folder -Leaf
    foreach ($item in Get-ChildItem -LiteralPath $folder -Recurse -File) {
        if ($item.Extension -in @('.py','.pyd') -and $item.FullName -notmatch '\\__pycache__\\') {
            Add-SnapshotFile $item.FullName ("lib\$name\" + $item.FullName.Substring($folder.Length+1))
        }
    }
} }
Add-SnapshotFile $Baseline 'baseline.bin'
Add-SnapshotFile $baselineReceipt 'baseline-deployment.json'
$initialState = @{baseline_file='baseline.bin';baseline_sha256=$BaselineSha256;blocked=$false;reset_pending=$false;operation='idle'}
if ($recovery) {
    foreach ($file in $recovery.files) { Add-SnapshotFile $file.source $file.relative }
    $initialState = $recovery.initial_state
}
$pythonEnvironment = @{}
foreach ($name in @('PATH','PYTHONPATH','PYTHONHOME','PYTHONUSERBASE','PYTHONNOUSERSITE','PYTHONSAFEPATH','PYTHONUTF8','PYTHONIOENCODING')) {
    $pythonEnvironment[$name] = [Environment]::GetEnvironmentVariable($name,'Process')
}
$plan = @{root=$sessionRoot; owner_sid=$ownerSid; pipe="fm1-flash-$id"; files=$files;
          baseline_sha256=$BaselineSha256; initial_state=$initialState; trusted_local=[bool]$TrustedLocal; python_environment=$pythonEnvironment}
$localDir = Join-Path $repo 'tmp\flash-sessions'
$null = New-Item -ItemType Directory -Path $localDir -Force
$planPath = Join-Path $localDir "$id.json"
$plan | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $planPath -Encoding UTF8
if ($PrepareOnly) { Write-Output "Prepared: $planPath (no elevation or device I/O)"; return }
# Diagnose this BEFORE asking for UAC. Recheck inside the elevated bootstrap
# as well; no global ACL changes or replacement Python environments here.
if ($TrustedLocal) {
    Write-Warning 'Trusted local mode: existing Python and user packages will run elevated. No runtime ACL changes; fixed flash safeguards remain enabled.'
} else { Assert-FM1PythonRuntime }
$planHash = (Get-FileHash -LiteralPath $planPath -Algorithm SHA256).Hash.ToLower()
$planPath64 = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($planPath))
# This entire bootstrap is frozen into the UAC-launched command line. It does
# not execute workspace code or use shell input from IPC. The manifest is read
# once as data and must match the hash frozen into this command line.
$bootstrap = @'
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$planPath = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('__PLANPATH__'))
$env:PSModulePath = 'C:\Windows\System32\WindowsPowerShell\v1.0\Modules'
$env:PATH = 'C:\Windows\System32;C:\Windows;C:\Python311'
$planBytes = [IO.File]::ReadAllBytes($planPath)
$hash = [Security.Cryptography.SHA256]::Create()
try { $digest = ([BitConverter]::ToString($hash.ComputeHash($planBytes))).Replace('-','').ToLower() } finally { $hash.Dispose() }
if ($digest -ne '__PLANHASH__') { throw 'Launch manifest changed; aborting' }
$plan = [Text.Encoding]::UTF8.GetString($planBytes).TrimStart([char]0xfeff) | ConvertFrom-Json
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
if (-not ([Security.Principal.WindowsPrincipal]::new($identity)).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Elevation required' }
if ($identity.User.Value -ne $plan.owner_sid) { throw 'Use the same Windows account for elevation' }
if ($plan.root -notmatch '^C:\\Program Files\\FM1FlashSession-[0-9a-f]{32}$' -or (Test-Path -LiteralPath $plan.root)) { throw 'Invalid/existing session root' }
# Same complete runtime trust audit, again after elevation.
function Assert-FM1PythonRuntime { __RUNTIMECHECK__ }
if (-not $plan.trusted_local) { Assert-FM1PythonRuntime }
if ($plan.trusted_local) {
    foreach ($property in $plan.python_environment.PSObject.Properties) {
        [Environment]::SetEnvironmentVariable($property.Name, $property.Value, 'Process')
    }
}
$null = New-Item -ItemType Directory -Path $plan.root
$acl = [Security.AccessControl.DirectorySecurity]::new()
$acl.SetSecurityDescriptorSddlForm("O:BAG:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;GRGX;;;$($plan.owner_sid))")
Set-Acl -LiteralPath $plan.root -AclObject $acl
foreach ($file in $plan.files) {
    if ($file.relative -match '(^[\\/]|:|(^|[\\/])\.\.([\\/]|$))') { throw 'Snapshot path rejected' }
    $dest = Join-Path $plan.root $file.relative
    $parent = Split-Path $dest
    if (-not (Test-Path -LiteralPath $parent)) { $null = New-Item -ItemType Directory -Path $parent -Force }
    Copy-Item -LiteralPath $file.source -Destination $dest -ErrorAction Stop
    if ((Get-FileHash -LiteralPath $dest -Algorithm SHA256).Hash.ToLower() -ne $file.sha256) { throw 'Snapshot changed during elevation; aborting' }
}
$null = New-Item -ItemType Directory -Path "$($plan.root)\ipc","$($plan.root)\runs"
@{pipe=$plan.pipe;owner_sid=$plan.owner_sid;root=$plan.root;server_pid=$PID;trusted_local=$plan.trusted_local} | ConvertTo-Json | Set-Content -LiteralPath "$($plan.root)\session.json" -Encoding UTF8
$plan.initial_state | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath "$($plan.root)\state.json" -Encoding UTF8
# Retain exact source hashes with the snapshot for auditability.
$plan | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath "$($plan.root)\snapshot.json" -Encoding UTF8
Set-Location -LiteralPath $plan.root
& "$($plan.root)\tools\jieli-wl82\flash-session-server.ps1" -SessionRoot $plan.root
'@
$bootstrap = $bootstrap.Replace('__PLANPATH__', $planPath64).Replace('__PLANHASH__', $planHash)
$bootstrap = $bootstrap.Replace('__RUNTIMECHECK__', ${function:Assert-FM1PythonRuntime}.ToString())
# Compress the frozen bootstrap to stay below Windows' command-line limit.
$rawBytes = [Text.Encoding]::UTF8.GetBytes($bootstrap)
$memory = [IO.MemoryStream]::new()
$gzip = [IO.Compression.GZipStream]::new($memory,[IO.Compression.CompressionMode]::Compress,$true)
$gzip.Write($rawBytes,0,$rawBytes.Length); $gzip.Dispose()
$packed = [Convert]::ToBase64String($memory.ToArray()); $memory.Dispose()
$unpack = '$m=[IO.MemoryStream]::new([Convert]::FromBase64String(''' + $packed + '''));$g=[IO.Compression.GZipStream]::new($m,[IO.Compression.CompressionMode]::Decompress);$r=[IO.StreamReader]::new($g); & ([scriptblock]::Create($r.ReadToEnd()))'
$encoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($unpack))
if ($encoded.Length -gt 30000) { throw 'Frozen bootstrap exceeds Windows command-line limit; no process started' }
$process = Start-Process 'C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe' -ArgumentList '-NoProfile','-NonInteractive','-EncodedCommand',$encoded -Verb RunAs -WindowStyle Hidden -PassThru -ErrorAction Stop
Write-Output "SessionRoot: $sessionRoot"
Write-Output "Elevated PID: $($process.Id)"
$ready = $false
for ($attempt=0; $attempt -lt 100; $attempt++) {
    if (Test-Path -LiteralPath "$sessionRoot\session.json") { $ready=$true; break }
    if ($process.HasExited) { throw "Elevated startup exited ($($process.ExitCode)) before readiness. No device operation was requested." }
    Start-Sleep -Milliseconds 100
}
if (-not $ready) { throw "Startup not confirmed. Inspect PID $($process.Id); do not launch a duplicate session." }
& "$PSScriptRoot\flash-session-client.ps1" -SessionRoot $sessionRoot -Operation status
Write-Output 'Status exchange passed. No flash/reset requested.'

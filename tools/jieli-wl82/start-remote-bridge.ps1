# Laptop-local preparation and unelevated launch. No device I/O or UAC here.
[CmdletBinding()]
param(
    [string]$RepoRoot,
    [string]$Python = 'C:\Python311\python.exe',
    [string]$StateRoot,
    [string]$SessionRoot,
    [ValidateRange(1,65535)][int]$Port = 9770,
    [switch]$PublishTailscale,
    [switch]$PrepareOnly,
    [string]$OfficialUpdater,
    [ValidatePattern('^[0-9a-f]{64}$')][string]$OfficialUpdaterSha256
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ($env:OS -ne 'Windows_NT') { throw 'This launcher requires Windows.' }
$launchIdentity = [Security.Principal.WindowsIdentity]::GetCurrent()
$launchPrincipal = [Security.Principal.WindowsPrincipal]::new($launchIdentity)
if (-not $PrepareOnly -and $launchPrincipal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run the bridge launcher in normal, non-administrator PowerShell. Only the separately started protected flashing helper is elevated.'
}
if (-not $RepoRoot) { $RepoRoot = Join-Path $PSScriptRoot '..\..' }
if (-not $StateRoot) { $StateRoot = Join-Path $env:LOCALAPPDATA 'FM1RemoteBridge' }
$repo = (Resolve-Path -LiteralPath $RepoRoot).Path
$runtime = (Resolve-Path -LiteralPath $Python).Path
$state = [IO.Path]::GetFullPath($StateRoot).TrimEnd('\')
$bridge = Join-Path $repo 'tools\jieli-wl82\remote_bridge.py'
if (-not (Test-Path -LiteralPath $bridge -PathType Leaf)) { throw 'RepoRoot lacks remote_bridge.py.' }
if ($state.StartsWith('\\') -or $state -eq [IO.Path]::GetPathRoot($state).TrimEnd('\') -or
    $state -in @($repo.TrimEnd('\'), $env:USERPROFILE.TrimEnd('\'), $env:LOCALAPPDATA.TrimEnd('\')) -or
    $state.StartsWith($env:WINDIR + '\', [StringComparison]::OrdinalIgnoreCase) -or
    $state -eq $env:WINDIR) { throw 'StateRoot must be a dedicated local directory, not a system/user/repository root.' }

function Assert-NoReparse([string]$Path) {
    $candidate = [IO.Path]::GetFullPath($Path)
    while ($candidate) {
        if (Test-Path -LiteralPath $candidate) {
            if ((Get-Item -LiteralPath $candidate -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "Reparse path is not allowed: $candidate"
            }
        }
        $parent = Split-Path -Path $candidate -Parent
        if ($parent -eq $candidate) { break }
        $candidate = $parent
    }
}
if (-not ('FM1RemoteDacl' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
public static class FM1RemoteDacl {
    [DllImport("advapi32.dll", SetLastError=true)]
    static extern bool GetSecurityDescriptorDacl(IntPtr descriptor, out bool present, out IntPtr dacl, out bool defaulted);
    [DllImport("advapi32.dll", CharSet=CharSet.Unicode, EntryPoint="SetNamedSecurityInfoW")]
    static extern uint SetNamedSecurityInfo(string path, uint objectType, uint sections, IntPtr owner, IntPtr group, IntPtr dacl, IntPtr sacl);
    public static void Apply(string path, byte[] descriptor) {
        GCHandle pin = GCHandle.Alloc(descriptor, GCHandleType.Pinned);
        try {
            bool present, defaulted; IntPtr dacl;
            if (!GetSecurityDescriptorDacl(pin.AddrOfPinnedObject(), out present, out dacl, out defaulted) || !present)
                throw new Win32Exception(Marshal.GetLastWin32Error());
            // DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION.
            // Do not request owner, group, or SACL privileges.
            uint error = SetNamedSecurityInfo(path, 1, 0x80000004, IntPtr.Zero, IntPtr.Zero, dacl, IntPtr.Zero);
            if (error != 0) throw new Win32Exception((int)error);
        } finally { pin.Free(); }
    }
}
'@
}
function Protect-State([string]$Path) {
    Assert-NoReparse $Path
    $null = New-Item -ItemType Directory -Path $Path -Force
    $currentSid = [Security.Principal.WindowsIdentity]::GetCurrent().User
    $principals = @($currentSid, [Security.Principal.SecurityIdentifier]::new('S-1-5-18'),
                    [Security.Principal.SecurityIdentifier]::new('S-1-5-32-544'))
    $todo = [Collections.Generic.Stack[IO.FileSystemInfo]]::new()
    $todo.Push((Get-Item -LiteralPath $Path -Force))
    while ($todo.Count) {
        $item = $todo.Pop()
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Reparse state entry: $($item.FullName)" }
        # Modify the existing DACL only. Constructing a new FileSecurity object
        # can make Set-Acl try to replace its SACL and demand SeSecurityPrivilege.
        $acl = Get-Acl -LiteralPath $item.FullName
        $ownerSid = $acl.GetOwner([Security.Principal.SecurityIdentifier]).Value
        if ($ownerSid -notin @($currentSid.Value,'S-1-5-18','S-1-5-32-544')) {
            throw "State entry has an owner outside the permitted accounts: $($item.FullName)"
        }
        if ($item.PSIsContainer) {
            $inherit = [Security.AccessControl.InheritanceFlags]'ContainerInherit,ObjectInherit'
        } else {
            $inherit = [Security.AccessControl.InheritanceFlags]::None
        }
        $acl.SetAccessRuleProtection($true, $false)
        foreach ($rule in @($acl.Access)) { $acl.RemoveAccessRuleAll($rule) }
        foreach ($sid in $principals) {
            $acl.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new(
                $sid, [Security.AccessControl.FileSystemRights]::FullControl, $inherit,
                [Security.AccessControl.PropagationFlags]::None, [Security.AccessControl.AccessControlType]::Allow))
        }
        [FM1RemoteDacl]::Apply($item.FullName, $acl.GetSecurityDescriptorBinaryForm())
        if ($item.PSIsContainer) {
            foreach ($child in Get-ChildItem -LiteralPath $item.FullName -Force) { $todo.Push($child) }
        }
    }
}
Protect-State $state
$tokenFile = Join-Path $state 'token.txt'
if (-not (Test-Path -LiteralPath $tokenFile)) {
    $random = [byte[]]::new(48)
    $rng = [Security.Cryptography.RandomNumberGenerator]::Create()
    try { $rng.GetBytes($random) } finally { $rng.Dispose() }
    [IO.File]::WriteAllText($tokenFile, [Convert]::ToBase64String($random), [Text.UTF8Encoding]::new($false))
}
$token = [IO.File]::ReadAllText($tokenFile).Trim()
if ($token.Length -lt 32 -or $token -notmatch '^[\x21-\x7e]+$') { throw 'Existing token file is invalid; no token was replaced.' }
# A protected flashing snapshot fixes C:\Python311\python.exe independently
# of the unelevated bridge runtime; this script never changes that installation.
& $runtime -c 'import sys,yaml,tqdm,crcmod,colorama,serial,Cryptodome; assert sys.version_info >= (3,11)'
if ($LASTEXITCODE -ne 0) { throw 'Python 3.11+ dependencies unavailable; see requirements-remote.txt. Nothing was installed.' }
$configFile = Join-Path $state 'launch.json'
$oldConfig = $null
if (Test-Path -LiteralPath $configFile) { $oldConfig = Get-Content -LiteralPath $configFile -Raw | ConvertFrom-Json }
if (-not $PSBoundParameters.ContainsKey('SessionRoot') -and $oldConfig -and $oldConfig.session) {
    $SessionRoot = $oldConfig.session
}
if ($SessionRoot) {
    $SessionRoot = (Resolve-Path -LiteralPath $SessionRoot).Path
    Assert-NoReparse $SessionRoot
    foreach ($name in @('session.json','state.json')) {
        if (-not (Test-Path -LiteralPath (Join-Path $SessionRoot $name) -PathType Leaf)) { throw "SessionRoot lacks $name" }
    }
    $descriptor = Get-Content -LiteralPath (Join-Path $SessionRoot 'session.json') -Raw | ConvertFrom-Json
    if ([IO.Path]::GetFullPath($descriptor.root).TrimEnd('\') -ne $SessionRoot.TrimEnd('\')) { throw 'Session descriptor root mismatch.' }
    $worker = Join-Path $SessionRoot 'tools\jieli-wl82\flash_session_worker.py'
    if (-not (Test-Path -LiteralPath $worker) -or (Get-Content -LiteralPath $worker -Raw) -notmatch 'read_firmware') {
        throw 'Protected snapshot predates remote read support; start a fresh laptop session locally.'
    }
}
if ([bool]$OfficialUpdater -ne [bool]$OfficialUpdaterSha256) { throw 'Provide both OfficialUpdater and OfficialUpdaterSha256.' }
if ($OfficialUpdater) {
    $OfficialUpdater = (Resolve-Path -LiteralPath $OfficialUpdater).Path
    Assert-NoReparse $OfficialUpdater
    if ((Get-FileHash -LiteralPath $OfficialUpdater -Algorithm SHA256).Hash.ToLowerInvariant() -ne $OfficialUpdaterSha256) {
        throw 'Official updater hash mismatch; no executable was started.'
    }
}

function Get-BridgeHealth {
    try {
        $answer = Invoke-RestMethod -Uri "http://127.0.0.1:$Port/v1/status" -Headers @{Authorization=('Bearer ' + $token)} -TimeoutSec 20 -MaximumRedirection 0
        if ($answer.ok -eq $true) { return $answer.data }
    } catch { }
    return $null
}
function Publish-Bridge {
    $tailscale = (Get-Command tailscale.exe -ErrorAction Stop).Source
    $existingText = & $tailscale serve status --json 2>$null
    if ($LASTEXITCODE -ne 0) { throw 'Could not inspect existing Tailscale Serve configuration; no Serve changes made.' }
    $existing = ($existingText -join "`n") | ConvertFrom-Json
    if ($null -eq $existing) { $existing = [pscustomobject]@{} }
    $target = "http://127.0.0.1:$Port"
    $properties = @($existing.PSObject.Properties)
    $webProperty = $existing.PSObject.Properties['Web']
    function Test-MeaningfulConfig($Value) {
        if ($null -eq $Value) { return $false }
        if ($Value -is [pscustomobject]) {
            foreach ($property in $Value.PSObject.Properties) { if (Test-MeaningfulConfig $property.Value) { return $true } }
            return $false
        }
        if ($Value -is [Collections.IDictionary]) {
            foreach ($child in $Value.Values) { if (Test-MeaningfulConfig $child) { return $true } }
            return $false
        }
        if ($Value -is [array]) { return $Value.Count -gt 0 }
        return [bool]$Value
    }
    $hasAny = Test-MeaningfulConfig $existing
    if ($hasAny) {
        $same = $false
        if ($webProperty) {
            foreach ($endpoint in $webProperty.Value.PSObject.Properties) {
                if ($endpoint.Name -like '*:443') {
                    $handlers = $endpoint.Value.PSObject.Properties['Handlers']
                    if ($handlers) {
                        $rootRoute = $handlers.Value.PSObject.Properties['/']
                        if ($rootRoute -and $rootRoute.Value.PSObject.Properties['Proxy'] -and $rootRoute.Value.Proxy -eq $target) { $same = $true }
                    }
                }
            }
        }
        if (-not $same) { throw 'Tailscale Serve already has another configuration. Review it locally; this launcher never resets or overwrites it.' }
        $funnel = $existing.PSObject.Properties['AllowFunnel']
        if ($funnel -and (Test-MeaningfulConfig $funnel.Value)) { throw 'Existing Serve route permits Funnel. Review it locally before exposing the device bridge; no configuration was changed.' }
        Write-Output 'Existing HTTPS Serve route already points to this bridge; preserved.'
        return
    }
    & $tailscale serve --bg --https=443 $target
    if ($LASTEXITCODE -ne 0) { throw 'Tailscale publication failed; local bridge remains running. Resolve tailnet HTTPS/Serve permissions locally.' }
}
if ($PrepareOnly) {
    Write-Output "Prepared private bridge state: $state"
    Write-Output "Token file: $tokenFile (contents intentionally hidden)"
    Write-Output 'Dependencies checked. No listener, elevation, Tailscale change, or device operation requested.'
    return
}
$pidFile = Join-Path $state 'bridge.pid'
$running = $null
if (Test-Path -LiteralPath $pidFile) {
    $savedPidText = [IO.File]::ReadAllText($pidFile).Trim()
    if ($savedPidText -match '^[0-9]+$') { $running = Get-Process -Id ([int]$savedPidText) -ErrorAction SilentlyContinue }
}
if ($running) {
    $savedProcess = Get-CimInstance Win32_Process -Filter "ProcessId=$($running.Id)"
    if (-not $savedProcess -or $savedProcess.ExecutablePath -ne $runtime -or
        -not $savedProcess.CommandLine -or -not $savedProcess.CommandLine.Contains($bridge) -or -not $savedProcess.CommandLine.Contains($state)) {
        throw 'Recorded PID is alive but this bridge could not be verified. No process was stopped or duplicate launched.'
    }
    $health = Get-BridgeHealth
    if (-not $health) { throw 'Recorded bridge PID is alive but authenticated health is unavailable. No duplicate was launched.' }
    if ($oldConfig -and ($oldConfig.session -ne $SessionRoot -or $oldConfig.port -ne $Port -or
        $oldConfig.official_updater -ne $OfficialUpdater)) { throw 'Running bridge uses different launch settings; inspect it locally before restarting.' }
    Write-Output "Bridge already running, PID $($running.Id), loopback port $Port."
    if ($PublishTailscale) { Publish-Bridge }
    return
}
$probe = [Net.Sockets.TcpClient]::new()
try {
    $connected = $probe.ConnectAsync('127.0.0.1', $Port).Wait(1000)
    if ($connected -and $probe.Connected) { throw "Loopback port $Port is already in use. No existing listener was stopped." }
} catch [AggregateException] {
    # Connection refused means the port is currently available; bind remains
    # authoritative if another process starts between this check and launch.
} finally { $probe.Dispose() }
function Quote-Argument([string]$Argument) {
    if ($Argument.Contains('"') -or $Argument.EndsWith('\')) { throw 'Unsupported quote/trailing slash in a launch argument.' }
    return '"' + $Argument + '"'
}
$arguments = @('-u', $bridge, '--repo', $repo, '--state-dir', $state, '--token-file', $tokenFile, '--port', [string]$Port)
if ($SessionRoot) { $arguments += @('--session', $SessionRoot) }
if ($OfficialUpdater) { $arguments += @('--official-updater', $OfficialUpdater, '--official-updater-sha256', $OfficialUpdaterSha256) }
$tag = [Guid]::NewGuid().ToString('N')
$stdout = Join-Path $state "bridge-$tag.out.log"
$stderr = Join-Path $state "bridge-$tag.err.log"
$process = Start-Process -FilePath $runtime -ArgumentList (($arguments | ForEach-Object { Quote-Argument $_ }) -join ' ') -WorkingDirectory $repo -WindowStyle Hidden -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
[IO.File]::WriteAllText($pidFile, [string]$process.Id, [Text.UTF8Encoding]::new($false))
@{repo=$repo;session=$SessionRoot;port=$Port;python=$runtime;official_updater=$OfficialUpdater;official_updater_sha256=$OfficialUpdaterSha256;pid=$process.Id;stdout=$stdout;stderr=$stderr} |
    ConvertTo-Json | Set-Content -LiteralPath $configFile -Encoding UTF8
$ready = $false
for ($attempt=0; $attempt -lt 30; $attempt++) {
    $process.Refresh()
    if ($process.HasExited) { throw "Bridge exited before readiness. Inspect $stderr; no existing process was stopped." }
    if (Get-BridgeHealth) { $ready = $true; break }
    Start-Sleep -Milliseconds 200
}
if (-not $ready) { throw "Bridge readiness unconfirmed, PID $($process.Id). Inspect local logs; do not launch a duplicate." }
Write-Output "Bridge running, PID $($process.Id): http://127.0.0.1:$Port/"
Write-Output "Private state and token file: $state (token contents intentionally hidden)"
if ($PublishTailscale) { Publish-Bridge }
Write-Output 'Bridge stays running after this shell closes. No firmware/serial operation was requested.'

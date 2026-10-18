# Export a private, immutable laptop kit. No credentials, jobs, or device I/O.
[CmdletBinding()]
param(
    [string]$RepoRoot,
    [string]$OutputRoot,
    [string]$Python = 'C:\Python311\python.exe',
    [switch]$IncludeUsbTools,
    [string]$SessionRoot,
    [string]$CatalogDir
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ($env:OS -ne 'Windows_NT') { throw 'This packager requires Windows.' }
if (-not $RepoRoot) { $RepoRoot = Join-Path $PSScriptRoot '..\..' }
$repo = (Resolve-Path -LiteralPath $RepoRoot).Path
if (-not $OutputRoot) { $OutputRoot = Join-Path $repo 'tmp\remote-kit' }
$output = [IO.Path]::GetFullPath($OutputRoot)
function Assert-NoReparse([string]$Path) {
    $candidate = [IO.Path]::GetFullPath($Path)
    while ($candidate) {
        if ((Test-Path -LiteralPath $candidate) -and
            ((Get-Item -LiteralPath $candidate -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw "Reparse path is not allowed: $candidate"
        }
        $parent = Split-Path -Path $candidate -Parent
        if ($parent -eq $candidate) { break }
        $candidate = $parent
    }
}
Assert-NoReparse $repo
Assert-NoReparse $output
$version = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N')
$privateRoot = Join-Path $output $version
$null = New-Item -ItemType Directory -Path $privateRoot
$sid = [Security.Principal.WindowsIdentity]::GetCurrent().User
$acl = Get-Acl -LiteralPath $privateRoot
$acl.SetAccessRuleProtection($true,$false)
foreach ($rule in @($acl.Access)) { $acl.RemoveAccessRuleAll($rule) }
foreach ($principal in @($sid,[Security.Principal.SecurityIdentifier]::new('S-1-5-18'),[Security.Principal.SecurityIdentifier]::new('S-1-5-32-544'))) {
    $acl.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new($principal,
        [Security.AccessControl.FileSystemRights]::FullControl,
        [Security.AccessControl.InheritanceFlags]'ContainerInherit,ObjectInherit',
        [Security.AccessControl.PropagationFlags]::None,[Security.AccessControl.AccessControlType]::Allow))
}
Set-Acl -LiteralPath $privateRoot -AclObject $acl
$kit = Join-Path $privateRoot 'fm1-remote'
$null = New-Item -ItemType Directory -Path $kit
$records = [Collections.Generic.List[object]]::new()
function Copy-KitFile([string]$Source,[string]$Relative) {
    if ($Relative -match '(^[\\/]|:|(^|[\\/])\.\.([\\/]|$))') { throw 'Kit relative path rejected.' }
    Assert-NoReparse $Source
    $sourceItem = Get-Item -LiteralPath $Source -Force
    if ($sourceItem.PSIsContainer) { throw 'Kit inputs must be files.' }
    $destination = [IO.Path]::GetFullPath((Join-Path $kit $Relative))
    if (-not $destination.StartsWith($kit.TrimEnd('\') + '\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Kit destination escaped private directory.' }
    $null = New-Item -ItemType Directory -Path (Split-Path $destination -Parent) -Force
    if (Test-Path -LiteralPath $destination) { throw "Duplicate kit input: $Relative" }
    $sourceHash = (Get-FileHash -LiteralPath $Source -Algorithm SHA256).Hash.ToLowerInvariant()
    Copy-Item -LiteralPath $Source -Destination $destination
    if ((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant() -ne $sourceHash) { throw 'Kit input changed during copy.' }
    $records.Add(@{path=$Relative.Replace('\','/');sha256=$sourceHash;size=$sourceItem.Length})
}
# Tracked source plus this feature's explicit new source files. Never scan the
# entire working tree or copy private-backups/tmp/environment/token files.
$tracked = & git -C $repo ls-files -- tools/jieli-wl82 README.md
if ($LASTEXITCODE -ne 0) { throw 'RepoRoot must be a Git checkout when making a kit.' }
$inputs = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($relative in $tracked) {
    if ([IO.Path]::GetExtension($relative) -in @('.py','.ps1','.md','.html','.txt')) { $null = $inputs.Add($relative) }
}
foreach ($name in @('remote_bridge.py','remote_backend.py','remote_catalog.py','remote_client.py','remote_store.html',
                    'export_remote_catalog.py','start-remote-bridge.ps1','make-remote-kit.ps1','requirements-remote.txt')) {
    $relative = 'tools/jieli-wl82/' + $name
    if (Test-Path -LiteralPath (Join-Path $repo $relative) -PathType Leaf) { $null = $inputs.Add($relative) }
}
$null = $inputs.Add('docs/FM1_REMOTE_DEBUG.md')
foreach ($relative in ($inputs | Sort-Object)) { Copy-KitFile (Join-Path $repo $relative) $relative }
$usbManifest = $null
if ($IncludeUsbTools) {
    $usbRoot = Join-Path $repo 'tools\jl-uboot-tool'
    Assert-NoReparse $usbRoot
    $loader = Join-Path $usbRoot 'data\loaderblobs\usb\wl82loader.bin'
    $loaderSha = 'd41da6126760c9d66660bcc0cac8d27d221806c5e369a8036921efe68dca5376'
    if ((Get-Item -LiteralPath $loader).Length -ne 24064 -or
        (Get-FileHash -LiteralPath $loader -Algorithm SHA256).Hash.ToLowerInvariant() -ne $loaderSha) {
        throw 'Reviewed WL82 loader hash/size mismatch; no USB package exported.'
    }
    $todo = [Collections.Generic.Stack[string]]::new()
    $todo.Push($usbRoot)
    while ($todo.Count) {
        $folder = $todo.Pop()
        foreach ($item in Get-ChildItem -LiteralPath $folder -Force) {
            if ($item.Name -in @('.git','__pycache__','.venv','venv')) { continue }
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Reparse USB dependency: $($item.FullName)" }
            if ($item.PSIsContainer) { $todo.Push($item.FullName); continue }
            $relative = $item.FullName.Substring($usbRoot.Length + 1)
            if ($item.Extension -in @('.py','.yaml') -or $relative -in @('LICENSE','README.md','requirements.txt','data\loaderblobs\usb\wl82loader.bin')) {
                Copy-KitFile $item.FullName ('tools\jl-uboot-tool\' + $relative)
            }
        }
    }
    $usbManifest = @{loader_sha256=$loaderSha;loader_size=24064;source='explicit local reviewed dependency; see per-file hashes'}
}
$baselineManifest = $null
if ($SessionRoot) {
    $session = (Resolve-Path -LiteralPath $SessionRoot).Path
    Assert-NoReparse $session
    $state = Get-Content -LiteralPath (Join-Path $session 'state.json') -Raw | ConvertFrom-Json
    if ($state.blocked -or $state.reset_pending -or ($state.PSObject.Properties['needs_observation'] -and $state.needs_observation) -or $state.operation -ne 'idle') {
        throw 'Session is not idle/verified; blocked or pending evidence must not bootstrap a fresh laptop session.'
    }
    if ($state.baseline_file -match '(^[\\/]|:|(^|[\\/])\.\.([\\/]|$))' -or $state.baseline_sha256 -notmatch '^[0-9a-f]{64}$') { throw 'Invalid session baseline reference.' }
    $baseline = [IO.Path]::GetFullPath((Join-Path $session $state.baseline_file))
    if (-not $baseline.StartsWith($session.TrimEnd('\') + '\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Baseline escaped selected session.' }
    Assert-NoReparse $baseline
    if ((Get-Item -LiteralPath $baseline).Length -ne 1048576 -or
        (Get-FileHash -LiteralPath $baseline -Algorithm SHA256).Hash.ToLowerInvariant() -ne $state.baseline_sha256) { throw 'Selected session baseline size/hash mismatch.' }
    $receipt = Join-Path (Split-Path $baseline -Parent) 'deployment.json'
    if ((Split-Path $baseline -Leaf) -eq 'baseline.bin') { $receipt = Join-Path (Split-Path $baseline -Parent) 'baseline-deployment.json' }
    Assert-NoReparse $receipt
    $deployment = Get-Content -LiteralPath $receipt -Raw | ConvertFrom-Json
    if ($deployment.status -notin @('written_and_twice_readback_verified','written_and_readback_verified') -or
        $deployment.full_readback_sha256 -ne $state.baseline_sha256) { throw 'Baseline requires its successful full-readback deployment receipt.' }
    if ($deployment.status -eq 'written_and_readback_verified' -and $deployment.full_readback_count -ne 1) { throw 'Invalid readback receipt.' }
    Copy-KitFile $baseline 'private-bootstrap\baseline.bin'
    Copy-KitFile $receipt 'private-bootstrap\baseline-deployment.json'
    $baselineManifest = @{file='private-bootstrap/baseline.bin';receipt='private-bootstrap/baseline-deployment.json';sha256=$state.baseline_sha256;size=1048576}
}
if ($CatalogDir) {
    $catalog = (Resolve-Path -LiteralPath $CatalogDir).Path
    Assert-NoReparse $catalog
    $runtime = (Resolve-Path -LiteralPath $Python).Path
    foreach ($bundle in Get-ChildItem -LiteralPath $catalog -Filter '*.json' -File) {
        Assert-NoReparse $bundle.FullName
        $validator = "import json,pathlib,sys; sys.path.insert(0,sys.argv[1]); from remote_catalog import validate_bundle; p=pathlib.Path(sys.argv[2]); b=json.loads(p.read_text(encoding='utf-8-sig')); validate_bundle(b); assert p.stem == b['id']"
        & $runtime -c $validator (Join-Path $repo 'tools\jieli-wl82') $bundle.FullName
        if ($LASTEXITCODE -ne 0) { throw 'Catalog package validation failed; no archive created.' }
        Copy-KitFile $bundle.FullName ('private-catalog\' + $bundle.Name)
    }
}
$commit = & git -C $repo rev-parse HEAD
if ($LASTEXITCODE -ne 0) { throw 'Could not capture source commit.' }
$dirty = & git -C $repo status --porcelain -- tools/jieli-wl82 README.md docs/FM1_REMOTE_DEBUG.md
if ($LASTEXITCODE -ne 0) { throw 'Could not capture source worktree status.' }
$manifest = @{format='FM1RemoteKit/1';created_utc=[DateTime]::UtcNow.ToString('o');source_commit=($commit -join '').Trim();files=$records;
              source_tree_dirty=[bool]$dirty;usb=$usbManifest;baseline=$baselineManifest;credentials_included=$false;device_io=$false}
$manifestPath = Join-Path $kit 'remote-kit-manifest.json'
[IO.File]::WriteAllText($manifestPath,($manifest | ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
$archive = Join-Path $privateRoot "fm1-remote-$version.zip"
Compress-Archive -LiteralPath $kit -DestinationPath $archive -CompressionLevel Optimal
$archiveSha = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText(($archive + '.sha256'),$archiveSha,[Text.UTF8Encoding]::new($false))
Write-Output "Private kit: $archive"
Write-Output "Archive SHA256: $archiveSha"
Write-Output 'No tokens, bridge state, job history, or protected session descriptors copied. Preserve this directory privately; nothing was deleted.'

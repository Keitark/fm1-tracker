# Pure snapshot/ACL helpers. The launcher pins the production source to C:\Python311.
Set-StrictMode -Version Latest

function Assert-FM1NoReparsePath([string]$Path) {
    $node = Get-Item -LiteralPath $Path -Force -ErrorAction Stop
    while ($node) {
        if ($node.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Reparse path rejected: $($node.FullName)" }
        if ($node -is [IO.DirectoryInfo]) { $node = $node.Parent } else { $node = $node.Directory }
    }
}

function Get-FM1SnapshotDestination([string]$Root, [string]$Relative) {
    if (-not $Relative -or $Relative -match '[<>:"|?*\x00-\x1f]' -or $Relative -match '^[\\/]') { throw 'Snapshot path rejected' }
    foreach ($segment in [regex]::Split($Relative,'[\\/]')) {
        if (-not $segment -or $segment -in @('.','..') -or $segment.EndsWith('.') -or $segment.EndsWith(' ') -or
            $segment -imatch '^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\.|$)') { throw 'Snapshot path rejected' }
    }
    $prefix = [IO.Path]::GetFullPath($Root).TrimEnd([char]'\') + '\'
    $destination = [IO.Path]::GetFullPath((Join-Path $Root $Relative))
    if (-not $destination.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)) { throw 'Snapshot path escaped session root' }
    return $destination
}

function Get-FM1PythonSnapshot([string]$RuntimeRoot, [hashtable]$DependencyRoots) {
    $RuntimeRoot = (Get-Item -LiteralPath $RuntimeRoot -Force -ErrorAction Stop).FullName
    Assert-FM1NoReparsePath $RuntimeRoot
    foreach ($required in @('python.exe','python311.dll','Lib\os.py','DLLs')) {
        Assert-FM1NoReparsePath (Join-Path $RuntimeRoot $required)
    }
    foreach ($folder in @('Lib','DLLs')) {
        if ((Get-Item -LiteralPath (Join-Path $RuntimeRoot $folder)) -isnot [IO.DirectoryInfo]) { throw "Python runtime directory required: $folder" }
    }
    foreach ($file in @('python.exe','python311.dll','Lib\os.py')) {
        if ((Get-Item -LiteralPath (Join-Path $RuntimeRoot $file)) -isnot [IO.FileInfo]) { throw "Python runtime file required: $file" }
    }
    $entries = [Collections.Generic.List[object]]::new()
    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    function Add-FM1RuntimeEntry($item, [string]$relative) {
        $destination = Get-FM1SnapshotDestination -Root $RuntimeRoot -Relative $relative
        if (-not $seen.Add($destination)) { throw "Duplicate snapshot path: $relative" }
        $entries.Add(@{source=$item.FullName; relative=$relative; sha256=(Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash.ToLowerInvariant()})
    }
    # No path-control files, even when they otherwise would not be copied.
    foreach ($item in Get-ChildItem -LiteralPath $RuntimeRoot -Force) {
        Assert-FM1NoReparsePath $item.FullName
        if ($item.Name -ieq 'pyvenv.cfg' -or $item.Name -imatch '\._pth$') { throw "Python path override rejected: $($item.FullName)" }
        if (-not $item.PSIsContainer -and $item.Extension -in @('.exe','.dll')) {
            Add-FM1RuntimeEntry $item "runtime\$($item.Name)"
        }
    }
    foreach ($folderName in @('Lib','DLLs')) {
        $todo = [Collections.Generic.Stack[string]]::new()
        $todo.Push((Join-Path $RuntimeRoot $folderName))
        while ($todo.Count) {
            foreach ($item in Get-ChildItem -LiteralPath $todo.Pop() -Force) {
                Assert-FM1NoReparsePath $item.FullName
                if ($item.Name -ieq 'pyvenv.cfg' -or $item.Name -imatch '\._pth$') { throw "Python path override rejected: $($item.FullName)" }
                if ($item.PSIsContainer) {
                    if ($item.Name -notin @('site-packages','__pycache__')) { $todo.Push($item.FullName) }
                } elseif ($item.Extension -notin @('.pyc','.pyo')) {
                    Add-FM1RuntimeEntry $item ('runtime\' + $item.FullName.Substring($RuntimeRoot.Length+1))
                }
            }
        }
    }
    $names = @('yaml','tqdm','crcmod','colorama','serial')
    if ($DependencyRoots.Count -ne $names.Count) { throw 'Exactly five frozen dependency roots are required' }
    foreach ($name in $names) {
        if (-not $DependencyRoots.ContainsKey($name)) { throw "Missing frozen dependency: $name" }
        $folder = (Get-Item -LiteralPath $DependencyRoots[$name] -Force -ErrorAction Stop).FullName
        Assert-FM1NoReparsePath $folder
        Assert-FM1NoReparsePath (Join-Path $folder '__init__.py')
        $todo = [Collections.Generic.Stack[string]]::new(); $todo.Push($folder)
        while ($todo.Count) {
            foreach ($item in Get-ChildItem -LiteralPath $todo.Pop() -Force) {
                Assert-FM1NoReparsePath $item.FullName
                if ($item.Name -ieq 'pyvenv.cfg' -or $item.Name -imatch '\._pth$') { throw "Dependency path override rejected: $($item.FullName)" }
                if ($item.PSIsContainer) {
                    if ($item.Name -ne '__pycache__') { $todo.Push($item.FullName) }
                } elseif ($item.Extension -notin @('.pyc','.pyo')) {
                    Add-FM1RuntimeEntry $item ("lib\$name\" + $item.FullName.Substring($folder.Length+1))
                }
            }
        }
    }
    return $entries.ToArray()
}

function Assert-FM1ReadOnlyTree([string]$Root) {
    Assert-FM1NoReparsePath $Root
    $todo = [Collections.Generic.Stack[string]]::new(); $todo.Push($Root)
    while ($todo.Count) {
        $path = $todo.Pop()
        $item = Get-Item -LiteralPath $path -Force -ErrorAction Stop
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Protected reparse path: $path" }
        $acl = Get-Acl -LiteralPath $path -ErrorAction Stop
        $owner = $acl.GetOwner([Security.Principal.SecurityIdentifier]).Value
        if ($owner -notin @('S-1-5-18','S-1-5-32-544')) { throw "Protected object has a non-administrator owner: $path" }
        foreach ($rule in $acl.Access) {
            $sid = $rule.IdentityReference.Translate([Security.Principal.SecurityIdentifier]).Value
            # Includes DELETE_CHILD (0x40), DELETE, WRITE_DAC and WRITE_OWNER.
            if ($rule.AccessControlType -eq 'Allow' -and $sid -notin @('S-1-5-18','S-1-5-32-544') -and
                ([int]$rule.FileSystemRights -band 0xD0156)) { throw "Protected object writable outside administrators: $path ($sid)" }
        }
        if ($item.PSIsContainer) { foreach ($child in Get-ChildItem -LiteralPath $path -Force) { $todo.Push($child.FullName) } }
    }
}

function Set-FM1StrictTree([string]$Root, [string]$ReaderSid) {
    Assert-FM1NoReparsePath $Root
    $todo = [Collections.Generic.Stack[string]]::new(); $todo.Push($Root)
    while ($todo.Count) {
        $path = $todo.Pop(); $item = Get-Item -LiteralPath $path -Force -ErrorAction Stop
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Protected reparse path: $path" }
        if ($item.PSIsContainer) {
            $acl = [Security.AccessControl.DirectorySecurity]::new()
            $reader = if ($ReaderSid) { "(A;OICI;GRGX;;;$ReaderSid)" } else { '' }
            $acl.SetSecurityDescriptorSddlForm("O:BAG:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)$reader")
            foreach ($child in Get-ChildItem -LiteralPath $path -Force) { $todo.Push($child.FullName) }
        } else {
            $acl = [Security.AccessControl.FileSecurity]::new()
            $reader = if ($ReaderSid) { "(A;;GRGX;;;$ReaderSid)" } else { '' }
            $acl.SetSecurityDescriptorSddlForm("O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)$reader")
        }
        Set-Acl -LiteralPath $path -AclObject $acl -ErrorAction Stop
    }
}

#requires -Version 7.0
<#
.SYNOPSIS
Moves superseded heavy release and test-run payloads out of this checkout into an archive.

.DESCRIPTION
Shared EXPBG release tooling (identical in mod-gm-tools and mod-cdf-compat). release.ps1 runs it
after a verified -Publish; it can also be run by hand. It only looks at build/<run>,
artifacts/<run> and .local/workshop-local-<utc> folders named <family>-yyyyMMdd-HHmmss-fff[-suffix];
any other name is left alone. docs/RELEASING.md lists what each run type leaves behind.

Kept in full:
- the newest -KeepReleases published release runs per Workshop item (workshop-receipt.json says
  uploaded and packageVerified), both the artifacts/ and .local/ halves, and -PublishedRun;
- the newest -KeepBuilds build/local-* snapshots and the build named in .local/last-build.json;
- the newest -KeepRuns runs of every other family (gameplay, contracts, art, audio-sweep, ...);
- anything modified within -MinAgeHours, unless it is a release run of an older version than the
  newest published release of its item (a superseded payload). The version comes from the
  run's workshop-receipt.json, receipt.json or source zip name; an earlier attempt of the same
  version is not superseded and follows the age rule.

Every other run is slimmed: evidence stays in place, heavy payloads move to the archive.
- Evidence (stays, whatever its size): *.json, *.jsonl, *.log, *.txt, *.md, *.csv outside addon
  payloads (receipts, manifests, change notes, results, logs), and the Workshop manifest.json of a
  packed or published bundle. Release guards (Assert-NoWorkshopUploadAttempt, retired-version
  checks, Get-LocalReleaseSource) keep reading them.
- Heavy (moves): addon payloads (any folder holding a .gproj: Stage, Assembled, built, packed,
  ui-published, previous-ui-bundle, PC, dependency and fixture snapshots), *.zip, *.pak, *.7z and
  any other file of 1 MB or more. A folder without evidence moves as a whole.
- Other small files stay.
Items keep their repository-relative path under <ArchiveRoot>/<repository folder name>
(ArchiveRoot from .local/config.json; never inside this or any other Git checkout). Same volume:
atomic rename. Different volume: copy, verify file count and every file size (-VerifyHash adds
SHA-256) against an unchanged source, then take the source out of use with a same-folder rename
(fails while any file in it is open) and remove it; a failed copy or a source in use is rolled back
in the archive and the source stays. Links (junctions, symlinks, mount points) are never followed or
moved. Without ArchiveRoot nothing is moved. Nothing is ever deleted, except by -Purge.

-Purge -OlderThanDays n permanently deletes archived run folders whose last recorded archive event
(archived.json with role 'archive', written by this tool into the archive copy) is older than n
days. Folders without that record (including the role 'source' pointers beside kept evidence) or
holding a link are skipped. It asks for confirmation unless -Confirm:$false, touches only the
archive and is never called by release.ps1.

.EXAMPLE
./tools/Invoke-ReleaseHousekeeping.ps1 -WhatIf
.EXAMPLE
./tools/Invoke-ReleaseHousekeeping.ps1 -KeepReleases 2 -KeepBuilds 1 -KeepRuns 5
.EXAMPLE
./tools/Invoke-ReleaseHousekeeping.ps1 -Purge -OlderThanDays 90
#>
[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'Medium', DefaultParameterSetName = 'Archive')]
param(
    [Parameter(ParameterSetName = 'Archive')][ValidateRange(0, 1000)][int]$KeepReleases = 1,
    [Parameter(ParameterSetName = 'Archive')][ValidateRange(0, 1000)][int]$KeepBuilds = 1,
    [Parameter(ParameterSetName = 'Archive')][ValidateRange(0, 1000)][int]$KeepRuns = 3,
    [Parameter(ParameterSetName = 'Archive')][ValidateRange(0, 87600)][double]$MinAgeHours = 24,
    # The run release.ps1 just published; housekeeping is skipped unless its receipt is verified.
    [Parameter(ParameterSetName = 'Archive')][ValidatePattern('^workshop-local-\d{8}-\d{6}-\d{3}$')][string]$PublishedRun,
    # Copy, verify and remove even on the same volume (exercises the cross-volume path).
    [Parameter(ParameterSetName = 'Archive')][switch]$ForceCopy,
    [Parameter(ParameterSetName = 'Archive')][switch]$VerifyHash,
    # Test seam: fail a verified copy after this many files (portable rollback test only).
    [Parameter(ParameterSetName = 'Archive', DontShow)][int]$TestFailCopyAfter = 0,
    [Parameter(Mandatory, ParameterSetName = 'Purge')][switch]$Purge,
    [Parameter(Mandatory, ParameterSetName = 'Purge')][ValidateRange(1, 36500)][int]$OlderThanDays,
    # Overrides ArchiveRoot from .local/config.json.
    [string]$ArchiveRoot,
    [string]$RepoRoot = (Split-Path -Parent $PSScriptRoot),
    [switch]$PassThru
)
$ErrorActionPreference = 'Stop'
$separator = [IO.Path]::DirectorySeparatorChar
$comparison = if ($IsWindows) { [StringComparison]::OrdinalIgnoreCase } else { [StringComparison]::Ordinal }
$enumeration = [IO.EnumerationOptions]@{ RecurseSubdirectories = $false; AttributesToSkip = [IO.FileAttributes]0; IgnoreInaccessible = $false; ReturnSpecialDirectories = $false }
$evidenceExtensions = @('.json', '.jsonl', '.log', '.txt', '.md', '.csv')
$evidenceNames = @('receipt.json', 'workshop-receipt.json', 'source-manifest.json', 'manifest.json', 'dependencies.json', 'inputs.json', 'run.json', 'result.json', 'summary.json', 'change-note.txt', 'archived.json')
$heavyExtensions = @('.zip', '.pak', '.7z')
$heavyFileBytes = 1MB
$runPattern = '^(?<family>[A-Za-z][A-Za-z0-9]*(?:-[A-Za-z][A-Za-z0-9]*)*)-(?<stamp>\d{8}-\d{6}-\d{3})(?<suffix>-[A-Za-z0-9_-]+)?$'
$invariant = [Globalization.CultureInfo]::InvariantCulture
$now = [DateTime]::UtcNow

function Get-FullPath([string]$Path) {
    $full = [IO.Path]::GetFullPath($Path)
    if ($full.Length -gt [IO.Path]::GetPathRoot($full).Length) { $full = $full.TrimEnd('\', '/') }
    $full
}
function Test-Under([string]$Path, [string]$Parent) { $Path.StartsWith($Parent.TrimEnd('\', '/') + $separator, $comparison) }
function Test-Link([IO.FileSystemInfo]$Item) { [bool]($Item.Attributes -band [IO.FileAttributes]::ReparsePoint) -or $null -ne $Item.LinkTarget }
function Find-LinkedAncestor([string]$Path, [string]$Stop = '') {
    # First existing link (junction, symlink, mount point) from Path up to Stop (or the root), else $null.
    for ($ancestor = $Path; $ancestor; $ancestor = Split-Path -Parent $ancestor) {
        if ((Test-Path -LiteralPath $ancestor) -and (Test-Link (Get-Item -LiteralPath $ancestor -Force))) { return $ancestor }
        if ($Stop -and $ancestor.Equals($Stop, $comparison)) { break }
    }
    $null
}
function Format-Size([double]$Bytes) {
    if ($Bytes -ge 1GB) { return [string]::Format($invariant, '{0:N1} GB', $Bytes / 1GB) }
    if ($Bytes -ge 1MB) { return [string]::Format($invariant, '{0:N1} MB', $Bytes / 1MB) }
    if ($Bytes -ge 1KB) { return [string]::Format($invariant, '{0:N0} KB', $Bytes / 1KB) }
    "$Bytes B"
}
function Get-VolumeInfo([string]$Path) {
    # Longest mounted root containing the path (drive letters on Windows, mount points elsewhere).
    $full = [IO.Path]::GetFullPath($Path)
    $best = $null
    foreach ($drive in [IO.DriveInfo]::GetDrives()) {
        try { $root = $drive.RootDirectory.FullName } catch { continue }
        $prefix = $root.TrimEnd('\', '/')
        if (($prefix -eq '' -or $full.Equals($root, $comparison) -or $full.Equals($prefix, $comparison) -or $full.StartsWith($prefix + $separator, $comparison)) -and (!$best -or $root.Length -gt $best.RootDirectory.FullName.Length)) { $best = $drive }
    }
    $best
}
function Get-VolumeKey([string]$Path) {
    $volume = Get-VolumeInfo $Path
    if ($volume) { return $volume.RootDirectory.FullName.ToUpperInvariant() }
    [IO.Path]::GetPathRoot([IO.Path]::GetFullPath($Path)).ToUpperInvariant()
}
function Get-FreeSpace([string]$Path) {
    try {
        $volume = Get-VolumeInfo $Path
        if ($volume -and $volume.IsReady) { return [pscustomobject]@{ Root = $volume.RootDirectory.FullName; Free = $volume.AvailableFreeSpace } }
    } catch { }
    $null
}
function Get-InnerException($Exception) {
    while ($Exception -is [Management.Automation.MethodInvocationException] -and $Exception.InnerException) { $Exception = $Exception.InnerException }
    $Exception
}
function Read-JsonFile([string]$Path) {
    if (!(Test-Path -LiteralPath $Path -PathType Leaf)) { return $null }
    try { Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json } catch { $null }
}
function ConvertTo-ReleaseVersion($Value) {
    $parsed = $null
    if ($Value -is [string] -and $Value -match '^\d+\.\d+\.\d+$' -and [version]::TryParse($Value, [ref]$parsed)) { return $parsed }
    $null
}

function Measure-Tree([IO.FileSystemInfo]$Item) {
    # Files, bytes and newest write below an item. Throws on any link so nothing is moved or deleted through one.
    if (Test-Link $Item) { throw "Link found: $($Item.FullName)" }
    if ($Item -is [IO.FileInfo]) { return [pscustomobject]@{ Files = 1; Bytes = [long]$Item.Length; Latest = $Item.LastWriteTimeUtc } }
    $files = 0; $bytes = [long]0; $latest = [DateTime]::MinValue
    $stack = [Collections.Generic.Stack[IO.DirectoryInfo]]::new()
    $stack.Push($Item)
    while ($stack.Count) {
        foreach ($entry in @($stack.Pop().EnumerateFileSystemInfos('*', $enumeration))) {
            if (Test-Link $entry) { throw "Link found: $($entry.FullName)" }
            if ($entry -is [IO.FileInfo]) { $files++; $bytes += $entry.Length; if ($entry.LastWriteTimeUtc -gt $latest) { $latest = $entry.LastWriteTimeUtc } } else { $stack.Push($entry) }
        }
    }
    [pscustomobject]@{ Files = $files; Bytes = $bytes; Latest = $latest }
}
function Test-Unchanged($Measured, $Expected) { $Measured.Files -eq $Expected.Files -and $Measured.Bytes -eq $Expected.Bytes -and $Measured.Latest -eq $Expected.Latest }
function Move-Path([string]$Source, [string]$Target, [bool]$Directory) {
    if ($Directory) { [IO.Directory]::Move($Source, $Target) } else { [IO.File]::Move($Source, $Target, $false) }
}

function Remove-TreeNoFollow([string]$Path) {
    # Used only on a verified-moved source, this call's own (incomplete) copy, or a confirmed purge entry.
    $item = Get-Item -LiteralPath $Path -Force
    $null = Measure-Tree $item
    if ($item -is [IO.FileInfo]) {
        if ($item.Attributes -band [IO.FileAttributes]::ReadOnly) { $item.Attributes = [IO.FileAttributes]::Normal }
        [IO.File]::Delete($item.FullName); return
    }
    foreach ($file in @($item.EnumerateFiles('*', [IO.EnumerationOptions]@{ RecurseSubdirectories = $true; AttributesToSkip = [IO.FileAttributes]0 }))) {
        if ($file.Attributes -band [IO.FileAttributes]::ReadOnly) { $file.Attributes = [IO.FileAttributes]::Normal }
    }
    [IO.Directory]::Delete($item.FullName, $true)
}

function Get-FileKind([IO.FileInfo]$File, [bool]$Payload, [bool]$PayloadRoot) {
    $name = $File.Name.ToLowerInvariant()
    if ($Payload) {
        # The Workshop upload manifest beside a packed or published bundle records its hashes and listing.
        if ($PayloadRoot -and $name -eq 'manifest.json') { return 'evidence' }
        return 'heavy'
    }
    if ($name -in $evidenceNames) { return 'evidence' }
    $extension = $File.Extension.ToLowerInvariant()
    if ($extension -in $evidenceExtensions) { return 'evidence' }
    if ($extension -in $heavyExtensions -or $File.Length -ge $heavyFileBytes) { return 'heavy' }
    'other'
}

function Get-SlimPlan([IO.DirectoryInfo]$Directory, [bool]$InPayload, [bool]$IsRunRoot) {
    $entries = @($Directory.EnumerateFileSystemInfos('*', $enumeration))
    # A folder holding an engine project is an addon payload (never the run folder itself).
    $payloadRoot = !$IsRunRoot -and !$InPayload -and [bool]@($entries | Where-Object { $_ -is [IO.FileInfo] -and $_.Extension -ieq '.gproj' }).Count
    $payload = $InPayload -or $payloadRoot
    $node = [pscustomobject]@{ Files = 0; Bytes = [long]0; Evidence = 0; Heavy = 0; Latest = [DateTime]::MinValue; Unit = $false
        Links = [Collections.Generic.List[string]]::new(); Moves = [Collections.Generic.List[object]]::new() }
    $candidates = [Collections.Generic.List[object]]::new()
    foreach ($entry in $entries) {
        if (Test-Link $entry) { $node.Links.Add($entry.FullName); continue }
        if ($entry -is [IO.FileInfo]) {
            $node.Files++; $node.Bytes += $entry.Length
            if (!($IsRunRoot -and $entry.Name -like 'archived*.json') -and $entry.LastWriteTimeUtc -gt $node.Latest) { $node.Latest = $entry.LastWriteTimeUtc }
            switch (Get-FileKind $entry $payload $payloadRoot) {
                'evidence' { $node.Evidence++ }
                'heavy' { $node.Heavy++; $candidates.Add([pscustomobject]@{ Item = $entry; Files = 1; Bytes = [long]$entry.Length }) }
            }
            continue
        }
        $sub = Get-SlimPlan $entry $payload $false
        $node.Files += $sub.Files; $node.Bytes += $sub.Bytes; $node.Evidence += $sub.Evidence; $node.Heavy += $sub.Heavy
        if ($sub.Latest -gt $node.Latest) { $node.Latest = $sub.Latest }
        foreach ($link in $sub.Links) { $node.Links.Add($link) }
        if ($sub.Unit) { $candidates.Add([pscustomobject]@{ Item = $entry; Files = $sub.Files; Bytes = $sub.Bytes }) }
        else { foreach ($move in $sub.Moves) { $node.Moves.Add($move) } }
    }
    # A folder without evidence or links that holds heavy content moves as a whole.
    $node.Unit = !$IsRunRoot -and !$node.Evidence -and !$node.Links.Count -and $node.Heavy -gt 0
    if (!$node.Unit) { foreach ($candidate in $candidates) { $node.Moves.Add($candidate) } }
    $node
}

function Copy-Tree([IO.FileSystemInfo]$Item, [string]$Target) {
    $stats = [pscustomobject]@{ Files = 0; Bytes = [long]0 }
    $pending = [Collections.Generic.Stack[object]]::new()
    if ($Item -is [IO.FileInfo]) { Copy-VerifiedFile $Item $Target $stats; return $stats }
    [void][IO.Directory]::CreateDirectory($Target)
    $pending.Push(@($Item, $Target))
    while ($pending.Count) {
        $directory, $destination = $pending.Pop()
        foreach ($entry in @($directory.EnumerateFileSystemInfos('*', $enumeration))) {
            if (Test-Link $entry) { throw "Link found: $($entry.FullName)" }
            $path = Join-Path $destination $entry.Name
            if ($entry -is [IO.FileInfo]) { Copy-VerifiedFile $entry $path $stats }
            else { [void][IO.Directory]::CreateDirectory($path); $pending.Push(@($entry, $path)) }
        }
    }
    $stats
}
function Copy-VerifiedFile([IO.FileInfo]$File, [string]$Target, $Stats) {
    if ($TestFailCopyAfter -gt 0 -and $Stats.Files -ge $TestFailCopyAfter) { throw 'Simulated copy failure (test seam).' }
    [IO.File]::Copy($File.FullName, $Target, $false)
    if ([IO.FileInfo]::new($Target).Length -ne $File.Length) { throw "Copied size differs: $($File.FullName)" }
    if ($VerifyHash -and (Get-FileHash -LiteralPath $Target).Hash -ne (Get-FileHash -LiteralPath $File.FullName).Hash) { throw "Copied SHA-256 differs: $($File.FullName)" }
    $Stats.Files++; $Stats.Bytes += $File.Length
}

function Move-Verified([IO.FileSystemInfo]$Item, [string]$Destination) {
    $source = $Item.FullName
    $directory = $Item -is [IO.DirectoryInfo]
    if (Test-Path -LiteralPath $Destination) { return [pscustomobject]@{ Status = 'skipped'; Method = ''; Detail = 'already in the archive; left in place' } }
    # A junction or mount point inside the archive would send the payload elsewhere (or across volumes).
    $linked = Find-LinkedAncestor (Split-Path -Parent $Destination) $archive
    if ($linked) { return [pscustomobject]@{ Status = 'skipped'; Method = ''; Detail = "link in the archive path ($linked); not followed" } }
    # Fresh metadata: a file's size and write time from the planning enumeration may be cached.
    $Item.Refresh()
    if (!$Item.Exists) { return [pscustomobject]@{ Status = 'skipped'; Method = ''; Detail = 'no longer in the run folder' } }
    try { $expected = Measure-Tree $Item } catch { return [pscustomobject]@{ Status = 'skipped'; Method = ''; Detail = $_.Exception.Message } }
    try { [void][IO.Directory]::CreateDirectory((Split-Path -Parent $Destination)) }
    catch { return [pscustomobject]@{ Status = 'failed'; Method = ''; Detail = "archive folder not writable: $((Get-InnerException $_.Exception).Message)" } }
    if ($renameAllowed -and (Get-VolumeKey $source) -eq (Get-VolumeKey $Destination)) {
        try {
            Move-Path $source $Destination $directory
            $actual = Measure-Tree (Get-Item -LiteralPath $Destination -Force)
            if ($actual.Files -ne $expected.Files -or $actual.Bytes -ne $expected.Bytes) { return [pscustomobject]@{ Status = 'failed'; Method = 'rename'; Detail = "renamed, but the archived count/size differs; inspect $Destination" } }
            return [pscustomobject]@{ Status = 'moved'; Method = 'rename'; Detail = '' }
        } catch {
            $inner = Get-InnerException $_.Exception
            # 17 = ERROR_NOT_SAME_DEVICE (Windows), 18 = EXDEV (Unix): fall back to the verified copy.
            if ($inner -isnot [IO.IOException] -or ($inner.HResult -band 0xFFFF) -notin 17, 18 -or !(Test-Path -LiteralPath $source) -or (Test-Path -LiteralPath $Destination)) {
                return [pscustomobject]@{ Status = 'failed'; Method = 'rename'; Detail = $inner.Message }
            }
        }
    }
    # Different volume: copy beside the destination, verify it against an unchanged source, publish it
    # by rename, take the source out of use by a same-folder rename, then remove it.
    $suffix = [guid]::NewGuid().ToString('N').Substring(0, 8)
    $staging = $Destination + '.incomplete-' + $suffix
    try {
        $copied = Copy-Tree $Item $staging
        $current = Measure-Tree (Get-Item -LiteralPath $source -Force)
        if ($copied.Files -ne $expected.Files -or $copied.Bytes -ne $expected.Bytes -or !(Test-Unchanged $current $expected)) { throw 'Copy verification failed: file count, size or write time differs.' }
        Move-Path $staging $Destination $directory
    } catch {
        $message = (Get-InnerException $_.Exception).Message
        # Roll back only the incomplete copy this call created; the source was never touched.
        $rollback = ''
        if (Test-Path -LiteralPath $staging) { try { Remove-TreeNoFollow $staging } catch { $rollback = " Incomplete copy left at $staging." } }
        return [pscustomobject]@{ Status = 'failed'; Method = 'copy'; Detail = "$message Source kept.$rollback" }
    }
    # The rename fails while any file below the source is open, so a source in use is never half deleted.
    $retired = $source + '.archived-' + $suffix
    $withdraw = {
        param([string]$Why)
        $left = ''
        try { Remove-TreeNoFollow $Destination } catch { $left = " The verified copy stays at $Destination." }
        [pscustomobject]@{ Status = 'failed'; Method = 'copy'; Detail = "$Why Source kept.$left" }
    }
    try { Move-Path $source $retired $directory }
    catch { return & $withdraw "source in use ($((Get-InnerException $_.Exception).Message))." }
    $changed = $true
    try { $changed = !(Test-Unchanged (Measure-Tree (Get-Item -LiteralPath $retired -Force)) $expected) } catch { }
    if ($changed) {
        try { Move-Path $retired $source $directory }
        catch { return [pscustomobject]@{ Status = 'failed'; Method = 'copy'; Detail = "source changed after the copy and could not be renamed back; it is complete at $retired, the earlier copy at $Destination." } }
        return & $withdraw 'source changed after the copy.'
    }
    try { Remove-TreeNoFollow $retired }
    catch { return [pscustomobject]@{ Status = 'failed'; Method = 'copy'; Detail = "copied and verified to $Destination, but the source (renamed to $retired) was not fully removed: $((Get-InnerException $_.Exception).Message)" } }
    [pscustomobject]@{ Status = 'moved'; Method = 'copy'; Detail = '' }
}

function Add-ArchiveRecord([string]$Directory, [ValidateSet('archive', 'source')][string]$Role, $Entry) {
    # role 'archive': the archive copy (-Purge ages it); role 'source': the pointer beside the kept evidence.
    $path = Join-Path $Directory 'archived.json'
    $record = $null
    if (Test-Path -LiteralPath $path -PathType Leaf) {
        try { $record = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json -AsHashtable } catch { $record = $null }
        # Never overwrite an unreadable or foreign record; start a new one beside it.
        if ($record -isnot [Collections.IDictionary] -or $record.role -ne $Role) { $record = $null; $path = Join-Path $Directory ('archived-' + $now.ToString('yyyyMMdd-HHmmss-fff') + '.json') }
    }
    if (!$record) { $record = [ordered]@{ tool = 'tools/Invoke-ReleaseHousekeeping.ps1'; role = $Role; events = @() } }
    $record.events = @($record.events) + @($Entry)
    $record | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $path -Encoding utf8NoBOM
}
function Get-ArchivedTime([IO.DirectoryInfo]$Directory) {
    # Newest archivedUtc of this tool's archive-copy records, or $null without one. Source-side pointers
    # (role 'source', beside kept receipts) never count, so a misdirected purge cannot reach evidence.
    $latest = [DateTime]::MinValue
    foreach ($file in @($Directory.EnumerateFiles('archived*.json', $enumeration))) {
        try {
            $record = Get-Content -LiteralPath $file.FullName -Raw | ConvertFrom-Json
            if ($record.role -cne 'archive') { continue }
            foreach ($recorded in @($record.events)) {
                # ConvertFrom-Json may already have turned the ISO 8601 text into a DateTime.
                $value = $recorded.archivedUtc
                $stamp = if ($value -is [DateTime]) { $value.ToUniversalTime() } else { [DateTime]::Parse([string]$value, $invariant, [Globalization.DateTimeStyles]::AdjustToUniversal -bor [Globalization.DateTimeStyles]::AssumeUniversal) }
                if ($stamp -gt $latest) { $latest = $stamp }
            }
        } catch { }
    }
    if ($latest -eq [DateTime]::MinValue) { return $null }
    $latest
}
function Format-SpaceChange([string]$Label, $Before, $After, [bool]$Projected) {
    if (!$Before -or $null -eq $After) { return $null }
    if ($Projected) { return "$Label$($Before.Root) $(Format-Size $Before.Free) now, about $(Format-Size $After) after" }
    "$Label$($Before.Root) $(Format-Size $Before.Free) before, $(Format-Size $After) after"
}

# --- Locations ---------------------------------------------------------------------------------
if (!(Test-Path -LiteralPath $RepoRoot -PathType Container)) { throw "Repository folder not found: $RepoRoot" }
$repo = Get-FullPath (Resolve-Path -LiteralPath $RepoRoot).ProviderPath
$repoName = Split-Path -Leaf $repo
# A real checkout (not a test fixture) uses its own configuration readers, release mutex and engine check.
$repoTools = Join-Path $repo 'tools'
$realRepo = Test-Path -LiteralPath (Join-Path $repoTools 'Get-ProjectConfig.ps1') -PathType Leaf
$configScript = if (Test-Path -LiteralPath (Join-Path $repoTools 'Get-LocalConfig.ps1') -PathType Leaf) { Join-Path $repoTools 'Get-LocalConfig.ps1' } else { Join-Path $PSScriptRoot 'Get-LocalConfig.ps1' }
$configured = $ArchiveRoot
if (!$PSBoundParameters.ContainsKey('ArchiveRoot')) {
    $configured = (& $configScript -ConfigPath (Join-Path $repo '.local/config.json')).ArchiveRoot
}
$archive = $null
$archiveProblem = 'ArchiveRoot is not set in .local/config.json'
if ($configured -and $configured.Trim()) {
    $value = $configured.Trim()
    if ($value -match 'replace-with') { $archiveProblem = 'ArchiveRoot is still the example placeholder' }
    elseif (![IO.Path]::IsPathFullyQualified($value)) { $archiveProblem = "ArchiveRoot must be an absolute path: $value" }
    else {
        $root = Get-FullPath $value
        $candidate = if ((Split-Path -Leaf $root) -ieq $repoName) { $root } else { Get-FullPath (Join-Path $root $repoName) }
        $archiveProblem = $null
        if ($candidate.Equals($repo, $comparison) -or (Test-Under $candidate $repo) -or (Test-Under $repo $candidate)) { $archiveProblem = "ArchiveRoot must be outside this repository: $candidate" }
        elseif (!(Test-Path -LiteralPath ([IO.Path]::GetPathRoot($candidate)))) { $archiveProblem = "archive drive is not available: $candidate" }
        else {
            $linkedArchive = Find-LinkedAncestor $candidate
            if ($linkedArchive) { $archiveProblem = "linked archive paths are not used: $linkedArchive" }
            else {
                # Catches aliases the text comparison misses (8.3 names, \\localhost\C$ shares, another
                # clone): an archive inside any Git checkout, this one included, is refused.
                for ($ancestor = $candidate; $ancestor; $ancestor = Split-Path -Parent $ancestor) {
                    if (Test-Path -LiteralPath (Join-Path $ancestor '.git')) { $archiveProblem = "ArchiveRoot must not be inside a Git checkout: $ancestor"; break }
                }
            }
        }
        if (!$archiveProblem) { $archive = $candidate }
    }
}
# Same-volume renames only when no link above the checkout can hide a different volume.
$renameAllowed = !$ForceCopy -and !(Find-LinkedAncestor $repo)

# Serialize with releases of this repository (release.ps1 holds the same mutex on this thread).
# (BitConverter/ComputeHash rather than Convert.ToHexString/SHA256.HashData: those need .NET 5, #requires allows 7.0.)
$hasher = [Security.Cryptography.SHA256]::Create()
try { $repoHash = [BitConverter]::ToString($hasher.ComputeHash([Text.Encoding]::UTF8.GetBytes($repo.ToLowerInvariant()))).Replace('-', '').Substring(0, 16) } finally { $hasher.Dispose() }
$mutexName = 'Local\EXPBG-Housekeeping-' + $repoHash
if ($realRepo) { try { $mutexName = 'Local\Reforger-Workshop-' + (& (Join-Path $repoTools 'Get-ProjectConfig.ps1') -Repo $repo).addon.id } catch { } }
$mutex = $null
$locked = $false
try {
    $mutex = [Threading.Mutex]::new($false, $mutexName)
    try { $locked = $mutex.WaitOne(0) } catch [Threading.AbandonedMutexException] { $locked = $true }
} catch {
    if ($IsWindows) { throw }
    $locked = $true # Named mutexes may be unavailable off Windows; the portable test runs alone.
}
if (!$locked) {
    Write-Host 'Release housekeeping skipped: another release or housekeeping run holds this repository.'
    if ($mutex) { $mutex.Dispose() }
    return
}

$result = [pscustomobject]@{ Mode = $PSCmdlet.ParameterSetName; Repository = $repo; Archive = $archive; ArchiveProblem = $archiveProblem
    Runs = @(); Moved = [Collections.Generic.List[object]]::new(); Failed = [Collections.Generic.List[object]]::new(); Skipped = [Collections.Generic.List[object]]::new()
    Planned = [Collections.Generic.List[object]]::new(); MovedBytes = [long]0; PlannedBytes = [long]0; Purged = [Collections.Generic.List[object]]::new() }
try {
# --- Purge (explicit, permanent, archive only) -------------------------------------------------
if ($Purge) {
    if (!$archive) { throw "Purge needs a usable ArchiveRoot: $archiveProblem." }
    if (!(Test-Path -LiteralPath $archive -PathType Container)) { Write-Host "Archive purge: $archive does not exist; nothing to delete."; if ($PassThru) { $result }; return }
    $cutoff = $now.AddDays(-$OlderThanDays)
    $entries = [Collections.Generic.List[object]]::new()
    foreach ($area in 'build', 'artifacts', '.local') {
        $areaPath = Join-Path $archive $area
        if (!(Test-Path -LiteralPath $areaPath -PathType Container)) { continue }
        $areaInfo = [IO.DirectoryInfo]::new($areaPath)
        if (Test-Link $areaInfo) { $result.Skipped.Add([pscustomobject]@{ Path = $area; Detail = 'link; not purged' }); continue }
        foreach ($directory in @($areaInfo.EnumerateDirectories('*', $enumeration))) {
            $relative = "$area/$($directory.Name)"
            if (Test-Link $directory) { $result.Skipped.Add([pscustomobject]@{ Path = $relative; Detail = 'link; not purged' }); continue }
            $archived = Get-ArchivedTime $directory
            # Only folders this tool archived (and recorded) are ever purged.
            if ($null -eq $archived) { $result.Skipped.Add([pscustomobject]@{ Path = $relative; Detail = 'no archived.json record; not purged' }); continue }
            if ($archived -ge $cutoff) { continue }
            try { $size = Measure-Tree $directory } catch { $result.Skipped.Add([pscustomobject]@{ Path = $relative; Detail = 'contains a link; not purged' }); continue }
            $entries.Add([pscustomobject]@{ Path = $relative; FullName = $directory.FullName; Archived = $archived; Files = $size.Files; Bytes = $size.Bytes })
        }
    }
    foreach ($skip in $result.Skipped) { Write-Host "Archive purge: skipped $($skip.Path) ($($skip.Detail))." }
    if (!$entries.Count) { Write-Host "Archive purge: nothing in $archive was archived more than $OlderThanDays day(s) ago."; if ($PassThru) { $result }; return }
    $total = [long]($entries | Measure-Object Bytes -Sum).Sum
    foreach ($entry in $entries) { Write-Host ('  {0}  archived {1}  {2}' -f $entry.Path, $entry.Archived.ToString('yyyy-MM-dd', $invariant), (Format-Size $entry.Bytes)) }
    $target = "$($entries.Count) archived run folder(s), $(Format-Size $total), in $archive"
    # Permanent deletion always asks (and fails in non-interactive sessions) unless -Confirm:$false is explicit.
    $approved = $false
    if ($WhatIfPreference) { [void]$PSCmdlet.ShouldProcess($target, 'Permanently delete') }
    elseif ($PSBoundParameters.ContainsKey('Confirm') -and !$PSBoundParameters['Confirm']) { $approved = $true }
    else { $approved = $PSCmdlet.ShouldContinue("Permanently delete $target? This cannot be undone.", 'Archive purge') }
    if ($approved) {
        foreach ($entry in $entries) {
            if (!(Test-Under $entry.FullName $archive)) { throw "Refusing to delete outside the archive: $($entry.FullName)" }
            try { Remove-TreeNoFollow $entry.FullName; $result.Purged.Add($entry) }
            catch { $result.Failed.Add([pscustomobject]@{ Path = $entry.Path; Detail = (Get-InnerException $_.Exception).Message }); Write-Warning "Archive purge: $($entry.Path) not fully deleted: $((Get-InnerException $_.Exception).Message)" }
        }
        Write-Host "Archive purge: permanently deleted $($result.Purged.Count) archived run folder(s), $(Format-Size (($result.Purged | Measure-Object Bytes -Sum).Sum)), from $archive."
    }
    if ($PassThru) { $result }
    return
}

# --- Inventory ---------------------------------------------------------------------------------
$runs = [Collections.Generic.List[object]]::new()
foreach ($area in 'build', 'artifacts', '.local') {
    $areaPath = Join-Path $repo $area
    if (!(Test-Path -LiteralPath $areaPath -PathType Container)) { continue }
    $areaInfo = [IO.DirectoryInfo]::new($areaPath)
    if (Test-Link $areaInfo) { $result.Skipped.Add([pscustomobject]@{ Path = "$area/"; Detail = 'link; not followed' }); continue }
    foreach ($directory in @($areaInfo.EnumerateDirectories('*', $enumeration))) {
        # In .local only release payload folders are housekeeping candidates; e2e, art, notes etc. stay.
        if ($area -eq '.local' -and $directory.Name -cnotmatch '^workshop-local-\d{8}-\d{6}-\d{3}$') { continue }
        $relative = "$area/$($directory.Name)"
        if (Test-Link $directory) { $result.Skipped.Add([pscustomobject]@{ Path = $relative; Detail = 'link; not followed' }); continue }
        $run = [pscustomobject]@{ Path = $relative; Area = $area; Name = $directory.Name; Info = $directory; Kind = 'unknown'; Group = ''; Stamp = $null
            Item = ''; Version = $null; Published = $false; Decision = 'keep'; Reason = 'unrecognized name'; Plan = $null }
        $match = [regex]::Match($directory.Name, $runPattern)
        if ($match.Success) {
            try {
                $run.Stamp = [DateTime]::ParseExact($match.Groups['stamp'].Value, 'yyyyMMdd-HHmmss-fff', $invariant, [Globalization.DateTimeStyles]::AssumeUniversal -bor [Globalization.DateTimeStyles]::AdjustToUniversal)
                $family = $match.Groups['family'].Value
                $suffix = $match.Groups['suffix'].Value
                $run.Decision = 'pending'; $run.Reason = ''
                if ($family -ceq 'workshop-local' -and !$suffix -and $area -ne 'build') { $run.Kind = 'release'; $run.Group = 'release' }
                elseif ($family -ceq 'local' -and $area -eq 'build') { $run.Kind = 'build'; $run.Group = "build/local$suffix" }
                else { $run.Kind = 'run'; $run.Group = "$area/$family$suffix" }
            } catch { $run.Stamp = $null }
        }
        $runs.Add($run)
    }
}
# Receipts live in the artifacts/ half; read them only from a real (unlinked) folder.
$artifactRuns = @{}
foreach ($run in $runs | Where-Object { $_.Kind -eq 'release' -and $_.Area -eq 'artifacts' }) { $artifactRuns[$run.Name] = $run.Info }
foreach ($run in $runs | Where-Object Kind -eq 'release') {
    $artifact = $artifactRuns[$run.Name]
    if (!$artifact) { continue }
    $receipt = Read-JsonFile (Join-Path $artifact.FullName 'workshop-receipt.json')
    if ($receipt) {
        $run.Item = [string]$receipt.item
        $run.Published = $receipt.uploaded -eq $true -and $receipt.packageVerified -eq $true
        $run.Version = ConvertTo-ReleaseVersion $receipt.version
    }
    if (!$run.Version) { $staged = Read-JsonFile (Join-Path $artifact.FullName 'receipt.json'); if ($staged) { $run.Version = ConvertTo-ReleaseVersion $staged.version } }
    if (!$run.Version) {
        foreach ($zip in @($artifact.EnumerateFiles('*_source.zip', $enumeration))) {
            $zipVersion = [regex]::Match($zip.Name, '_(\d+\.\d+\.\d+)_source\.zip$')
            if ($zipVersion.Success) { $run.Version = ConvertTo-ReleaseVersion $zipVersion.Groups[1].Value; break }
        }
    }
}
if ($PublishedRun -and !@($runs | Where-Object { $_.Kind -eq 'release' -and $_.Name -eq $PublishedRun -and $_.Published }).Count) {
    Write-Host "Release housekeeping skipped: $PublishedRun has no verified upload receipt."
    if ($PassThru) { $result }
    return
}

function Set-Decision($Run, [string]$Decision, [string]$Reason) { $Run.Decision = $Decision; $Run.Reason = $Reason }
function Get-Activity($Group) {
    # Name stamp or the newest file write (archived.json excluded), across both release halves.
    $latest = [DateTime]::MinValue
    foreach ($run in $Group) {
        if (!$run.Plan) { $run.Plan = Get-SlimPlan $run.Info $false $true }
        if ($run.Stamp -gt $latest) { $latest = $run.Stamp }
        if ($run.Plan.Latest -gt $latest) { $latest = $run.Plan.Latest }
    }
    $latest
}
$margin = $now.AddHours(-$MinAgeHours)
$marginText = [string]::Format($invariant, '{0:0.##} h', $MinAgeHours)

# Release runs: decide per run name so the artifacts/ and .local/ halves stay together.
$releases = @($runs | Where-Object Kind -eq 'release' | Group-Object Name | ForEach-Object {
    $first = $_.Group[0]
    [pscustomobject]@{ Name = $_.Name; Runs = $_.Group; Stamp = $first.Stamp; Item = $first.Item; Version = $first.Version; Published = $first.Published } })
$keepRelease = @{}
$newestVersion = @{}
foreach ($group in @($releases | Where-Object Published | Group-Object Item)) {
    $sorted = @($group.Group | Sort-Object Stamp -Descending)
    # A script block, not ForEach-Object -MemberName: the latter honours -WhatIf and returns nothing.
    $versions = @($group.Group | Where-Object Version | ForEach-Object { $_.Version } | Sort-Object -Descending)
    if ($versions.Count) { $newestVersion[$group.Name] = $versions[0] }
    foreach ($release in @($sorted | Select-Object -First $KeepReleases)) { $keepRelease[$release.Name] = "newest $KeepReleases published release(s) of the item" }
}
# Without a Workshop receipt the item is unknown: superseded only when older than every item's newest version.
$newestEveryItem = if ($newestVersion.Count) { @($newestVersion.Values | Sort-Object)[0] } else { $null }
if ($PublishedRun) { $keepRelease[$PublishedRun] = 'just published' }
foreach ($release in $releases) {
    if ($keepRelease.ContainsKey($release.Name)) { foreach ($run in $release.Runs) { Set-Decision $run 'keep' $keepRelease[$release.Name] }; continue }
    $newest = if ($release.Item) { $newestVersion[$release.Item] } else { $newestEveryItem }
    if ($release.Version -and $newest -and $release.Version -lt $newest) {
        foreach ($run in $release.Runs) { Set-Decision $run 'slim' "superseded: $($release.Version) is older than published $newest" }; continue
    }
    if ((Get-Activity $release.Runs) -ge $margin) { foreach ($run in $release.Runs) { Set-Decision $run 'keep' "modified within $marginText" }; continue }
    foreach ($run in $release.Runs) { Set-Decision $run 'slim' "older than $marginText and not a kept release" }
}

# Build snapshots and test runs: newest per family, the installed build, recent activity.
$lastBuild = $null
try { $lastBuild = [string](Get-Content -LiteralPath (Join-Path $repo '.local/last-build.json') -Raw | ConvertFrom-Json).build } catch { }
# The native slot list of tests/Run-Gameplay.ps1 plus Workbench: any of them may hold a build/ snapshot.
$enginesRunning = $realRepo -and [bool](Get-Process -Name ArmaReforgerSteamDiag, ArmaReforgerSteam, ArmaReforgerServerDiag, ArmaReforgerServer, ArmaReforgerWorkbenchSteamDiag, ArmaReforgerWorkbenchSteam -ErrorAction SilentlyContinue)
foreach ($group in @($runs | Where-Object { $_.Kind -in 'build', 'run' } | Group-Object Group)) {
    $keep = if ($group.Group[0].Kind -eq 'build') { $KeepBuilds } else { $KeepRuns }
    $sorted = @($group.Group | Sort-Object Stamp -Descending)
    for ($index = 0; $index -lt $sorted.Count; $index++) {
        $run = $sorted[$index]
        if ($index -lt $keep) { Set-Decision $run 'keep' "newest $keep of $($group.Name)"; continue }
        if ($run.Kind -eq 'build' -and $lastBuild -and $run.Name -in $lastBuild, "$lastBuild-companion") { Set-Decision $run 'keep' 'installed build (.local/last-build.json)'; continue }
        if ($enginesRunning -and $run.Area -eq 'build') { Set-Decision $run 'keep' 'a native engine is running'; continue }
        if ((Get-Activity @($run)) -ge $margin) { Set-Decision $run 'keep' "modified within $marginText"; continue }
        Set-Decision $run 'slim' "older than $marginText and not among the newest $keep"
    }
}

$slim = @($runs | Where-Object Decision -eq 'slim')
foreach ($run in $slim) {
    if (!$run.Plan) { $run.Plan = Get-SlimPlan $run.Info $false $true }
    foreach ($link in $run.Plan.Links) { $result.Skipped.Add([pscustomobject]@{ Path = [IO.Path]::GetRelativePath($repo, $link).Replace('\', '/'); Detail = 'link; not followed' }) }
    foreach ($move in $run.Plan.Moves) { $result.Planned.Add($move); $result.PlannedBytes += $move.Bytes }
}
foreach ($run in $runs) {
    # Sizes for the summary only; a kept folder that cannot be read is reported without a size.
    if (!$run.Plan) { try { $run.Plan = Get-SlimPlan $run.Info $false $true } catch { Write-Verbose "size unknown: $($run.Path): $($_.Exception.Message)" } }
}
$result.Runs = @($runs | Select-Object Path, Kind, Decision, Reason, Published, @{ n = 'Version'; e = { [string]$_.Version } },
    @{ n = 'Bytes'; e = { if ($_.Plan) { $_.Plan.Bytes } else { $null } } }, @{ n = 'MoveBytes'; e = { if ($_.Decision -eq 'slim') { [long]($_.Plan.Moves | Measure-Object Bytes -Sum).Sum } else { [long]0 } } })
foreach ($run in $runs) { Write-Verbose "$($run.Decision.PadRight(4)) $($run.Path) [$(if ($run.Plan) { Format-Size $run.Plan.Bytes } else { '?' })]: $($run.Reason)" }
$kept = @($runs | Where-Object Decision -eq 'keep')
$keptCount = $kept.Count
$keptBytes = [long](@($kept | Where-Object Plan | ForEach-Object { $_.Plan.Bytes }) | Measure-Object -Sum).Sum
$slimRuns = @($slim | Where-Object { $_.Plan.Moves.Count })
$result | Add-Member -NotePropertyName KeptBytes -NotePropertyValue $keptBytes

if (!$archive) {
    $line = if ($result.PlannedBytes) { "Release housekeeping: $(Format-Size $result.PlannedBytes) of superseded payloads in $($slimRuns.Count) run folder(s) could be archived" } else { 'Release housekeeping: nothing to archive' }
    $hint = $archiveProblem
    if (!$configured -or !$configured.Trim()) { $hint += ' (for example "ArchiveRoot": "G:/EXPBG-archive")' }
    Write-Host "$line; $hint. Nothing was moved or deleted."
    if ($PassThru) { $result }
    return
}

# --- Archive -----------------------------------------------------------------------------------
# Free space of the repository volume (C: on the release PC) and, when different, the archive volume.
$sameVolume = (Get-VolumeKey $repo) -eq (Get-VolumeKey $archive)
$freeBefore = Get-FreeSpace $repo
$archiveFreeBefore = if ($sameVolume) { $null } else { Get-FreeSpace $archive }
$done = 0
foreach ($run in $slimRuns) {
    $items = [Collections.Generic.List[object]]::new()
    $runBytes = [long]($run.Plan.Moves | Measure-Object Bytes -Sum).Sum
    foreach ($move in $run.Plan.Moves) { Write-Verbose "  $([IO.Path]::GetRelativePath($repo, $move.Item.FullName).Replace('\', '/')) ($(Format-Size $move.Bytes))" }
    if (!$PSCmdlet.ShouldProcess("$($run.Path): $($run.Plan.Moves.Count) item(s), $(Format-Size $runBytes) ($($run.Reason))", "Move to archive $archive")) { $done += $run.Plan.Moves.Count; continue }
    foreach ($move in $run.Plan.Moves) {
        $source = $move.Item.FullName
        # Confinement: only content inside build/<run>, artifacts/<run> or .local/workshop-local-<utc>.
        if (!(Test-Under $source $run.Info.FullName)) { throw "Refusing to move outside $($run.Path): $source" }
        $relative = [IO.Path]::GetRelativePath($repo, $source).Replace('\', '/')
        $destination = Get-FullPath (Join-Path $archive $relative)
        if (!(Test-Under $destination $archive)) { throw "Refusing to archive outside $archive`: $relative" }
        $done++
        Write-Progress -Activity "Archiving to $archive" -Status $relative -PercentComplete ([Math]::Min(100, 100 * $done / [Math]::Max(1, $result.Planned.Count)))
        $outcome = Move-Verified $move.Item $destination
        $entry = [pscustomobject]@{ Path = $relative; Files = $move.Files; Bytes = $move.Bytes; Method = $outcome.Method; Detail = $outcome.Detail }
        switch ($outcome.Status) {
            'moved' { $result.Moved.Add($entry); $result.MovedBytes += $move.Bytes; $items.Add([ordered]@{ path = [IO.Path]::GetRelativePath($run.Info.FullName, $source).Replace('\', '/'); files = $move.Files; bytes = $move.Bytes; method = $outcome.Method }) }
            'failed' { $result.Failed.Add($entry); Write-Warning "Release housekeeping: $relative not archived: $($outcome.Detail)" }
            default { $result.Skipped.Add($entry) }
        }
    }
    if ($items.Count) {
        $archiveRun = Join-Path $archive $run.Path
        $archiveEvent = [ordered]@{ archivedUtc = $now.ToString('o'); reason = $run.Reason; source = $run.Info.FullName; archive = $archiveRun; items = @($items) }
        # Pointer beside the kept evidence (role 'source'), and the archive copy's record that -Purge ages.
        foreach ($side in @(@($run.Info.FullName, 'source'), @($archiveRun, 'archive'))) {
            $directory = $side[0]
            try { Add-ArchiveRecord $directory $side[1] $archiveEvent } catch { Write-Warning "Release housekeeping: could not write archived.json in $directory`: $((Get-InnerException $_.Exception).Message)" }
        }
    }
}
Write-Progress -Activity "Archiving to $archive" -Completed
$keptText = "$keptCount run folder(s) in full ($(Format-Size $keptBytes))"
if ($WhatIfPreference) {
    Write-Host "Release housekeeping (WhatIf): would move $($result.Planned.Count) item(s), $(Format-Size $result.PlannedBytes), from $($slimRuns.Count) run folder(s) to $archive; would keep $keptText; evidence stays in place. Nothing changed."
    $delta = if ($sameVolume) { 0 } else { $result.PlannedBytes }
    $space = @(
        if ($freeBefore) { Format-SpaceChange '' $freeBefore ($freeBefore.Free + $delta) $true }
        # [long] operands: an Int32 literal would pick Math.Max(int, int) and overflow on real disks.
        if ($archiveFreeBefore) { Format-SpaceChange 'archive ' $archiveFreeBefore ([Math]::Max([long]0, [long]$archiveFreeBefore.Free - [long]$delta)) $true }
    )
} else {
    Write-Host "Release housekeeping: moved $($result.Moved.Count) item(s), $(Format-Size $result.MovedBytes), from $($slimRuns.Count) run folder(s) to $archive; kept $keptText; receipts, manifests, notes and logs stay in place."
    $freeAfter = Get-FreeSpace $repo
    $archiveFreeAfter = if ($sameVolume) { $null } else { Get-FreeSpace $archive }
    $space = @(
        if ($freeBefore -and $freeAfter) { Format-SpaceChange '' $freeBefore $freeAfter.Free $false }
        if ($archiveFreeBefore -and $archiveFreeAfter) { Format-SpaceChange 'archive ' $archiveFreeBefore $archiveFreeAfter.Free $false }
    )
}
if ($sameVolume -and $freeBefore) { $space = @($space) + 'the archive is on the same volume, so moving frees no space there' }
if (@($space).Count) { Write-Host "Free space: $(@($space) -join '; ')." }
if ($result.Failed.Count) { Write-Host "Release housekeeping: $($result.Failed.Count) item(s) not archived; their sources are unchanged unless noted above." }
$links = @($result.Skipped | Where-Object Detail -like 'link*')
if ($links.Count) { Write-Host "Release housekeeping: skipped $($links.Count) link(s): $(@($links.Path) -join ', ')" }
if ($PassThru) { $result }
} finally {
    if ($mutex) {
        try { $mutex.ReleaseMutex() } catch { }
        $mutex.Dispose()
    }
}

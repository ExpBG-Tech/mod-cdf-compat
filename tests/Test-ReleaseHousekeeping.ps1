#requires -Version 7.0
# Portable guard for tools/Invoke-ReleaseHousekeeping.ps1 on fake run folders: keep/move rules,
# evidence in place, nothing deleted without -Purge, purge limited to the archive, links skipped,
# failed copies leave the source intact, and release.ps1 calls it (through tools/Invoke-Cleanup.ps1)
# only after a verified publish. Delete mode: tests/Test-Cleanup.ps1.
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$tool = Join-Path $repo 'tools/Invoke-ReleaseHousekeeping.ps1'
. "$repo/tools/Workshop-Common.ps1"
# Outside this checkout: the tool refuses an archive inside any Git checkout.
$temp = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$root = Join-Path $temp ('expbg-housekeeping-test-' + [guid]::NewGuid().ToString('N'))
$fake = Join-Path $root 'mod-fake'
$archiveBase = Join-Path $root 'archive'
$archive = Join-Path $archiveBase 'mod-fake'
$outside = Join-Path $root 'outside'
$item = 'A1B2C3D4E5F60718'
$enumeration = [IO.EnumerationOptions]@{ RecurseSubdirectories = $false; AttributesToSkip = [IO.FileAttributes]0 }

function Assert([bool]$Condition, [string]$Message) { if (!$Condition) { throw "FAIL: $Message" } }
function Test-Link($Entry) { [bool]($Entry.Attributes -band [IO.FileAttributes]::ReparsePoint) -or $null -ne $Entry.LinkTarget }
function Get-Stamp([double]$HoursAgo) { [DateTime]::UtcNow.AddHours(-$HoursAgo).ToString('yyyyMMdd-HHmmss-fff') }
function New-Run([string]$Base, [string]$Relative, [double]$HoursAgo, [hashtable]$Files) {
    # Files: relative path -> text content or byte count. Times match the run so age rules apply.
    $time = [DateTime]::UtcNow.AddHours(-$HoursAgo)
    foreach ($name in $Files.Keys) {
        $path = Join-Path $Base "$Relative/$name"
        New-Item -ItemType Directory -Path (Split-Path -Parent $path) -Force | Out-Null
        if ($Files[$name] -is [string]) { [IO.File]::WriteAllText($path, $Files[$name]) }
        else { $data = [byte[]]::new([int]$Files[$name]); [Random]::new([int]$Files[$name]).NextBytes($data); [IO.File]::WriteAllBytes($path, $data) }
        [IO.File]::SetLastWriteTimeUtc($path, $time)
    }
}
function Get-Inventory([string]$Base) {
    # Relative path -> size for every file below Base, never following links.
    $map = @{}
    if (!(Test-Path -LiteralPath $Base -PathType Container)) { return $map }
    $stack = [Collections.Generic.Stack[IO.DirectoryInfo]]::new()
    $stack.Push([IO.DirectoryInfo]::new($Base))
    while ($stack.Count) {
        foreach ($entry in $stack.Pop().EnumerateFileSystemInfos('*', $enumeration)) {
            if (Test-Link $entry) { continue }
            if ($entry -is [IO.FileInfo]) { $map[[IO.Path]::GetRelativePath($Base, $entry.FullName).Replace('\', '/')] = $entry.Length } else { $stack.Push($entry) }
        }
    }
    $map
}
function Get-Combined([string]$Repository, [string]$Archive) {
    # Repository plus archive mapped back to repository paths; housekeeping records and the
    # test-edited local configuration excluded.
    $all = @{}
    foreach ($entry in (Get-Inventory $Repository).GetEnumerator()) { if ($entry.Key -notmatch '(^|/)archived[^/]*\.json$' -and $entry.Key -ne '.local/config.json') { $all[$entry.Key] = $entry.Value } }
    foreach ($entry in (Get-Inventory $Archive).GetEnumerator()) {
        if ($entry.Key -match '(^|/)archived[^/]*\.json$') { continue }
        Assert (!$all.ContainsKey($entry.Key)) "$($entry.Key) exists in both the repository and the archive"
        $all[$entry.Key] = $entry.Value
    }
    $all
}
function Assert-Same($Before, $After, [string]$Message) {
    $changed = @($Before.Keys | Where-Object { !$After.ContainsKey($_) -or $After[$_] -ne $Before[$_] })
    $extra = @($After.Keys | Where-Object { !$Before.ContainsKey($_) })
    Assert (!$changed.Count -and !$extra.Count) "$Message (missing/changed: $($changed -join ', '); new: $($extra -join ', '))"
}
function New-Link([string]$Path, [string]$Target) {
    New-Item -ItemType Directory -Path $Target, (Split-Path -Parent $Path) -Force | Out-Null
    if ($IsWindows) { New-Item -ItemType Junction -Path $Path -Target $Target | Out-Null }
    else { New-Item -ItemType SymbolicLink -Path $Path -Target $Target | Out-Null }
}
function Remove-Links([string]$Base) {
    # Unlink every junction/symlink first so cleanup can never reach a link target.
    $stack = [Collections.Generic.Stack[IO.DirectoryInfo]]::new()
    $stack.Push([IO.DirectoryInfo]::new($Base))
    while ($stack.Count) {
        foreach ($entry in $stack.Pop().EnumerateFileSystemInfos('*', $enumeration)) {
            if (Test-Link $entry) { if ($IsWindows -and $entry -is [IO.DirectoryInfo]) { [IO.Directory]::Delete($entry.FullName, $false) } else { [IO.File]::Delete($entry.FullName) } }
            elseif ($entry -is [IO.DirectoryInfo]) { $stack.Push($entry) }
        }
    }
}
$script:messages = @()
function Invoke-Housekeeping([string]$Repository, [hashtable]$Arguments = @{}) {
    $output = @(& $tool -RepoRoot $Repository -PassThru -Confirm:$false -WarningAction SilentlyContinue @Arguments 6>&1)
    $script:messages = @($output | Where-Object { $_ -is [Management.Automation.InformationRecord] } | ForEach-Object { [string]$_.MessageData })
    $result = @($output | Where-Object { $_ -isnot [Management.Automation.InformationRecord] })
    Assert ($result.Count -eq 1) 'Housekeeping returns one result object with -PassThru.'
    $result[0]
}
function Set-Config([string]$Repository, $Value) {
    New-Item -ItemType Directory -Path "$Repository/.local" -Force | Out-Null
    $config = [ordered]@{}
    if ($null -ne $Value) { $config.ArchiveRoot = $Value }
    $config | ConvertTo-Json | Set-Content -LiteralPath "$Repository/.local/config.json"
}
function Get-Decision($Result, [string]$Path) { @($Result.Runs | Where-Object Path -eq $Path)[0] }
function Assert-InRepo([string]$Relative) { Assert ((Test-Path -LiteralPath "$fake/$Relative") -and !(Test-Path -LiteralPath "$archive/$Relative")) "$Relative stays in the repository" }
function Assert-InArchive([string]$Relative) { Assert (!(Test-Path -LiteralPath "$fake/$Relative") -and (Test-Path -LiteralPath "$archive/$Relative")) "$Relative moves to the archive" }

try {
    # --- Fixture: release pairs, builds, test runs, links and untouched .local content -----------
    $releases = [ordered]@{}
    $versions = @{}
    # R4a/R4b are receipt-less attempts of the published 0.1.2 (staging only; R4a without receipt.json,
    # so its version comes from the source zip name): same version, so only the age rule applies.
    foreach ($spec in @(
            @{ Key = 'R0'; Hours = 300; Version = '0.0.9'; Receipt = $null },
            @{ Key = 'R1'; Hours = 240; Version = '0.1.0'; Receipt = @{ uploadAttempted = $true; uploaded = $true; packageVerified = $true } },
            @{ Key = 'R2'; Hours = 120; Version = '0.1.1'; Receipt = @{ uploadAttempted = $true; uploaded = $false; packageVerified = $false } },
            @{ Key = 'R4b'; Hours = 30; Version = '0.1.2'; Receipt = $null },
            @{ Key = 'R3'; Hours = 3; Version = '0.1.1'; Receipt = @{ uploadAttempted = $true; uploaded = $true; packageVerified = $true } },
            @{ Key = 'R4a'; Hours = 2; Version = '0.1.2'; Receipt = $null; NoStageReceipt = $true },
            @{ Key = 'R4'; Hours = 1; Version = '0.1.2'; Receipt = @{ uploadAttempted = $true; uploaded = $true; packageVerified = $true } },
            @{ Key = 'R5'; Hours = 0.5; Version = '0.1.3'; Receipt = @{ uploadAttempted = $false; uploaded = $false; packageVerified = $false } })) {
        $name = 'workshop-local-' + (Get-Stamp $spec.Hours)
        $releases[$spec.Key] = $name
        $versions[$spec.Key] = $spec.Version
        $files = @{
            'receipt.json' = (@{ version = $spec.Version } | ConvertTo-Json); 'source-manifest.json' = '[]'
            'Assembled/Fake_Addon/Fake_Addon.gproj' = 'GameProject { }'; 'Assembled/Fake_Addon/Scripts/a.c' = 2048
            'Stage/Fake_Addon/Fake_Addon.gproj' = 'GameProject { }'; 'Stage/Fake_Addon/Configs/x.json' = '{}'; 'Stage/LICENSE' = 'license'
            "Fake_Addon_$($spec.Version)_source.zip" = 4096
        }
        if ($spec.NoStageReceipt) { $files.Remove('receipt.json'); $files.Remove('source-manifest.json') }
        if ($spec.Receipt) {
            $receipt = @{ version = $spec.Version; item = $item } + $spec.Receipt
            $files['workshop-receipt.json'] = $receipt | ConvertTo-Json
            New-Run $fake ".local/$name" $spec.Hours @{
                'change-note.txt' = 'notes'; 'build-output.log' = 'ok'; 'build-engine-logs/console.log' = 'console'
                'build-engine-logs/resourceDatabase.rdb' = 81; 'profile/profile/state.bin' = 42
                'packed/manifest.json' = '{"version":1}'; 'packed/Fake_Addon.gproj' = 'GameProject { }'; 'packed/data.pak' = 8192
                'packed/resourceDatabase.rdb' = 512; 'packed/previewImage.png' = 300
                'built/Fake_Addon.gproj' = 'GameProject { }'; 'built/Scripts/b.c' = 1024
                'dependencies/0123456789ABCDEF/Dep.gproj' = 'GameProject { }'; 'dependencies/0123456789ABCDEF/data.pak' = 2048
                'dependencies.json' = '[]'
            }
            New-Run $fake '.local' $spec.Hours @{ "$name-notes.md" = 'release notes' }
        }
        New-Run $fake "artifacts/$name" $spec.Hours $files
    }
    $builds = @{}
    foreach ($spec in @(@{ Key = 'L1'; Hours = 300 }, @{ Key = 'L2'; Hours = 200 }, @{ Key = 'L3'; Hours = 100 })) {
        $builds[$spec.Key] = 'local-' + (Get-Stamp $spec.Hours)
        New-Run $fake "build/$($builds[$spec.Key])" $spec.Hours @{
            'receipt.json' = '{}'; 'source-manifest.json' = '[]'; 'dependencies.json' = '[]'
            'Fake_Addon/Fake_Addon.gproj' = 'GameProject { }'; 'Fake_Addon/Scripts/c.c' = 1024
            'PC/Fake_Addon.gproj' = 'GameProject { }'; 'PC/data.bin' = 4096; 'profile-data/logs/console.log' = 'log'
            'dependencies/0123456789ABCDEF/Dep.gproj' = 'GameProject { }'
        }
    }
    $gameplay = @{}
    foreach ($index in 1..5) {
        $gameplay[$index] = 'gameplay-' + (Get-Stamp (160 - 10 * $index))
        New-Run $fake "build/$($gameplay[$index])" (160 - 10 * $index) @{
            'run.json' = '{}'; 'result.json' = '{"passed":true}'; 'inputs.json' = '[]'; 'native-output.log' = 'out'; 'logs/console.log' = 'log'
            'addons/Fixture/Fixture.gproj' = 'GameProject { }'; 'addons/Fixture/f.c' = 1024
        }
    }
    New-Run $fake "build/$($gameplay[1])" 150 @{ 'profile/big.bin' = 1200000 }
    New-Run $outside 'linked-target' 1 @{ 'payload.pak' = 4096 }
    New-Link "$fake/build/$($gameplay[1])/linked" "$outside/linked-target"
    $art = @{}
    foreach ($index in 1..4) {
        $art[$index] = 'art-' + (Get-Stamp $index)
        New-Run $fake "build/$($art[$index])" $index @{ 'Fake_Addon/Fake_Addon.gproj' = 'GameProject { }'; 'Fake_Addon/UI/t.edds' = 2048; 'result.json' = '{}' }
    }
    New-Run $fake 'build/manual-scratch' 500 @{ 'big.bin' = 2048 }
    $linkedRun = 'contracts-' + (Get-Stamp 400)
    New-Run $outside 'contracts-target' 400 @{ 'Fake/Fake.gproj' = 'GameProject { }'; 'Fake/data.pak' = 4096 }
    New-Link "$fake/build/$linkedRun" "$outside/contracts-target"
    New-Run $fake '.local' 500 @{ 'e2e/modset.json' = '{}'; 'art/card.png' = 2048; 'scratch-run-20200101-000000-000/big.bin' = 2048 }
    @{ build = $builds.L2 } | ConvertTo-Json | Set-Content -LiteralPath "$fake/.local/last-build.json"
    Set-Config $fake $null
    $original = Get-Combined $fake $archive
    $outsideBefore = Get-Inventory $outside

    # --- Without ArchiveRoot: plan only, one-line hint, nothing moved ----------------------------
    $result = Invoke-Housekeeping $fake
    Assert ($null -eq $result.Archive -and !$result.Moved.Count) 'No archive is used without ArchiveRoot.'
    Assert (@($script:messages | Where-Object { $_ -match 'ArchiveRoot is not set' -and $_ -match 'Nothing was moved or deleted' }).Count -eq 1) 'A one-line ArchiveRoot hint is printed.'
    Assert-Same $original (Get-Combined $fake $archive) 'Nothing changes without ArchiveRoot'
    Assert (!(Test-Path -LiteralPath $archiveBase)) 'No archive folder is created without ArchiveRoot.'
    $expected = [ordered]@{
        "artifacts/$($releases.R0)" = 'slim'; "artifacts/$($releases.R1)" = 'slim'; ".local/$($releases.R1)" = 'slim'
        "artifacts/$($releases.R2)" = 'slim'; "artifacts/$($releases.R3)" = 'slim'; ".local/$($releases.R3)" = 'slim'
        "artifacts/$($releases.R4)" = 'keep'; ".local/$($releases.R4)" = 'keep'; "artifacts/$($releases.R5)" = 'keep'; ".local/$($releases.R5)" = 'keep'
        "artifacts/$($releases.R4a)" = 'keep'; "artifacts/$($releases.R4b)" = 'slim'
        "build/$($builds.L1)" = 'slim'; "build/$($builds.L2)" = 'keep'; "build/$($builds.L3)" = 'keep'
        "build/$($gameplay[1])" = 'slim'; "build/$($gameplay[2])" = 'slim'; "build/$($gameplay[3])" = 'keep'; "build/$($gameplay[4])" = 'keep'; "build/$($gameplay[5])" = 'keep'
        "build/$($art[1])" = 'keep'; "build/$($art[2])" = 'keep'; "build/$($art[3])" = 'keep'; "build/$($art[4])" = 'keep'
        'build/manual-scratch' = 'keep'
    }
    foreach ($path in $expected.Keys) {
        $decision = Get-Decision $result $path
        Assert ($decision -and $decision.Decision -eq $expected[$path]) "$path should be '$($expected[$path])', got '$($decision.Decision)' ($($decision.Reason))"
    }
    Assert ((Get-Decision $result "artifacts/$($releases.R3)").Reason -match 'superseded: 0\.1\.1 is older than published 0\.1\.2') 'A superseded release payload of an older version is slimmed even within the age margin.'
    Assert ((Get-Decision $result "artifacts/$($releases.R0)").Reason -match 'superseded: 0\.0\.9') 'A receipt-less release run of an older version is superseded.'
    Assert ((Get-Decision $result "artifacts/$($releases.R4a)").Reason -match 'modified within' -and (Get-Decision $result "artifacts/$($releases.R4a)").Version -eq '0.1.2') 'A recent attempt of the published version is not superseded (version from the zip name).'
    Assert ((Get-Decision $result "artifacts/$($releases.R4b)").Reason -match 'older than 24 h') 'An old attempt of the published version follows the age rule.'
    Assert ((Get-Decision $result "artifacts/$($releases.R4)").Bytes -gt 0) 'Run sizes are reported.'
    $plan = @($result.Runs | ForEach-Object { "$($_.Path)=$($_.Decision):$($_.Reason)" })
    Assert ((Get-Decision $result "build/$($builds.L2)").Reason -match 'last-build') 'The installed build snapshot stays.'
    Assert ((Get-Decision $result "build/$($art[4])").Reason -match 'modified within') 'Recent runs beyond the newest N stay.'
    Assert (!(Get-Decision $result '.local/e2e') -and !(Get-Decision $result '.local/scratch-run-20200101-000000-000')) 'Only .local/workshop-local-* folders are candidates.'
    Assert (@($result.Skipped | Where-Object { $_.Path -eq "build/$linkedRun" -and $_.Detail -like 'link*' }).Count -eq 1) 'A linked run folder is skipped.'

    # --- Unusable archive locations and the publish guard move nothing ---------------------------
    Set-Config $fake 'D:/replace-with-your-archive-drive/EXPBG-archive'
    $result = Invoke-Housekeeping $fake
    Assert ($null -eq $result.Archive -and $result.ArchiveProblem -match 'placeholder' -and !$result.Moved.Count) 'The example placeholder is not used as an archive.'
    $result = Invoke-Housekeeping $fake @{ ArchiveRoot = (Join-Path $fake 'archive-inside') }
    Assert ($null -eq $result.Archive -and $result.ArchiveProblem -match 'outside this repository') 'An archive inside the repository is refused.'
    $result = Invoke-Housekeeping $fake @{ ArchiveRoot = 'relative/archive' }
    Assert ($null -eq $result.Archive -and $result.ArchiveProblem -match 'absolute') 'A relative archive path is refused.'
    New-Item -ItemType Directory -Path "$root/checkout/.git" -Force | Out-Null
    $result = Invoke-Housekeeping $fake @{ ArchiveRoot = "$root/checkout/archive" }
    Assert ($null -eq $result.Archive -and $result.ArchiveProblem -match 'Git checkout' -and !(Test-Path -LiteralPath "$root/checkout/archive")) 'An archive inside a Git checkout (another clone, or this one by an alias path) is refused.'
    $result = Invoke-Housekeeping $fake @{ ArchiveRoot = $archiveBase; PublishedRun = $releases.R2 }
    Assert (!$result.Moved.Count -and @($script:messages | Where-Object { $_ -match 'no verified upload receipt' }).Count) 'An unverified publication skips housekeeping.'
    $result = Invoke-Housekeeping $fake @{ ArchiveRoot = $archiveBase; WhatIf = $true }
    Assert ($result.Planned.Count -gt 0 -and !$result.Moved.Count) 'WhatIf plans moves without moving.'
    $whatIfPlan = @($result.Runs | ForEach-Object { "$($_.Path)=$($_.Decision):$($_.Reason)" })
    Assert (($whatIfPlan -join '|') -ceq ($plan -join '|')) "WhatIf decides exactly like a real run (got $($whatIfPlan -join '; '))."
    Assert (@($script:messages | Where-Object { $_ -match '^Release housekeeping \(WhatIf\): would move \d+ item\(s\), .+ to .+; would keep \d+ run folder\(s\) in full \(.+\)' }).Count -eq 1) 'WhatIf prints the move/keep summary.'
    Assert (@($script:messages | Where-Object { $_ -match '^Free space: .+' }).Count -eq 1) 'WhatIf prints free space.'
    Assert-Same $original (Get-Combined $fake $archive) 'WhatIf changes nothing'
    Assert (!(Test-Path -LiteralPath $archiveBase)) 'WhatIf creates no archive folder.'
    # Archive on another fixed drive (WhatIf only, so nothing is written there): both volumes are
    # summarized with real free-space numbers. Skipped on single-volume machines.
    if ($IsWindows) {
        $repoVolume = [IO.Path]::GetPathRoot([IO.Path]::GetFullPath($fake))
        $other = @([IO.DriveInfo]::GetDrives() | Where-Object { $_.DriveType -eq [IO.DriveType]::Fixed -and $_.IsReady -and $_.RootDirectory.FullName -ne $repoVolume } | Select-Object -First 1)
        if ($other.Count) {
            $otherBase = Join-Path $other[0].RootDirectory.FullName ('expbg-housekeeping-test-' + [guid]::NewGuid().ToString('N'))
            $result = Invoke-Housekeeping $fake @{ ArchiveRoot = $otherBase; WhatIf = $true }
            Assert (!$result.Moved.Count -and !(Test-Path -LiteralPath $otherBase)) 'A cross-volume WhatIf writes nothing on the archive drive.'
            Assert (@($script:messages | Where-Object { $_ -match '^Free space: \S+ .+ now, about .+ after; archive \S+ .+ now, about .+ after\.$' }).Count -eq 1) "A cross-volume WhatIf summarizes both volumes ($($script:messages -join ' / '))."
        }
    }

    # --- Archive from .local/config.json ----------------------------------------------------------
    Set-Config $fake $archiveBase
    $result = Invoke-Housekeeping $fake
    Assert ($result.Archive -eq [IO.Path]::GetFullPath($archive)) 'The archive is <ArchiveRoot>/<repository folder>.'
    Assert (!$result.Failed.Count -and $result.Moved.Count -gt 0) "Archive moves succeed ($(@($result.Failed.Detail) -join '; '))."
    Assert (@($result.Moved | Where-Object Method -ne 'rename').Count -eq 0) 'Same-volume moves are renames.'
    Assert (@($script:messages | Where-Object { $_ -match '^Release housekeeping: moved \d+ item\(s\), .+ to .+; kept \d+ run folder\(s\) in full' }).Count -eq 1) 'A real run prints the moved/kept summary.'
    Assert (@($script:messages | Where-Object { $_ -match '^Free space: .+same volume' }).Count -eq 1) 'A real run prints free space (same-volume archive noted).'
    Assert-Same $original (Get-Combined $fake $archive) 'Archiving loses or duplicates nothing'
    Assert-Same $outsideBefore (Get-Inventory $outside) 'Link targets are untouched'
    foreach ($heavy in 'Assembled', 'Stage', "Fake_Addon_0.1.2_source.zip") { Assert-InRepo "artifacts/$($releases.R4a)/$heavy" }
    foreach ($key in 'R0', 'R1', 'R2', 'R3', 'R4b') {
        $run = "artifacts/$($releases[$key])"
        foreach ($heavy in 'Assembled', 'Stage', "Fake_Addon_$($versions[$key])_source.zip") { Assert-InArchive "$run/$heavy" }
        Assert-InRepo "$run/receipt.json"; Assert-InRepo "$run/source-manifest.json"
        Assert ((Test-Path -LiteralPath "$fake/$run/archived.json") -and (Test-Path -LiteralPath "$archive/$run/archived.json")) "$run records where its payload went"
    }
    foreach ($key in 'R1', 'R2', 'R3') {
        Assert-InRepo "artifacts/$($releases[$key])/workshop-receipt.json"
        $private = ".local/$($releases[$key])"
        foreach ($heavy in 'packed/data.pak', 'packed/Fake_Addon.gproj', 'packed/resourceDatabase.rdb', 'packed/previewImage.png', 'built', 'dependencies') { Assert-InArchive "$private/$heavy" }
        foreach ($evidence in 'packed/manifest.json', 'change-note.txt', 'build-output.log', 'build-engine-logs/console.log', 'build-engine-logs/resourceDatabase.rdb', 'profile/profile/state.bin', 'dependencies.json') { Assert-InRepo "$private/$evidence" }
        Assert-InRepo ".local/$($releases[$key])-notes.md"
    }
    foreach ($key in 'R4', 'R5') {
        foreach ($half in "artifacts/$($releases[$key])", ".local/$($releases[$key])") {
            Assert (!(Test-Path -LiteralPath "$archive/$half") -and !(Test-Path -LiteralPath "$fake/$half/archived.json")) "$half stays complete"
        }
        foreach ($heavy in 'Stage/Fake_Addon/Configs/x.json', 'Assembled/Fake_Addon/Scripts/a.c', "Fake_Addon_$($versions[$key])_source.zip") { Assert-InRepo "artifacts/$($releases[$key])/$heavy" }
        foreach ($heavy in 'packed/data.pak', 'built/Scripts/b.c', 'dependencies/0123456789ABCDEF/data.pak') { Assert-InRepo ".local/$($releases[$key])/$heavy" }
    }
    foreach ($heavy in 'Fake_Addon', 'PC', 'dependencies') { Assert-InArchive "build/$($builds.L1)/$heavy" }
    foreach ($evidence in 'receipt.json', 'source-manifest.json', 'dependencies.json', 'profile-data/logs/console.log') { Assert-InRepo "build/$($builds.L1)/$evidence" }
    Assert-InRepo "build/$($builds.L2)/PC/data.bin"; Assert-InRepo "build/$($builds.L3)/Fake_Addon/Scripts/c.c"
    foreach ($index in 1, 2) {
        Assert-InArchive "build/$($gameplay[$index])/addons"
        foreach ($evidence in 'run.json', 'result.json', 'inputs.json', 'native-output.log', 'logs/console.log') { Assert-InRepo "build/$($gameplay[$index])/$evidence" }
    }
    Assert-InArchive "build/$($gameplay[1])/profile/big.bin"
    Assert ((Test-Link (Get-Item -LiteralPath "$fake/build/$($gameplay[1])/linked" -Force)) -and !(Test-Path -LiteralPath "$archive/build/$($gameplay[1])/linked")) 'A link inside a slimmed run stays in place.'
    Assert ((Test-Link (Get-Item -LiteralPath "$fake/build/$linkedRun" -Force)) -and !(Test-Path -LiteralPath "$archive/build/$linkedRun")) 'A linked run folder is never followed.'
    foreach ($index in 3, 4, 5) { Assert-InRepo "build/$($gameplay[$index])/addons/Fixture/f.c" }
    foreach ($index in 1..4) { Assert-InRepo "build/$($art[$index])/Fake_Addon/UI/t.edds" }
    foreach ($kept in 'build/manual-scratch/big.bin', '.local/e2e/modset.json', '.local/art/card.png', '.local/scratch-run-20200101-000000-000/big.bin', '.local/config.json', '.local/last-build.json') { Assert-InRepo $kept }
    # Release guards still read every retained receipt.
    $rejected = $false
    try { Assert-NoWorkshopUploadAttempt $fake '0.1.1' $item } catch { $rejected = $true }
    Assert $rejected 'The retired-version guard still sees archived runs.'
    Assert-NoWorkshopUploadAttempt $fake '0.1.9' $item

    # Idempotent: a second run has nothing left to move.
    $result = Invoke-Housekeeping $fake
    Assert (!$result.Moved.Count -and !$result.Failed.Count) 'A second run moves nothing.'
    Assert-Same $original (Get-Combined $fake $archive) 'A second run changes nothing'

    # --- Cross-volume path: copy, verify, roll back on failure, then remove the source -----------
    $copyRepo = Join-Path $root 'mod-copy'
    $copyArchive = Join-Path $archiveBase 'mod-copy'
    $copyRun = 'build/gameplay-' + (Get-Stamp 100)
    # The nested file makes source, staging and archive paths longer than 260 characters.
    $deep = 'addons/Fixture/' + ((1..4 | ForEach-Object { 'd' * 60 }) -join '/') + '/deep.c'
    New-Run $copyRepo $copyRun 100 @{ 'result.json' = '{}'; 'addons/Fixture/Fixture.gproj' = 'GameProject { }'; 'addons/Fixture/f1.c' = 1024; 'addons/Fixture/f2.c' = 2048; $deep = 512 }
    Assert ("$copyArchive/$copyRun/$deep".Length -gt 260) 'The fixture exercises long paths.'
    $copyOriginal = Get-Inventory $copyRepo
    $copyArguments = @{ ArchiveRoot = $archiveBase; KeepRuns = 0; ForceCopy = $true; VerifyHash = $true }
    New-Item -ItemType Directory -Path $copyArchive -Force | Out-Null
    'blocks the archive folder' | Set-Content -LiteralPath "$copyArchive/build"
    $result = Invoke-Housekeeping $copyRepo $copyArguments
    Assert ($result.Failed.Count -eq 1 -and !$result.Moved.Count) 'An unwritable archive fails the move.'
    Assert-Same $copyOriginal (Get-Inventory $copyRepo) 'A failed archive write leaves the source intact'
    Remove-Item -LiteralPath "$copyArchive/build"
    # A junction inside the archive would redirect the payload (possibly to another volume).
    New-Link "$copyArchive/build" "$outside/archive-redirect"
    $result = Invoke-Housekeeping $copyRepo $copyArguments
    Assert (!$result.Moved.Count -and !$result.Failed.Count -and @($result.Skipped | Where-Object Detail -match 'link in the archive path').Count -eq 1) 'A link inside the archive is never followed.'
    Assert-Same $copyOriginal (Get-Inventory $copyRepo) 'A linked archive path leaves the source intact'
    Assert (!(Get-Inventory "$outside/archive-redirect").Count) 'Nothing is written through an archive link.'
    Remove-Links $copyArchive
    $result = Invoke-Housekeeping $copyRepo ($copyArguments + @{ TestFailCopyAfter = 1 })
    Assert ($result.Failed.Count -eq 1 -and $result.Failed[0].Detail -match 'Source kept' -and !$result.Moved.Count) 'A failed copy is reported.'
    Assert-Same $copyOriginal (Get-Inventory $copyRepo) 'A failed copy leaves the source intact'
    Assert (!(Get-Inventory $copyArchive).Count -and !@(Get-ChildItem -LiteralPath $copyArchive -Recurse -Force | Where-Object Name -like '*.incomplete-*').Count) 'A failed copy is rolled back in the archive.'
    if ($IsWindows) {
        $lock = [IO.File]::Open("$copyRepo/$copyRun/addons/Fixture/f2.c", 'Open', 'Read', 'None')
        try { $result = Invoke-Housekeeping $copyRepo $copyArguments } finally { $lock.Dispose() }
        Assert ($result.Failed.Count -eq 1 -and !$result.Moved.Count) 'A locked file fails the copy.'
        Assert-Same $copyOriginal (Get-Inventory $copyRepo) 'A locked file leaves the source intact'
        Assert (!(Get-Inventory $copyArchive).Count) 'A locked-file copy is rolled back.'
        # Open for reading by another process: the copy succeeds, but the source is never half deleted.
        $lock = [IO.File]::Open("$copyRepo/$copyRun/addons/Fixture/f2.c", 'Open', 'Read', 'Read')
        try { $result = Invoke-Housekeeping $copyRepo $copyArguments } finally { $lock.Dispose() }
        Assert ($result.Failed.Count -eq 1 -and $result.Failed[0].Detail -match 'source in use' -and !$result.Moved.Count) "A source in use is not removed ($(@($result.Failed.Detail) -join '; '))."
        Assert-Same $copyOriginal (Get-Inventory $copyRepo) 'A source in use stays intact'
        Assert (!(Get-Inventory $copyArchive).Count -and !@(Get-ChildItem -LiteralPath $copyRepo -Recurse -Force | Where-Object Name -like '*.archived-*').Count) 'The copy of a source in use is withdrawn and nothing is left renamed.'
    }
    $result = Invoke-Housekeeping $copyRepo $copyArguments
    Assert ($result.Moved.Count -eq 1 -and $result.Moved[0].Method -eq 'copy' -and !$result.Failed.Count) "A verified copy completes the move ($(@($result.Failed.Detail) + @($result.Skipped.Detail) -join '; '))."
    Assert-Same $copyOriginal (Get-Combined $copyRepo $copyArchive) 'The verified copy matches the source'
    Assert ((Test-Path -LiteralPath "$copyRepo/$copyRun/result.json") -and !(Test-Path -LiteralPath "$copyRepo/$copyRun/addons")) 'Only the verified payload source is removed.'

    # --- Purge: explicit, confirmed, archive only, never through links ---------------------------
    $old = [DateTime]::UtcNow.AddDays(-40).ToString('o')
    foreach ($run in "artifacts/$($releases.R1)", ".local/$($releases.R1)", "artifacts/$($releases.R0)") {
        $record = Get-Content -LiteralPath "$archive/$run/archived.json" -Raw | ConvertFrom-Json -AsHashtable
        foreach ($entry in $record.events) { $entry.archivedUtc = $old }
        $record | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath "$archive/$run/archived.json"
    }
    New-Run $outside 'purge-target' 1 @{ 'keep.bin' = 1024 }
    New-Link "$archive/artifacts/$($releases.R0)/linked" "$outside/purge-target"
    # A folder placed in the archive by hand has no housekeeping record and is never purged.
    New-Run $archive 'build/manual-copy' 2400 @{ 'big.bin' = 2048 }
    [IO.Directory]::SetLastWriteTimeUtc("$archive/build/manual-copy", [DateTime]::UtcNow.AddDays(-100))
    # Aged pointers beside kept receipts: a purge misdirected at a checkout (here ArchiveRoot names
    # the fake repository itself, for a same-named second repository) must never delete them.
    foreach ($run in "artifacts/$($releases.R1)", ".local/$($releases.R1)") {
        $record = Get-Content -LiteralPath "$fake/$run/archived.json" -Raw | ConvertFrom-Json -AsHashtable
        Assert ($record.role -eq 'source') "$run carries a source-side pointer record"
        foreach ($entry in $record.events) { $entry.archivedUtc = $old }
        $record | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath "$fake/$run/archived.json"
    }
    $clone = Join-Path $root 'clone/mod-fake'
    New-Item -ItemType Directory -Path $clone -Force | Out-Null
    $repoBefore = Get-Inventory $fake
    $result = Invoke-Housekeeping $clone @{ ArchiveRoot = $fake; Purge = $true; OlderThanDays = 30 }
    Assert (!$result.Purged.Count -and $result.Archive -eq [IO.Path]::GetFullPath($fake)) 'Purge ignores source-side pointer records.'
    Assert-Same $repoBefore (Get-Inventory $fake) 'A misdirected purge deletes no kept evidence'
    $archiveBefore = Get-Inventory $archive
    $outsideBefore = Get-Inventory $outside
    $result = Invoke-Housekeeping $fake @{ Purge = $true; OlderThanDays = 30; WhatIf = $true }
    Assert (!$result.Purged.Count) 'Purge -WhatIf deletes nothing.'
    Assert-Same $archiveBefore (Get-Inventory $archive) 'Purge -WhatIf leaves the archive'
    $pwsh = Join-Path $PSHOME $(if ($IsWindows) { 'pwsh.exe' } else { 'pwsh' })
    & $pwsh -NoProfile -NonInteractive -File $tool -RepoRoot $fake -Purge -OlderThanDays 30 *> $null
    Assert ($LASTEXITCODE -ne 0) 'Purge without confirmation fails in a non-interactive session.'
    Assert-Same $archiveBefore (Get-Inventory $archive) 'Unconfirmed purge deletes nothing'
    $result = Invoke-Housekeeping $fake @{ Purge = $true; OlderThanDays = 30 }
    $purged = @($result.Purged.Path | Sort-Object)
    Assert (($purged -join '|') -eq ((@("artifacts/$($releases.R1)", ".local/$($releases.R1)") | Sort-Object) -join '|')) "Purge deletes only archive entries older than the cutoff (got $($purged -join ', '))."
    Assert (@($result.Skipped | Where-Object { $_.Path -eq "artifacts/$($releases.R0)" -and $_.Detail -match 'link' }).Count -eq 1) 'Purge skips entries containing links.'
    Assert ((Test-Path -LiteralPath "$archive/build/manual-copy/big.bin") -and @($result.Skipped | Where-Object { $_.Path -eq 'build/manual-copy' -and $_.Detail -match 'no archived.json' }).Count -eq 1) 'Purge skips archive folders without a housekeeping record.'
    Assert (!(Test-Path -LiteralPath "$archive/artifacts/$($releases.R1)") -and !(Test-Path -LiteralPath "$archive/.local/$($releases.R1)")) 'Purged entries are gone.'
    Assert ((Test-Path -LiteralPath "$archive/artifacts/$($releases.R2)/Stage") -and (Test-Path -LiteralPath "$archive/build/$($builds.L1)/PC")) 'Recent archive entries stay.'
    Assert-Same $repoBefore (Get-Inventory $fake) 'Purge never touches the repository'
    Assert-Same $outsideBefore (Get-Inventory $outside) 'Purge never follows links'

    # --- release.ps1 runs it only inside the verified -Publish path, never with -Purge ------------
    $tokens = $null; $errors = $null
    $ast = [Management.Automation.Language.Parser]::ParseFile((Join-Path $repo 'release.ps1'), [ref]$tokens, [ref]$errors)
    $calls = @($ast.FindAll({ param($node) $node -is [Management.Automation.Language.CommandAst] -and $node.Extent.Text -match 'Invoke-Cleanup' }, $true))
    Assert ($calls.Count -eq 1) 'release.ps1 calls the cleanup (delete-mode housekeeping) once.'
    $call = $calls[0]
    Assert ($call.Extent.Text -match '-PublishedRun \$runName' -and $call.Extent.Text -notmatch '-Purge') 'release.ps1 passes the published run and never purges.'
    $guarded = $false
    for ($parent = $call.Parent; $parent; $parent = $parent.Parent) {
        if ($parent -is [Management.Automation.Language.IfStatementAst] -and $parent.Clauses[0].Item1.Extent.Text -eq '$Publish') { $guarded = $true }
    }
    $text = $ast.Extent.Text
    Assert ($guarded -and $call.Extent.StartOffset -gt $text.IndexOf("'--draft=false'") -and $call.Extent.StartOffset -gt $text.IndexOf('packageVerified')) 'Housekeeping runs only after the verified upload and the published GitHub release.'
    'PASS: release housekeeping keeps evidence and newest runs, archives superseded payloads with verified moves, skips links, deletes only on confirmed -Purge in the archive.'
} finally {
    $parent = $temp.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    if ((Test-Path -LiteralPath $root) -and [IO.Path]::GetFullPath($root).StartsWith($parent, [StringComparison]::OrdinalIgnoreCase) -and (Split-Path -Leaf $root) -like 'expbg-housekeeping-test-*') {
        Remove-Links $root
        Remove-Item -LiteralPath $root -Recurse -Force
    }
}

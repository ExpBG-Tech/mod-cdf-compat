#requires -Version 7.0
# Portable guard for tools/Invoke-Cleanup.ps1 (Invoke-ReleaseHousekeeping.ps1 -Delete) on a fake
# repository: heavy payloads of published, superseded and old runs are deleted; receipts, manifests,
# notes and logs stay (the release guard still refuses an uploaded version); the newest build and
# the installed build stay; recent unpublished runs, .local configuration and e2e content stay;
# links are never followed; nothing reaches ArchiveRoot; a second run changes nothing; the
# pre-push hook starts the cleanup only for main/master and never fails a push.
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
. "$repo/tools/Workshop-Common.ps1"
$temp = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$root = Join-Path $temp ('expbg-cleanup-test-' + [guid]::NewGuid().ToString('N'))
$fake = Join-Path $root 'mod-fake'
$archive = Join-Path $root 'archive'
$outside = Join-Path $root 'outside'
function Assert([bool]$Condition, [string]$Message) { if (!$Condition) { throw "FAIL: $Message" } }
function Rejects([scriptblock]$Action, [string]$Message) { $failed = $false; try { & $Action } catch { $failed = $true }; Assert $failed $Message }
function Get-Stamp([double]$HoursAgo) { [DateTime]::UtcNow.AddHours(-$HoursAgo).ToString('yyyyMMdd-HHmmss-fff') }
function New-Files([string]$Relative, [double]$HoursAgo, [hashtable]$Files) {
    $time = [DateTime]::UtcNow.AddHours(-$HoursAgo)
    foreach ($name in $Files.Keys) {
        $path = Join-Path $fake "$Relative/$name"
        New-Item -ItemType Directory -Path (Split-Path -Parent $path) -Force | Out-Null
        if ($Files[$name] -is [string]) { [IO.File]::WriteAllText($path, $Files[$name]) } else { [IO.File]::WriteAllBytes($path, [byte[]]::new([int]$Files[$name])) }
        [IO.File]::SetLastWriteTimeUtc($path, $time)
    }
}
function Exists([string]$Relative) { Test-Path -LiteralPath (Join-Path $fake $Relative) }
$big = 2MB
try {
    New-Item -ItemType Directory -Path $fake, $archive, $outside -Force | Out-Null
    [IO.File]::WriteAllBytes((Join-Path $outside 'keep.bin'), [byte[]]::new($big))
    New-Item -ItemType Directory -Path "$fake/.local" -Force | Out-Null
    $workbench = Join-Path $root 'workbench'
    New-Item -ItemType Directory -Path "$workbench/addons" -Force | Out-Null
    @{ ArchiveRoot = $archive; BuildAddonsDirectory = "$workbench/addons" } | ConvertTo-Json | Set-Content -LiteralPath "$fake/.local/config.json"
    foreach ($stamp in '20261001-010101-001-aa', '20261002-010101-001-bb', '20261003-010101-001-cc') { New-Item -ItemType Directory -Path "$workbench/.EXPBG_Mod-local-backups/$stamp/Scripts" -Force | Out-Null }
    $item = 'A1B2C3D4E5F60718'
    function New-Release([string]$Name, [double]$HoursAgo, [string]$Version, [bool]$Published) {
        $receipt = @{ item = $item; version = $Version; uploadAttempted = $Published; uploaded = $Published; packageVerified = $Published } | ConvertTo-Json
        New-Files "artifacts/$Name" $HoursAgo @{ 'workshop-receipt.json' = $receipt; 'Stage/Addon/Addon.gproj' = 'x'; 'Stage/Addon/data.bin' = $big; 'Mod_source.zip' = 4096 }
        New-Files ".local/$Name" $HoursAgo @{ 'change-note.txt' = 'notes'; 'packed/Addon.gproj' = 'x'; 'packed/data.pak' = $big; 'packed/manifest.json' = '{}'; 'publish-engine-logs/console.log' = 'log' }
    }
    $old = "workshop-local-$(Get-Stamp 200)"; New-Release $old 200 '0.1.0' $true
    $justPublished = "workshop-local-$(Get-Stamp 0.05)"; New-Release $justPublished 0.05 '0.1.1' $true
    $inProgress = "workshop-local-$(Get-Stamp 0.05)"; Start-Sleep -Milliseconds 20; $inProgress = "workshop-local-$(Get-Stamp 0.04)"; New-Release $inProgress 0.04 '0.1.2' $false
    $buildOld = "local-$(Get-Stamp 50)"; $buildInstalled = "local-$(Get-Stamp 40)"; $buildNew = "local-$(Get-Stamp 30)"
    foreach ($b in $buildOld, $buildInstalled, $buildNew) { New-Files "build/$b" 30 @{ 'receipt.json' = '{}'; 'EXPBG_Mod/Addon.gproj' = 'x'; 'EXPBG_Mod/data.bin' = $big } }
    @{ build = $buildInstalled } | ConvertTo-Json | Set-Content -LiteralPath "$fake/.local/last-build.json"
    $runOld = "gameplay-$(Get-Stamp 20)"; $runNew = "gameplay-$(Get-Stamp 10)"
    foreach ($r in $runOld, $runNew) { New-Files "build/$r" 10 @{ 'result.json' = '{}'; 'addons/Mod/Addon.gproj' = 'x'; 'addons/Mod/data.bin' = $big } }
    New-Files '.local/e2e' 100 @{ 'profile/save.bin' = $big }
    $leftover = 'portable-test-' + [guid]::NewGuid().ToString('N'); New-Files ".local/$leftover" 5 @{ 'x.txt' = 'x' }
    [IO.Directory]::SetLastWriteTimeUtc((Join-Path $fake ".local/$leftover"), [DateTime]::UtcNow.AddHours(-5))
    $fresh = 'portable-test-' + [guid]::NewGuid().ToString('N'); New-Files ".local/$fresh" 0 @{ 'x.txt' = 'x' }
    # A junction inside an old run must never be followed.
    $link = Join-Path $fake "build/$runOld/linked"
    if ($IsWindows) { New-Item -ItemType Junction -Path $link -Target $outside | Out-Null } else { New-Item -ItemType SymbolicLink -Path $link -Target $outside | Out-Null }

    & "$repo/tools/Invoke-Cleanup.ps1" -RepoRoot $fake -TestIgnoreEngines -Confirm:$false 6>$null | Out-Null

    # Published (old and just published): heavy gone, evidence stays.
    foreach ($name in $old, $justPublished) {
        Assert (!(Exists "artifacts/$name/Stage") -and !(Exists "artifacts/$name/Mod_source.zip") -and !(Exists ".local/$name/packed/data.pak")) "$name payloads deleted"
        Assert ((Exists "artifacts/$name/workshop-receipt.json") -and (Exists ".local/$name/change-note.txt") -and (Exists ".local/$name/packed/manifest.json") -and (Exists ".local/$name/publish-engine-logs/console.log")) "$name evidence stays"
    }
    Assert ((Exists "artifacts/$inProgress/Stage/Addon/data.bin") -and (Exists ".local/$inProgress/packed/data.pak")) 'A recent unpublished release run stays in full.'
    Rejects { Assert-NoWorkshopUploadAttempt $fake '0.1.1' $item } 'uploaded version still refused after cleanup'
    Assert ((Exists "build/$buildNew/EXPBG_Mod/data.bin") -and (Exists "build/$buildInstalled/EXPBG_Mod/data.bin")) 'Newest and installed builds stay.'
    Assert (!(Exists "build/$buildOld/EXPBG_Mod") -and (Exists "build/$buildOld/receipt.json")) 'An older build loses its snapshot, keeps its receipt.'
    Assert ((Exists "build/$runNew/addons/Mod/data.bin") -and !(Exists "build/$runOld/addons") -and (Exists "build/$runOld/result.json")) 'Newest run stays; older run keeps only evidence.'
    Assert ((Exists "build/$runOld/linked") -and (Test-Path -LiteralPath (Join-Path $outside 'keep.bin'))) 'Links are skipped, their targets untouched.'
    Assert ((Exists '.local/e2e/profile/save.bin') -and (Exists '.local/config.json') -and (Exists '.local/last-build.json')) '.local configuration and e2e content stay.'
    Assert (!(Exists ".local/$leftover") -and (Exists ".local/$fresh")) 'Old leftover test folders go; recent ones stay.'
    Assert (!@(Get-ChildItem -LiteralPath $archive -Recurse -File).Count) 'Nothing is moved to ArchiveRoot.'
    Assert ((@(Get-ChildItem -LiteralPath "$workbench/.EXPBG_Mod-local-backups" -Directory).Name -join ',') -eq '20261003-010101-001-cc') 'Only the newest local install backup stays.'
    $before = @(Get-ChildItem -LiteralPath $fake -Recurse -File -Attributes !ReparsePoint | ForEach-Object FullName | Sort-Object)
    & "$repo/tools/Invoke-Cleanup.ps1" -RepoRoot $fake -TestIgnoreEngines -Confirm:$false 6>$null | Out-Null
    $after = @(Get-ChildItem -LiteralPath $fake -Recurse -File -Attributes !ReparsePoint | ForEach-Object FullName | Sort-Object)
    Assert (!(Compare-Object $before $after)) 'A second cleanup changes nothing.'

    # Hook: LF shell script, main/master only, starts Invoke-Cleanup detached, always exits 0.
    $hook = Join-Path $repo '.githooks/pre-push'
    $text = [IO.File]::ReadAllText($hook)
    Assert ($text.StartsWith("#!/bin/sh`n") -and $text -notmatch "`r") 'pre-push is an LF shell script.'
    Assert ($text -match 'refs/heads/main\|refs/heads/master\)' -and $text -match 'Invoke-Cleanup\.ps1' -and $text -match 'Start-Process -WindowStyle Hidden' -and $text.TrimEnd().EndsWith('exit 0')) 'pre-push starts the cleanup in the background for main/master only and never fails the push.'
    Assert (Test-Path -LiteralPath (Join-Path $repo 'tools/Install-GitHooks.ps1')) 'Install-GitHooks.ps1 enables the hook.'
    'PASS: cleanup deletes published, superseded and old payloads (receipts, manifests, notes and logs stay; uploaded versions stay refused), keeps the newest and installed builds and the newest install backup, recent unpublished runs and .local configuration, skips links, archives nothing, is idempotent; pre-push runs it for main/master only.'
} finally {
    if ((Test-Path -LiteralPath $root) -and (Split-Path -Leaf $root) -like 'expbg-cleanup-test-*') {
        foreach ($l in @(Get-ChildItem -LiteralPath $root -Recurse -Force -Attributes ReparsePoint -ErrorAction SilentlyContinue)) { if ($l.PSIsContainer) { [IO.Directory]::Delete($l.FullName, $false) } else { [IO.File]::Delete($l.FullName) } }
        Remove-Item -LiteralPath $root -Recurse -Force
    }
}

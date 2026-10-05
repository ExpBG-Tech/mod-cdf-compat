#requires -Version 7.0
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/../tools/Workshop-Common.ps1"
$project = & "$PSScriptRoot/../tools/Get-ProjectConfig.ps1"
$projectFile = $project.addon.project
function Rejects([scriptblock]$Action) {
    $rejected = $false
    try { & $Action } catch { $rejected = $true }
    if (!$rejected) { throw 'Expected release guard to reject input.' }
}
Assert-WorkshopVersion 'v0.1.15' '0.1.15'
Rejects { Assert-WorkshopVersion 'v0.1.15;echo injected' '0.1.15' }
Rejects { Assert-WorkshopVersion 'v0.1.14' '0.1.15' }
Rejects { Assert-WorkshopVersion 'v01.1.15' '01.1.15' }
Assert-WorkshopLog 'build' 0 'Build successful. Game destroyed.'
Rejects { Assert-WorkshopLog 'pack' 0 'Game destroyed.' }
Assert-WorkshopLog 'pack' 0 'Packaging project successful. Game destroyed.'
Rejects { Assert-WorkshopLog 'build' 1 'Build successful. Game destroyed.' }
Rejects { Assert-WorkshopLog 'build' 0 'Build successful. Assertion failed. Game destroyed.' }
Rejects { Assert-WorkshopLog 'build' 0 'Build successful. Game destroyed. Failed to initialize Enfusion engine' }
Rejects { Assert-WorkshopLog 'publish' 0 'Project uploaded successfully' }
Assert-WorkshopLog 'publish' 0 'Project uploaded successfully. Addon processing successful.'
$root = Join-Path $PSScriptRoot ('../.local/workshop-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $root -Force | Out-Null
$manifest = @{asset=@{id=$project.addon.id;unlisted=$project.asset.unlisted;private=$project.asset.private;version='0.1.14'}}
$manifest | ConvertTo-Json -Depth 5 | Set-Content "$root/manifest.json"
Rejects { Assert-WorkshopPackage $root }
'payload' | Set-Content "$root/data.pak"
'index' | Set-Content "$root/resourceDatabase.rdb"
('GameProject { GUID ' + $project.addon.id + ' }') | Set-Content "$root/$projectFile"
Write-WorkshopManifest $root "$PSScriptRoot/../tools/workshop-asset.json" '0.1.15' 'A test release' $project.addon.previewPath
$manifest = Get-Content "$root/manifest.json" -Raw | ConvertFrom-Json
if ($manifest.asset.version -ne '0.1.15' -or $manifest.asset.changelog -ne 'A test release' -or $manifest.asset.description -match '\{\{VERSION\}\}') { throw 'Manifest release metadata was not substituted.' }
Assert-WorkshopPackage $root
Assert-WorkshopPackage $root -Version '0.1.15'
Rejects { Assert-WorkshopPackage $root -Version '0.1.14' }
'changed payload' | Set-Content "$root/data.pak"
Rejects { Assert-WorkshopPackage $root }
'payload' | Set-Content "$root/data.pak"
# -publishAddon rewrites manifest.json with Workbench's inventory: runtime payloads only (0.1.18 run 35434389106).
$prepared = Get-Content "$root/manifest.json" -Raw
function Inventory([string[]]$Names) {
    $refreshed = $prepared | ConvertFrom-Json
    $refreshed.files = @($refreshed.files | Where-Object { $_.name -cin $Names })
    $refreshed | ConvertTo-Json -Depth 8 | Set-Content "$root/manifest.json"
}
Inventory @('data.pak',$projectFile,'resourceDatabase.rdb')
Rejects { Assert-WorkshopPackage $root }
Assert-WorkshopPackage $root -Published -Version '0.1.15'
Rejects { Assert-WorkshopPackage $root -Published -Version '0.1.14' }
Inventory @('data.pak',$projectFile,'resourceDatabase.rdb','previewImage.png')
Assert-WorkshopPackage $root -Published -Version '0.1.15'
Inventory @('data.pak',$projectFile)
Rejects { Assert-WorkshopPackage $root -Published -Version '0.1.15' }
$refreshed = $prepared | ConvertFrom-Json
$refreshed.files[3].name = 'extra.pak'
$refreshed | ConvertTo-Json -Depth 8 | Set-Content "$root/manifest.json"
Copy-Item "$root/previewImage.png" "$root/extra.pak"
Rejects { Assert-WorkshopPackage $root }
Rejects { Assert-WorkshopPackage $root -Published -Version '0.1.15' }
$prepared | Set-Content "$root/manifest.json"
Assert-WorkshopPackage $root
# A valid manifest/hash cannot make an editor-only resource available at runtime.
$plainProject = Get-Content "$root/$projectFile" -Raw
$sourceTable = ' StringTableSource "{B703E765A4924711}Language/EXPBG_Localization.st"'
($plainProject + "`n" + $sourceTable) | Set-Content "$root/$projectFile"
Write-WorkshopManifest $root "$PSScriptRoot/../tools/workshop-asset.json" '0.1.15' 'A test release' $project.addon.previewPath
Rejects { Assert-WorkshopPackage $root }
Rejects { Assert-WorkshopPackage $root -Published }
$runtimeTable = ' StringTableRuntime "{D360279A89F44AB2}Language/EXPBG_Localization.en_us.conf"'
foreach ($newline in @("`n", "`r`n")) {
    $expected = $plainProject + $newline + $runtimeTable + $newline
    [IO.File]::WriteAllText("$root/$projectFile", $expected + $sourceTable + $newline + ' StringTableSource ""' + $newline)
    Remove-WorkshopSourceTableReferences "$root/$projectFile"
    if ([IO.File]::ReadAllText("$root/$projectFile") -cne $expected) { throw 'Stripping editor references changed runtime configuration.' }
    Remove-WorkshopSourceTableReferences "$root/$projectFile"
    if ([IO.File]::ReadAllText("$root/$projectFile") -cne $expected) { throw 'Stripping editor references is not idempotent.' }
    Write-WorkshopManifest $root "$PSScriptRoot/../tools/workshop-asset.json" '0.1.15' 'A test release' $project.addon.previewPath
    Assert-WorkshopPackage $root
}
[IO.File]::WriteAllText("$root/$projectFile", ($plainProject + "`n StringTableSource unquoted.st"))
Rejects { Remove-WorkshopSourceTableReferences "$root/$projectFile" }
$plainProject | Set-Content "$root/$projectFile"
$manifest.asset.unlisted=!$project.asset.unlisted
$manifest | ConvertTo-Json -Depth 5 | Set-Content "$root/manifest.json"
Rejects { Assert-WorkshopPackage $root }
$manifest.asset.unlisted=$project.asset.unlisted
$manifest.asset.id='0000000000000000'
$manifest | ConvertTo-Json -Depth 5 | Set-Content "$root/manifest.json"
Rejects { Assert-WorkshopPackage $root }
# The running PowerShell 7, even when pwsh is not on PATH (portable runtime installs).
$pwsh = Join-Path $PSHOME $(if ($IsWindows) {'pwsh.exe'} else {'pwsh'})
$probe = Join-Path $root 'private argument probe.ps1'
'param([string]$Value) Write-Output $Value; exit 7' | Set-Content -LiteralPath $probe
$result = Invoke-PrivateProcess $pwsh @('-NoProfile','-File',$probe,'sentinel $() ; quoted " text') $root "$root/private.log" 20
if ($result.ExitCode -ne 7 -or !$result.Text.Contains('sentinel $() ; quoted " text')) { throw 'Process changed argument data or exit code.' }
Rejects { Invoke-PrivateProcess $pwsh @('-NoProfile','-Command','Start-Sleep 20') $root "$root/timeout.log" 1 }
if (!(Test-Path "$root/timeout.log")) { throw 'Timeout discarded private evidence.' }
# Every guard passed: the synthetic package holds no evidence worth keeping on the runner.
$testParent = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../.local')).TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar
if (![IO.Path]::GetFullPath($root).StartsWith($testParent, [StringComparison]::OrdinalIgnoreCase)) { throw 'Test cleanup must remain under .local.' }
Remove-Item -LiteralPath $root -Recurse -Force
'PASS: release version, native failure, backend processing, package identity/visibility, prepared and published manifest inventories, literal process arguments and timeout guards.'

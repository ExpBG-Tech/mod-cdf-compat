#requires -Version 7.0
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/../tools/Workshop-Common.ps1"
. "$PSScriptRoot/../tools/Workshop-Interactive.ps1"
function Rejects([scriptblock]$Action, [string]$Pattern) {
    try { & $Action | Out-Null } catch {
        if ($_.Exception.Message -notmatch $Pattern) { throw }
        return
    }
    throw "Expected rejection: $Pattern"
}
$repo = Split-Path -Parent $PSScriptRoot
$settings = & "$repo/tools/Get-ProjectConfig.ps1"
$projectFile = $settings.addon.project
$root = Join-Path $repo ('.local/interactive-release-test-' + [guid]::NewGuid().ToString('N'))
$prepared = Join-Path $root 'prepared'
$actual = Join-Path $root 'actual'
$private = Join-Path $root 'private'
New-Item -ItemType Directory -Path $prepared,$actual,$private | Out-Null
try {
    'payload' | Set-Content "$prepared/data.pak"
    'index' | Set-Content "$prepared/resourceDatabase.rdb"
    ('GameProject { GUID ' + $settings.addon.id + ' }') | Set-Content "$prepared/${projectFile}"
    Write-WorkshopManifest $prepared "$repo/tools/workshop-asset.json" '0.0.13' 'The release note.' $settings.addon.previewPath $settings.addon.name
    $preparedHashes = @{}
    foreach ($file in Get-ChildItem -LiteralPath $prepared -File) { $preparedHashes[$file.Name] = (Get-FileHash -LiteralPath $file.FullName).Hash }
    Copy-Item -Path "$prepared/*" -Destination $actual
    # Native UI may rebuild the index, but never the prepared code or project.
    'native UI index' | Set-Content "$actual/resourceDatabase.rdb"
    function RefreshManifest([string]$Directory = $actual) {
        $manifest = Get-Content "$Directory/manifest.json" -Raw | ConvertFrom-Json
        foreach ($file in $manifest.files) {
            $path = Join-Path $Directory $file.name
            $file.size = (Get-Item -LiteralPath $path).Length
            $file.sha256 = (Get-FileHash -LiteralPath $path).Hash
        }
        $manifest | ConvertTo-Json -Depth 8 | Set-Content "$Directory/manifest.json"
    }
    RefreshManifest
    Rejects { Assert-WorkshopInteractiveBundle $actual $prepared '0.0.13' $settings } 'Original prepared receipt hashes'
    Assert-WorkshopInteractiveBundle $actual $prepared '0.0.13' -PreparedHashes $preparedHashes
    $preparedPreview = [IO.File]::ReadAllBytes("$prepared/previewImage.png")
    'replaced prepared badge' | Set-Content "$prepared/previewImage.png"
    Rejects { Assert-WorkshopInteractiveBundle $actual $prepared '0.0.13' $settings -PreparedHashes $preparedHashes } 'Prepared release input changed: previewImage.png'
    [IO.File]::WriteAllBytes("$prepared/previewImage.png", $preparedPreview)
    'different compiled payload' | Set-Content "$actual/data.pak"
    RefreshManifest
    Rejects { Assert-WorkshopInteractiveBundle $actual $prepared '0.0.13' $settings -PreparedHashes $preparedHashes } 'data.pak changed'
    Copy-Item -LiteralPath "$prepared/data.pak" -Destination "$actual/data.pak"
    RefreshManifest
    'wrong badge' | Set-Content "$actual/previewImage.png"
    RefreshManifest
    Rejects { Assert-WorkshopInteractiveBundle $actual $prepared '0.0.13' $settings -PreparedHashes $preparedHashes } 'preview'
    Copy-Item -LiteralPath "$prepared/previewImage.png" -Destination "$actual/previewImage.png"
    RefreshManifest
    $manifest = Get-Content "$actual/manifest.json" -Raw | ConvertFrom-Json
    $manifest.asset.description = 'Old backend listing.'
    $manifest | ConvertTo-Json -Depth 8 | Set-Content "$actual/manifest.json"
    Rejects { Assert-WorkshopInteractiveBundle $actual $prepared '0.0.13' $settings -PreparedHashes $preparedHashes } 'listing'
    $manifest.asset.description = (Get-Content "$prepared/manifest.json" -Raw | ConvertFrom-Json).asset.description
    $manifest.asset.changelog = 'Wrong change note.'
    $manifest | ConvertTo-Json -Depth 8 | Set-Content "$actual/manifest.json"
    Rejects { Assert-WorkshopInteractiveBundle $actual $prepared '0.0.13' $settings -PreparedHashes $preparedHashes } 'change note'
    $manifest.asset.changelog = 'The release note.'
    $manifest.asset.version = '0.0.12'
    $manifest | ConvertTo-Json -Depth 8 | Set-Content "$actual/manifest.json"
    Rejects { Assert-WorkshopInteractiveBundle $actual $prepared '0.0.13' $settings -PreparedHashes $preparedHashes } 'version'
    $manifest.asset.version = '0.0.13'; $manifest.asset.unlisted = $false
    $manifest | ConvertTo-Json -Depth 8 | Set-Content "$actual/manifest.json"
    Rejects { Assert-WorkshopInteractiveBundle $actual $prepared '0.0.13' $settings -PreparedHashes $preparedHashes } 'visibility'

    # Matching rebuilt bundles must still agree with the original receipt hashes.
    Copy-Item -LiteralPath "$prepared/manifest.json" -Destination "$actual/manifest.json"
    foreach ($directory in @($prepared,$actual)) {
        'both bundles replaced after preparation' | Set-Content "$directory/data.pak"
        RefreshManifest $directory
    }
    Rejects { Assert-WorkshopInteractiveBundle $actual $prepared '0.0.13' $settings -PreparedHashes $preparedHashes } 'Prepared release input changed'

    # A stale native bundle is preserved before a new UI session can create one.
    $localData = Join-Path $root 'local-data'
    $bundle = Join-Path $localData ('Temp/Arma Reforger Workbench/Publishing/' + $settings.addon.id)
    New-Item -ItemType Directory -Path $bundle -Force | Out-Null
    'older evidence' | Set-Content "$bundle/evidence.txt"
    $resolved = Initialize-WorkshopInteractiveBundle $settings.addon.id $private $localData
    if ($resolved -ne $bundle -or (Test-Path -LiteralPath $bundle) -or (Get-Content "$private/previous-ui-bundle/evidence.txt" -Raw).Trim() -cne 'older evidence') { throw 'Old UI evidence was not preserved.' }
    New-Item -ItemType Directory -Path $bundle | Out-Null
    Rejects { Initialize-WorkshopInteractiveBundle $settings.addon.id $private $localData } 'already exists'
    Rejects { Initialize-WorkshopInteractiveBundle '../outside' $private $localData } 'item'

    # The real entrypoints must reject UI mode before credentials/native work.
    $pwsh = (Get-Process -Id $PID).Path
    foreach ($entry in @(@("$repo/release.ps1",'-Interactive'), @("$repo/tools/Invoke-WorkshopRelease.ps1",'-Tag','v0.0.13','-Interactive'))) {
        $output = & $pwsh -NoProfile -File @entry 2>&1 | Out-String
        if ($LASTEXITCODE -eq 0 -or $output -notmatch 'Interactive requires -Publish') { throw 'Interactive mode did not reject missing publication authorization.' }
    }
} finally {
    # This exact, newly created fixture is the only removal target.
    if ([IO.Path]::GetFullPath($root).StartsWith([IO.Path]::GetFullPath("$repo/.local/") , [StringComparison]::OrdinalIgnoreCase)) { Remove-Item -LiteralPath $root -Recurse -Force }
}
$global:LASTEXITCODE = 0
'PASS: interactive release rejects prepared-input, payload, badge, listing, note, version and visibility drift; preserves prior UI bundles and requires -Publish.'

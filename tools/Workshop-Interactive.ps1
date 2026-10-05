#requires -Version 7.0
# The public entrypoint owns authentication, GitHub checks and the item mutex.
# These helpers only preserve native UI evidence and validate its actual bundle.
function Initialize-WorkshopInteractiveBundle([string]$Item, [string]$Private, [string]$LocalAppData = $env:LOCALAPPDATA) {
    if ($Item -cnotmatch '^[A-F0-9]{16}$' -or !$LocalAppData) { throw 'A valid Workshop item and local application data directory are required.' }
    $publishing = [IO.Path]::GetFullPath((Join-Path $LocalAppData 'Temp/Arma Reforger Workbench/Publishing'))
    $bundle = [IO.Path]::GetFullPath((Join-Path $publishing $Item))
    $privatePath = [IO.Path]::GetFullPath($Private).TrimEnd('\','/')
    $archive = Join-Path $privatePath 'previous-ui-bundle'
    $separator = [IO.Path]::DirectorySeparatorChar
    if ((Split-Path -Parent $bundle) -ne $publishing -or $bundle.StartsWith($privatePath + $separator, [StringComparison]::OrdinalIgnoreCase) -or $privatePath.StartsWith($bundle + $separator, [StringComparison]::OrdinalIgnoreCase) -or $bundle -eq $privatePath) { throw 'UI bundle and private evidence paths must be separate.' }
    if (!(Test-Path -LiteralPath $privatePath -PathType Container)) { throw 'Create the private release directory before opening the UI.' }
    foreach ($path in @($bundle,$privatePath)) {
        for ($ancestor = $path; $ancestor; $ancestor = Split-Path -Parent $ancestor) {
            if ((Test-Path -LiteralPath $ancestor) -and ((Get-Item -LiteralPath $ancestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Linked UI publication paths are unsupported.' }
        }
    }
    if (Test-Path -LiteralPath $archive) { throw 'Prior UI bundle evidence already exists; preserve this run.' }
    if (Test-Path -LiteralPath $bundle) {
        if (!(Test-Path -LiteralPath $bundle -PathType Container) -or @(Get-ChildItem -LiteralPath $bundle -Recurse -Force | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }).Count) { throw 'Linked or non-directory UI bundles are unsupported.' }
        # Both absolute paths were checked above. Never overwrite old evidence.
        Move-Item -LiteralPath $bundle -Destination $archive
    }
    return $bundle
}

function Assert-WorkshopInteractiveBundle([string]$Directory, [string]$PreparedDirectory, [string]$Version, $Project = (& "$PSScriptRoot/Get-ProjectConfig.ps1"), [hashtable]$PreparedHashes) {
    if (([IO.Path]::GetFullPath($Directory)).TrimEnd('\','/') -eq ([IO.Path]::GetFullPath($PreparedDirectory)).TrimEnd('\','/')) { throw 'Validate the actual UI bundle, not the prepared package.' }
    if (!$PreparedHashes) { throw 'Original prepared receipt hashes are required for interactive verification.' }
    foreach ($name in @('manifest.json','data.pak','resourceDatabase.rdb',$Project.addon.project,'previewImage.png')) {
        $path = Join-Path $PreparedDirectory $name
        if (!$PreparedHashes.ContainsKey($name) -or [string]$PreparedHashes[$name] -notmatch '^[0-9A-Fa-f]{64}$' -or !(Test-Path -LiteralPath $path -PathType Leaf) -or (Get-FileHash -LiteralPath $path).Hash -ne [string]$PreparedHashes[$name]) { throw "Prepared release input changed: $name" }
    }
    if ((Get-Item -LiteralPath $Directory -Force).Attributes -band [IO.FileAttributes]::ReparsePoint -or @(Get-ChildItem -LiteralPath $Directory -Recurse -Force | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }).Count) { throw 'Linked UI bundles are unsupported.' }
    Assert-WorkshopPackage $PreparedDirectory -Version $Version -Project $Project
    $prepared = Get-Content -LiteralPath (Join-Path $PreparedDirectory 'manifest.json') -Raw | ConvertFrom-Json
    Assert-WorkshopPackage $Directory -Version $Version -Uploaded -PreparedHashes $PreparedHashes -Project $Project
    if ((Get-FileHash -LiteralPath (Join-Path $Directory 'previewImage.png')).Hash -ne $PreparedHashes['previewImage.png']) { throw 'UI preview differs from the prepared badge.' }
    $actual = Get-Content -LiteralPath (Join-Path $Directory 'manifest.json') -Raw | ConvertFrom-Json
    if (@(Get-WorkshopListingDifferences $prepared.asset $actual.asset).Count) { throw 'UI listing differs from the prepared metadata. Inspect the backend; never retry this version automatically.' }
    if (([string]$actual.asset.changelog).Replace("`r`n","`n").Trim() -cne ([string]$prepared.asset.changelog).Replace("`r`n","`n").Trim()) { throw 'UI change note differs from the prepared release note.' }
}

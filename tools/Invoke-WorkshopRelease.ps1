#requires -Version 7.0
[CmdletBinding()]
param([Parameter(Mandatory)][string]$Tag, [switch]$Publish, [switch]$Interactive, [switch]$Companion,
    [ValidatePattern('^[A-Za-z0-9_-]+$')][string]$RunName = '', [pscredential]$Credential)
$ErrorActionPreference = 'Stop'
if ($Interactive -and !$Publish) { throw 'Interactive requires -Publish and explicit publication authorization.' }
. "$PSScriptRoot/Workshop-Common.ps1"
. "$PSScriptRoot/Workshop-Interactive.ps1"
if (!$IsWindows) { throw 'Native build, packing and publication require the licensed Windows Workbench PC.' }
if ($env:GITHUB_ACTIONS -eq 'true') { throw 'Native releases run locally through ./release.ps1.' }
$repo = Split-Path -Parent $PSScriptRoot
$settings = & "$PSScriptRoot/Get-ReleaseConfig.ps1" -Companion:$Companion
$config = & "$PSScriptRoot/Get-LocalConfig.ps1"
$version = (Get-Content (Join-Path $repo $settings.versionFile) -Raw).Trim()
Assert-WorkshopVersion $Tag $version $settings.tagPrefix
if ($Publish) {
    Assert-NoLegacyPublishHold $repo
    $releaseSource = Get-LocalReleaseSource $repo $Tag -Companion:$Companion
    if ($releaseSource.createTag) { throw 'Create and push the release tag through ./release.ps1 -Publish first.' }
    Assert-NoWorkshopUploadAttempt $repo $version $settings.addon.id
    if (!$Credential) { $Credential = Get-WorkshopCredential }
}
if (Get-Process -Name ArmaReforgerSteamDiag,ArmaReforgerServerDiag,ArmaReforgerWorkbenchSteamDiag -ErrorAction SilentlyContinue) { throw 'Finish active engine tests before releasing.' }
$game = $env:REFORGER_GAME_DIR
if (!$game) { $game = $config.GameRoot }
$engine = $env:REFORGER_WORKBENCH_EXE
if (!$engine) { $engine = Join-Path $config.WorkbenchRoot 'ArmaReforgerWorkbenchSteamDiag.exe' }
if (!(Test-Path -LiteralPath $engine -PathType Leaf) -or !(Test-Path -LiteralPath "$game/addons/data/ArmaReforger.gproj")) { throw 'Configure complete native game/Workbench installations in .local/config.json.' }
if (!$RunName) { $RunName = 'workshop-local-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff') }
$private = Join-Path $repo ".local/$RunName"
if (Test-Path -LiteralPath $private) { throw 'Run directory already exists; preserve it and use a new run name.' }
New-Item -ItemType Directory -Path $private | Out-Null
& "$PSScriptRoot/Stage-Release.ps1" -Name $RunName -Companion:$Companion | Out-Null
$artifact = Join-Path $repo "artifacts/$RunName"
$source = Join-Path $artifact ('Stage/' + $settings.addon.name)
$project = Join-Path $source $settings.addon.project
$packed = Join-Path $private 'packed'
$built = Join-Path $private 'built'
$profile = Join-Path $private 'profile'
New-Item -ItemType Directory -Path $packed,$built,$profile | Out-Null
$match = [regex]::Match((Get-Content (Join-Path $repo $settings.changelogFile) -Raw), '(?ms)^## ' + [regex]::Escape($version) + '\b[^\r\n]*\r?\n(.*?)(?=^## |\z)')
if (!$match.Success) { throw 'Add this version to CHANGELOG.md before releasing.' }
$notes = $match.Groups[1].Value.Trim()
if (!$notes) { throw 'The release changelog section must not be empty.' }
[IO.File]::WriteAllText("$private/change-note.txt", $notes)
$addonDirectories = "$game/addons"
$dependencyRecord = $null
if ($Companion -or $settings.addon.installedDependencies) {
    $dependencies = Join-Path $private 'dependencies'
    & "$PSScriptRoot/Copy-AddonDependencies.ps1" -Destination $dependencies -InstalledAddonsRoot $config.InstalledAddonsRoot -SearchRoots ($config.DependencyAddonsRoots -split ';') -Companion:$Companion | Out-Null
    $dependencyRecord = Get-Content -LiteralPath ($dependencies + '.json') -Raw | ConvertFrom-Json
    $addonDirectories += ',' + $dependencies
}
$common = @('-disableCrashReporter','-noThrow','-gproj',$project,'-profile',$profile,'-addonsDir',$addonDirectories,'-wbModule=ResourceManager')
if ($env:REFORGER_WORKBENCH_SETTINGS) { $common = @('-forceSettings',[IO.Path]::GetFullPath($env:REFORGER_WORKBENCH_SETTINGS)) + $common }
function RunWorkbench([string]$Phase, [string[]]$Options) {
    $phaseLogs = Join-Path $private "$Phase-engine-logs"
    New-Item -ItemType Directory -Path $phaseLogs | Out-Null
    $timeout = 1200
    if ($Interactive -and $Phase -eq 'publish') { $timeout = 1800 }
    $result = Invoke-PrivateProcess $engine ($common + @('-logsDir',$phaseLogs) + $Options) $game "$private/$Phase-output.log" $timeout
    $logs = @(Get-ChildItem -LiteralPath $phaseLogs -Filter '*.log' -Recurse -File | ForEach-Object { Get-Content -LiteralPath $_.FullName -Raw })
    $log = $result.Text + ($logs -join "`n")
    Assert-WorkshopLog $Phase $result.ExitCode $log
    if ($Interactive -and $Phase -eq 'publish' -and $log -notmatch 'Game destroyed') { throw 'The publishing UI did not confirm clean shutdown; preserve its evidence and inspect the backend.' }
}
RunWorkbench 'build' @('-builddata','PC',$built,$settings.addon.name)
Remove-WorkshopSourceTableReferences $project
RunWorkbench 'pack' @('-packAddon','-packAddonDir',$packed)
Write-WorkshopManifest $packed (Join-Path $repo $settings.assetFile) $version $notes (Join-Path $source $settings.addon.preview) $settings.addon.name
Assert-WorkshopPackage $packed -Version $version -Project $settings
$receipt = [ordered]@{
    version=$version; commit=(& git -C $repo rev-parse HEAD); item=$settings.addon.id
    releaseCommit=if ($releaseSource) {$releaseSource.commit} else {$null}
    tag=$Tag; localExecution=$true
    visibility=if ($settings.asset.private) {'Private'} elseif ($settings.asset.unlisted) {'Unlisted'} else {'Public'}
    compiled=$true; packed=$true; uploadAttempted=$false; uploaded=$false; packageVerified=$false
    publicationMode=if ($Interactive) {'interactive'} else {'command-line'}
    listingMatchesPrepared=$null; listingDifferences=@()
    downloadedVerified=$false; nativeGameplayTested=$false
    dependencies=$dependencyRecord
    workshopUrl=('https://reforger.armaplatform.com/workshop/' + $settings.addon.id)
    files=@(Get-ChildItem -LiteralPath $packed -File | ForEach-Object { @{name=$_.Name;sha256=(Get-FileHash -LiteralPath $_.FullName).Hash} })
}
$receiptPath = Join-Path $artifact 'workshop-receipt.json'
function SaveReceipt { $receipt | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $receiptPath }
SaveReceipt
if ($Publish) {
    # Persist intent first: a failure after this point may already have changed the backend.
    $preparedListing = (Get-Content -LiteralPath "$packed/manifest.json" -Raw | ConvertFrom-Json).asset
    $receipt.preparedFiles=$receipt.files
    $publishedDirectory = $packed
    if ($Interactive) {
        $publishedDirectory = Initialize-WorkshopInteractiveBundle $settings.addon.id $private
        "Publish Project: version $version; preserve configured visibility." | Write-Host
        "Frozen project: $project" | Write-Host
        "Preview PNG: $packed/previewImage.png" | Write-Host
        "Change note: $private/change-note.txt" | Write-Host
        "Prepared package: $packed" | Write-Host
        "Original prepared receipt: $receiptPath" | Write-Host
        "Actual UI bundle: $publishedDirectory" | Write-Host
        'Before confirming upload, verify the actual UI bundle with Assert-WorkshopInteractiveBundle. Close this Workbench after successful backend processing; the release mutex stays held until exit (30-minute limit).' | Write-Host
    }
    $receipt.uploadAttempted=$true; SaveReceipt
    if ($Interactive) {
        RunWorkbench 'publish' @('-wbBackendLogin',$Credential.UserName,$Credential.GetNetworkCredential().Password)
    } else {
        RunWorkbench 'publish' @('-publishAddon','-publishAddonDir',$packed,'-publishAddonVersion',$version,'-publishAddonPreviewImage',(Join-Path $source $settings.addon.preview),'-publishAddonChangeNoteFile',"$private/change-note.txt",'-wbBackendLogin',$Credential.UserName,$Credential.GetNetworkCredential().Password)
    }
    # Workbench refreshes the resource index and manifest during upload.
    $receipt.uploaded=$true
    $receipt.files=@(Get-ChildItem -LiteralPath $publishedDirectory -File | ForEach-Object { @{name=$_.Name;sha256=(Get-FileHash -LiteralPath $_.FullName).Hash} })
    SaveReceipt
    $preparedHashes = @{}
    foreach ($file in $receipt.preparedFiles) { $preparedHashes[$file.name] = $file.sha256 }
    if ($Interactive) {
        Assert-WorkshopInteractiveBundle $publishedDirectory $packed $version $settings -PreparedHashes $preparedHashes
        Copy-Item -LiteralPath $publishedDirectory -Destination "$private/ui-published" -Recurse
        Assert-WorkshopInteractiveBundle "$private/ui-published" $packed $version $settings -PreparedHashes $preparedHashes
    } else { Assert-WorkshopPackage $publishedDirectory -Uploaded -PreparedHashes $preparedHashes -Version $version -Project $settings }
    $receipt.packageVerified=$true
    $publishedListing = (Get-Content -LiteralPath "$publishedDirectory/manifest.json" -Raw | ConvertFrom-Json).asset
    $receipt.listingDifferences = @(Get-WorkshopListingDifferences $preparedListing $publishedListing)
    $receipt.listingMatchesPrepared = $receipt.listingDifferences.Count -eq 0
    SaveReceipt
    if (!$receipt.listingMatchesPrepared) {
        Write-Warning ("Upload succeeded, but native publishing retained or normalized different listing fields: " + ($receipt.listingDifferences -join ', ') + '. Review the final listing separately; do not retry this version.')
    }
    "Uploaded and processed: $($receipt.workshopUrl). Downloaded-package/gameplay verification remains separate." | Write-Host
} else { 'Native build and packing completed; no upload requested.' | Write-Host }

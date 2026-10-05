# Local validation entry point: always build current source before installing it for engine tests.
#requires -Version 7.0
[CmdletBinding()]
param([string]$AddonsDirectory = '', [switch]$NonInteractive, [Alias('WithCDFCache')][switch]$WithCompanion)
$ErrorActionPreference = 'Stop'
$configPath = Join-Path $PSScriptRoot '.local/config.json'
$config = & (Join-Path $PSScriptRoot 'tools/Get-LocalConfig.ps1')
$project = & "$PSScriptRoot/tools/Get-ProjectConfig.ps1"
if ($WithCompanion -and !$project.companion) { throw 'No companion is configured in tools/project.json.' }
if (!$AddonsDirectory) { $AddonsDirectory = $config.BuildAddonsDirectory }
if (!$AddonsDirectory) {
 if ($NonInteractive) { throw 'Run ./build.ps1 once to choose your addons directory, or pass -AddonsDirectory.' }
 $default = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'My Games/ArmaReforgerWorkbench/addons'
 $answer = Read-Host "Build addons directory [$default]"
 $AddonsDirectory = $default
 if (![string]::IsNullOrWhiteSpace($answer)) { $AddonsDirectory = $answer.Trim().Trim('"') }
}
$AddonsDirectory = [IO.Path]::GetFullPath($AddonsDirectory)
& (Join-Path $PSScriptRoot 'tests/Test-LocalInstall.ps1') | Write-Host
$name = 'local-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff')
$build = & (Join-Path $PSScriptRoot 'tools/Build-Addon.ps1') -Name $name -Wait | ConvertFrom-Json
$installed = & (Join-Path $PSScriptRoot 'tools/Install-LocalAddon.ps1') -Source (Split-Path -Parent $build.source) -AddonsDirectory $AddonsDirectory
# Preserve other profile choices; save only after a successful build and installation.
$profile = @{}
if (Test-Path -LiteralPath $configPath) { $profile = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json -AsHashtable }
$profile.BuildAddonsDirectory = $AddonsDirectory
New-Item -ItemType Directory -Path (Split-Path -Parent $configPath) -Force | Out-Null
$profile | ConvertTo-Json | Set-Content -LiteralPath $configPath -Encoding utf8
$receipt = [ordered]@{ build=$name; installedAddon=$installed.installedAddon; backup=$installed.backup; files=$installed.files; nativeExitCode=$build.nativeExitCode; compiled=$build.succeeded; published=$false }
$receipt | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $PSScriptRoot '.local/last-build.json') -Encoding utf8
if ($WithCompanion) {
 $bridgeBuild = & (Join-Path $PSScriptRoot 'tools/Build-Addon.ps1') -Name ($name + '-companion') -Wait -Companion | ConvertFrom-Json
 $bridge = & (Join-Path $PSScriptRoot 'tools/Install-LocalAddon.ps1') -Source (Split-Path -Parent $bridgeBuild.source) -AddonsDirectory $AddonsDirectory -AddonName $project.companion.name
 $receipt.cdfCompanion = @{ installedAddon=$bridge.installedAddon; nativeExitCode=$bridgeBuild.nativeExitCode; compiled=$bridgeBuild.succeeded; backup=$bridge.backup }
 $receipt | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $PSScriptRoot '.local/last-build.json') -Encoding utf8
}
$receipt | ConvertTo-Json

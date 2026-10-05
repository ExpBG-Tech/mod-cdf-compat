#requires -Version 7.0
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$root = Join-Path $repo ('.local/portable-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $root -Force | Out-Null
function Rejects([scriptblock]$Action) {
 $rejected = $false
 try { & $Action | Out-Null } catch { $rejected = $true }
 if (!$rejected) { throw 'Expected invalid input to be rejected.' }
}
$configScript = Join-Path $repo 'tools/Get-LocalConfig.ps1'
$emptyConfig = Join-Path $root 'config.json'
'{}' | Set-Content -LiteralPath $emptyConfig
$previous = ${env:ProgramFiles(x86)}
try {
 Remove-Item 'Env:ProgramFiles(x86)' -ErrorAction SilentlyContinue
 $config = & $configScript -ConfigPath $emptyConfig
 if ($null -eq $config.BuildAddonsDirectory -or $null -eq $config.DependencyAddonsRoots) { throw 'Missing local build setting.' }
} finally { ${env:ProgramFiles(x86)} = $previous }
@{ GameRoot=(Join-Path $root 'game with spaces') } | ConvertTo-Json | Set-Content -LiteralPath $emptyConfig
$config = & $configScript -ConfigPath $emptyConfig
if ($config.GameRoot -ne (Join-Path $root 'game with spaces')) { throw 'Path override was not preserved.' }
# Dependency search defaults to the (overridden) Workshop root first; an explicit list replaces it.
@{ InstalledAddonsRoot=(Join-Path $root 'workshop addons') } | ConvertTo-Json | Set-Content -LiteralPath $emptyConfig
$config = & $configScript -ConfigPath $emptyConfig
if (($config.DependencyAddonsRoots -split ';')[0] -ne (Join-Path $root 'workshop addons')) { throw 'Dependency search does not start with the Workshop addons root.' }
@{ DependencyAddonsRoots='C:/first root;D:/second root' } | ConvertTo-Json | Set-Content -LiteralPath $emptyConfig
$config = & $configScript -ConfigPath $emptyConfig
if ($config.DependencyAddonsRoots -cne 'C:/first root;D:/second root') { throw 'Explicit dependency search order was not preserved.' }
foreach ($bad in @('{"UnknownSetting":"x"}', '{"GameRoot":42}')) {
 $bad | Set-Content -LiteralPath $emptyConfig
 Rejects { & $configScript -ConfigPath $emptyConfig }
}

# Copy real shared tools into a repository with a different source directory, name and GUID.
$fixture = Join-Path $root 'source with spaces'
$other = Join-Path $fixture 'runtime/relay-source'
New-Item -ItemType Directory -Path "$fixture/tools","$other/Scripts/Game/Example", "$other/UI" -Force | Out-Null
foreach ($tool in @('Test-Repository.ps1','Stage-Release.ps1','Get-ReleaseConfig.ps1','Get-ProjectConfig.ps1','Get-LocalConfig.ps1','Copy-AddonDependencies.ps1','Install-LocalAddon.ps1','Workshop-Common.ps1')) {
 Copy-Item -LiteralPath "$repo/tools/$tool" -Destination "$fixture/tools"
}
'GameProject { ID Example_Relay GUID A123456789ABCDEF Dependencies { "58D0FB3206B6F859" "B123456789ABCDEF" "D123456789ABCDEF" } }' | Set-Content "$other/Example_Relay.gproj"
'// alternate addon fixture' | Set-Content "$other/Scripts/Game/Example/Example.c"
'fixture preview' | Set-Content "$other/UI/Preview.png"
'MetaFileClass { Name "{A123456789ABCDE0}UI/Preview.png" }' | Set-Content "$other/UI/Preview.png.meta"
$identity = @{
 addon = @{name='Example_Relay';source='runtime/relay-source';dependencies=@('58D0FB3206B6F859','B123456789ABCDEF','D123456789ABCDEF');runtimePaths=@('Scripts/Game/Example','UI');preview='UI/Preview.png';installedDependencies=@{B123456789ABCDEF='External_Dependency';D123456789ABCDEF=@('Workshop_Pack','Local_Pack')}}
 legacyPublishWorkflows = @()
}
$identity | ConvertTo-Json -Depth 6 | Set-Content "$fixture/tools/project.json"
@{id='A123456789ABCDEF';name='Example Relay';unlisted=$false;private=$true;description='Example {{VERSION}}'} | ConvertTo-Json | Set-Content "$fixture/tools/workshop-asset.json"
'1.2.3' | Set-Content "$fixture/VERSION"
'fixture license' | Set-Content "$fixture/LICENSE"
"artifacts/`n**/resourceDatabase.rdb" | Set-Content "$fixture/.gitignore"
& git -C $fixture init --quiet
if ($LASTEXITCODE -ne 0) { throw 'Fixture Git initialization failed.' }
& git -C $fixture add .
if ($LASTEXITCODE -ne 0) { throw 'Fixture Git staging failed.' }
& git -C $fixture -c user.name=Fixture -c user.email=fixture@example.invalid commit --quiet -m fixture
if ($LASTEXITCODE -ne 0) { throw 'Fixture Git commit failed.' }
& "$fixture/tools/Test-Repository.ps1" | Out-Host
$alternate = & "$fixture/tools/Stage-Release.ps1" -Name alternate | ConvertFrom-Json
if ($alternate.compiled -or $alternate.published -or $alternate.runtimeFiles -ne 4) { throw 'Source archive inventory or native claims are wrong.' }
if ((Split-Path $alternate.stage -Leaf) -ne 'Example_Relay' -or (Split-Path $alternate.zip -Leaf) -ne 'Example_Relay_1.2.3_source.zip') { throw 'Alternate addon archive retained the original identity.' }
if (!(Test-Path "$($alternate.stage)/Example_Relay.gproj") -or (Get-ChildItem "$($alternate.stage)" -Recurse -File | Where-Object Name -like '*EXPBG*')) { throw 'Alternate addon archive contains foreign runtime.' }
Rejects { & "$fixture/tools/Stage-Release.ps1" -Name alternate }

$meta = "$other/UI/Preview.png.meta"
$originalMeta = [IO.File]::ReadAllText($meta)
foreach ($wrongPath in @('ui/Preview.png','../UI/Preview.png')) {
 try {
  [IO.File]::WriteAllText($meta, $originalMeta.Replace('UI/Preview.png', $wrongPath))
  Rejects { & "$fixture/tools/Test-Repository.ps1" }
 } finally { [IO.File]::WriteAllText($meta, $originalMeta) }
}
$badFile = Join-Path $other 'private.txt'
'not runtime' | Set-Content -LiteralPath $badFile
Rejects { & "$fixture/tools/Stage-Release.ps1" -Name forbidden }
if (Test-Path -LiteralPath "$fixture/artifacts/forbidden") { throw 'Forbidden runtime content was staged.' }
Remove-Item -LiteralPath $badFile
New-Item -ItemType Directory -Path "$other/Scripts/Game/Example/tests" | Out-Null
'// must not deploy' | Set-Content "$other/Scripts/Game/Example/tests/fixture.c"
Rejects { & "$fixture/tools/Test-Repository.ps1" }
Remove-Item -LiteralPath "$other/Scripts/Game/Example/tests/fixture.c"
$external = Join-Path $root 'external'
New-Item -ItemType Directory -Path $external | Out-Null
$link = Join-Path $other 'Linked'
$linkType = 'SymbolicLink'
if ($IsWindows) { $linkType = 'Junction' }
New-Item -ItemType $linkType -Path $link -Target $external | Out-Null
Rejects { & "$fixture/tools/Test-Repository.ps1" }
Remove-Item -LiteralPath $link

'index' | Set-Content "$other/resourceDatabase.rdb"
$installed = & "$fixture/tools/Install-LocalAddon.ps1" -Source $other -AddonsDirectory "$root/alternate-addons"
if ((Split-Path $installed.installedAddon -Leaf) -ne 'Example_Relay') { throw 'Alternate installation retained the original name.' }
$dependency = Join-Path $root 'installed/External_Dependency'
New-Item -ItemType Directory -Path $dependency -Force | Out-Null
'GameProject { GUID B123456789ABCDEF }' | Set-Content "$dependency/addon.gproj"
'indexed' | Set-Content "$dependency/resourceDatabase.rdb"
# The pack dependency exists only as a local Workbench build under its second candidate name.
$localPack = Join-Path $root 'workbench addons/Local_Pack'
New-Item -ItemType Directory -Path $localPack -Force | Out-Null
'GameProject { GUID "D123456789ABCDEF" }' | Set-Content "$localPack/Local_Pack.gproj"
$searchRoots = @("$root/installed", "$root/workbench addons")
Rejects { & "$fixture/tools/Copy-AddonDependencies.ps1" -Destination "$root/workshop-only" -InstalledAddonsRoot "$root/installed" }
& "$fixture/tools/Copy-AddonDependencies.ps1" -Destination "$root/dependencies" -InstalledAddonsRoot "$root/installed" -SearchRoots $searchRoots
if (!(Test-Path "$root/dependencies/B123456789ABCDEF/addon.gproj") -or !(Test-Path "$root/dependencies/D123456789ABCDEF/Local_Pack.gproj")) { throw 'Declared build dependencies were not frozen.' }
$frozen = @(Get-Content "$root/dependencies.json" -Raw | ConvertFrom-Json)
if ($frozen.Count -ne 2 -or ($frozen | Where-Object id -eq 'D123456789ABCDEF').source -ne (Join-Path "$root/workbench addons" 'Local_Pack') -or ($frozen | Where-Object id -eq 'D123456789ABCDEF').packed) { throw 'Dependency receipt does not record the local build source.' }
# A Workshop download of the same identity takes precedence over the local build.
$workshopPack = Join-Path $root 'installed/Workshop_Pack'
New-Item -ItemType Directory -Path $workshopPack | Out-Null
'GameProject { GUID "D123456789ABCDEF" }' | Set-Content "$workshopPack/addon.gproj"
'packed' | Set-Content "$workshopPack/data.pak"
& "$fixture/tools/Copy-AddonDependencies.ps1" -Destination "$root/dependencies-workshop" -SearchRoots $searchRoots
$frozen = @(Get-Content "$root/dependencies-workshop.json" -Raw | ConvertFrom-Json)
if (($frozen | Where-Object id -eq 'D123456789ABCDEF').source -ne $workshopPack -or !($frozen | Where-Object id -eq 'D123456789ABCDEF').packed) { throw 'Workshop dependency did not take precedence.' }
'GameProject { GUID "D000000000000000" }' | Set-Content "$workshopPack/addon.gproj"
Rejects { & "$fixture/tools/Copy-AddonDependencies.ps1" -Destination "$root/wrong-pack" -SearchRoots $searchRoots }
'GameProject { GUID "D123456789ABCDEF" }' | Set-Content "$workshopPack/addon.gproj"
'GameProject { GUID B000000000000000 }' | Set-Content "$dependency/addon.gproj"
Rejects { & "$fixture/tools/Copy-AddonDependencies.ps1" -Destination "$root/wrong-dependency" -SearchRoots $searchRoots }
. "$fixture/tools/Workshop-Common.ps1"
$settings = & "$fixture/tools/Get-ProjectConfig.ps1"
$packed = Join-Path $root 'alternate-packed'
New-Item -ItemType Directory -Path $packed | Out-Null
Copy-Item "$other/Example_Relay.gproj", "$other/resourceDatabase.rdb" $packed
'payload' | Set-Content "$packed/data.pak"
Write-WorkshopManifest $packed "$fixture/tools/workshop-asset.json" '1.2.3' 'alternate proof' "$other/UI/Preview.png" $settings.addon.name
Assert-WorkshopPackage $packed -Version '1.2.3' -Project $settings
$identity.companion = @{name='Example_Companion';source='compat/example';id='C123456789ABCDEF';dependencies=@('58D0FB3206B6F859','A123456789ABCDEF');runtimePaths=@('Scripts')}
$identity | ConvertTo-Json -Depth 6 | Set-Content "$fixture/tools/project.json"
New-Item -ItemType Directory -Path "$fixture/.local" | Out-Null
@{compiled=$true;installedAddon=$installed.installedAddon} | ConvertTo-Json | Set-Content "$fixture/.local/last-build.json"
& "$fixture/tools/Copy-AddonDependencies.ps1" -Destination "$root/companion-dependencies" -InstalledAddonsRoot "$root/installed" -Companion
if (!(Test-Path "$root/companion-dependencies/A123456789ABCDEF/Example_Relay.gproj")) { throw 'A companion with no external dependencies lost its core dependency.' }
$identity.addon.source = '../outside'
$identity | ConvertTo-Json -Depth 6 | Set-Content "$fixture/tools/project.json"
Rejects { & "$fixture/tools/Get-ProjectConfig.ps1" }
$identity.addon.source = 'runtime/relay-source'
$identity.addon.dependencies = @('A123456789ABCDEF')
$identity | ConvertTo-Json -Depth 6 | Set-Content "$fixture/tools/project.json"
Rejects { & "$fixture/tools/Get-ProjectConfig.ps1" }
$identity.addon.dependencies = @('58D0FB3206B6F859','B123456789ABCDEF','D123456789ABCDEF')
foreach ($badNames in @(@(), @('Valid_Name','../escape'))) {
 $identity.addon.installedDependencies.D123456789ABCDEF = $badNames
 $identity | ConvertTo-Json -Depth 6 | Set-Content "$fixture/tools/project.json"
 Rejects { & "$fixture/tools/Get-ProjectConfig.ps1" }
}

# The canonical test runner must stop on an explicit child failure, even before a later success.
$runner = Join-Path $root 'runner'
New-Item -ItemType Directory -Path "$runner/tests","$runner/tools" -Force | Out-Null
Copy-Item "$repo/tests/Test-Tools.ps1" "$runner/tests"
"'source checked'" | Set-Content "$runner/tools/Test-Repository.ps1"
'exit 9' | Set-Content "$runner/tests/Test-AFailure.ps1"
"Set-Content -LiteralPath '$runner/unexpected.txt' -Value ran" | Set-Content "$runner/tests/Test-ZLater.ps1"
$pwsh = Join-Path $PSHOME $(if ($IsWindows) {'pwsh.exe'} else {'pwsh'})
& $pwsh -NoProfile -File "$runner/tests/Test-Tools.ps1" *> "$root/expected-runner-failure.log"
if ($LASTEXITCODE -eq 0 -or (Test-Path "$runner/unexpected.txt")) { throw 'Portable test runner hid a failed child or continued after failure.' }
"Add-Content -LiteralPath '$runner/order.txt' -Value first" | Set-Content "$runner/tests/Test-AFailure.ps1"
"Add-Content -LiteralPath '$runner/order.txt' -Value second" | Set-Content "$runner/tests/Test-ZLater.ps1"
& $pwsh -NoProfile -File "$runner/tests/Test-Tools.ps1" *> "$root/runner-order.log"
if ($LASTEXITCODE -ne 0 -or ((Get-Content "$runner/order.txt") -join ',') -cne 'first,second') { throw 'Portable test discovery was incomplete or not deterministic.' }
'PASS: alternate source/name/GUID, private visibility, source archive, installation, ordered multi-root dependency identity and receipt, payload/metadata/link guards, config rejection and test-runner failfast.'
exit 0

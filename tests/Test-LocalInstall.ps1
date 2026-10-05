#requires -Version 7.0
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$settings = & "$repo/tools/Get-ProjectConfig.ps1"
$root = Join-Path $repo ('.local/install-test-' + [guid]::NewGuid().ToString('N'))
$addons = Join-Path $root 'addons'
New-Item -ItemType Directory -Path "$addons/OtherAddon" -Force | Out-Null
Set-Content "$addons/OtherAddon/keep.txt" 'unrelated'
$install = Join-Path $repo 'tools/Install-LocalAddon.ps1'
$coreHash = $null
foreach ($entry in @($settings.addon) + @($settings.companion | Where-Object { $_ })) {
 $source = Join-Path $root $entry.name
 $runtimePath = $entry.runtimePaths[0]
 New-Item -ItemType Directory -Path "$source/$runtimePath","$source/Docs" -Force | Out-Null
 Set-Content "$source/$($entry.project)" ('GameProject { GUID ' + $entry.id + ' }')
 Set-Content "$source/$runtimePath/example.c" '// fixture'
 Set-Content "$source/resourceDatabase.rdb" 'test index'
 Set-Content "$source/Docs/private.md" 'must not deploy'
 $first = & $install -Source $source -AddonsDirectory $addons -AddonName $entry.name
 if (!(Test-Path "$($first.installedAddon)/resourceDatabase.rdb") -or (Test-Path "$($first.installedAddon)/Docs") -or !(Test-Path "$($first.installedAddon)/$runtimePath/example.c")) { throw 'Runtime-only copy failed.' }
 Set-Content "$($first.installedAddon)/stale.txt" 'old content'
 $second = & $install -Source $source -AddonsDirectory $addons -AddonName $entry.name
 if ((Test-Path "$($second.installedAddon)/stale.txt") -or !(Test-Path "$($second.backup)/stale.txt")) { throw 'Replacement must remove stale files and retain backup.' }
 if (!(Test-Path "$addons/OtherAddon/keep.txt")) { throw 'Another addon was changed.' }
 $rejected = $false
 try { & $install -Source $source -AddonsDirectory $entry.sourcePath -AddonName $entry.name | Out-Null } catch { $rejected = $true }
 if (!$rejected) { throw 'Editable source destination was not rejected.' }
 $before = (Get-FileHash "$($second.installedAddon)/$($entry.project)").Hash
 Remove-Item -LiteralPath "$source/resourceDatabase.rdb"
 $rejected = $false
 try { & $install -Source $source -AddonsDirectory $addons -AddonName $entry.name | Out-Null } catch { $rejected = $true }
 if (!$rejected -or (Get-FileHash "$($second.installedAddon)/$($entry.project)").Hash -ne $before) { throw 'Invalid source damaged existing deployment.' }
 $coreProject = Join-Path $addons ($settings.addon.name + '/' + $settings.addon.project)
 if ($coreHash -and (Get-FileHash -LiteralPath $coreProject).Hash -ne $coreHash) { throw 'Companion replaced the core addon.' }
 $coreHash = (Get-FileHash -LiteralPath $coreProject).Hash
}
'PASS: configured addon runtime filtering, replacement/backup, sibling protection, source overlap and invalid-build protection.'

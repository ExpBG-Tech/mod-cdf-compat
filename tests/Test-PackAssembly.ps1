#requires -Version 7.0
# Exercise the real pack assembler on disposable fixtures: declared shared-config merges,
# undeclared path collisions, duplicate resource GUIDs, Workbench-only scripts and other
# structural errors must fail. Also assembles this repository's modules once.
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$root = Join-Path $repo ('.local/pack-assembly-test-' + [guid]::NewGuid().ToString('N'))
function Rejects([scriptblock]$Action, [string]$Pattern) {
 try { & $Action | Out-Null } catch {
  if ($_.Exception.Message -notmatch $Pattern) { throw "Unexpected rejection: $($_.Exception.Message)" }
  return
 }
 throw "Expected rejection: $Pattern"
}
function Write-Text([string]$Path, [string]$Text) {
 New-Item -ItemType Directory -Path (Split-Path -Parent $Path) -Force | Out-Null
 [IO.File]::WriteAllText($Path, $Text)
}
$fixture = Join-Path $root 'fixture'
$addon = Join-Path $fixture 'addon'
New-Item -ItemType Directory -Path "$fixture/tools" -Force | Out-Null
Copy-Item -LiteralPath "$repo/tools/Assemble-Pack.ps1" -Destination "$fixture/tools"
$assemble = Join-Path $fixture 'tools/Assemble-Pack.ps1'
$run = 0
function Assemble { $script:run++; & $assemble -Destination (Join-Path $root "out-$script:run") }
function Pack([string[]]$Merge = @(), [hashtable]$Relocate = @{}) {
 [ordered]@{ project='Fixture.gproj'; modules=@(@{name='alpha'}, @{name='beta'}); merge=$Merge; relocate=$Relocate } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath "$fixture/tools/pack.json"
}
$editConf = 'Configs/Editor/AttributeLists/Edit.conf'
$editMeta = 'MetaFileClass {' + "`n" + ' Name "{BB5C6A9E61F5C1F1}' + $editConf + '"' + "`n}`n"
try {
 Write-Text "$addon/Fixture.gproj" 'GameProject { ID "Fixture" GUID "F123456789ABCDEF" }'
 Write-Text "$addon/alpha/Scripts/Game/Alpha/Alpha.c" "class FixtureAlpha {}`r`n"
 Write-Text "$addon/alpha/UI/Alpha.edds.meta" 'MetaFileClass { Name "{A000000000000001}UI/Alpha.edds" }'
 Write-Text "$addon/alpha/UI/Alpha.edds" 'alpha texture'
 Write-Text "$addon/beta/Scripts/Game/Beta/Beta.c" "class FixtureBeta {}`n"
 Write-Text "$addon/alpha/$editConf" ("SCR_EditorAttributeList {`n m_aAttributes + {`n  AlphaAttribute {`n  }`n }`n}`n")
 Write-Text "$addon/alpha/$editConf.meta" $editMeta
 Write-Text "$addon/beta/$editConf" ("SCR_EditorAttributeList {`r`n m_aAttributes + {`r`n  BetaAttribute {`r`n  }`r`n }`r`n}`r`n")
 Write-Text "$addon/beta/$editConf.meta" $editMeta

 # An overridden shared config without a merge declaration is an undeclared collision.
 Pack
 Rejects { Assemble } 'Undeclared collision'
 Pack @($editConf)
 $result = Assemble | ConvertFrom-Json
 $merged = Get-Content -LiteralPath (Join-Path $result.destination $editConf) -Raw
 if ($merged -notmatch 'AlphaAttribute' -or $merged -notmatch 'BetaAttribute' -or $merged.IndexOf('AlphaAttribute') -gt $merged.IndexOf('BetaAttribute')) { throw 'Shared config was not merged in module order.' }
 if (@($result.merged).Count -ne 1 -or (@($result.merged[0].modules) -join ',') -cne 'alpha,beta') { throw 'Merge report is incomplete.' }
 $copied = [IO.File]::ReadAllBytes((Join-Path $result.destination 'Scripts/Game/Alpha/Alpha.c'))
 if ([Convert]::ToBase64String($copied) -cne [Convert]::ToBase64String([IO.File]::ReadAllBytes("$addon/alpha/Scripts/Game/Alpha/Alpha.c"))) { throw 'Module source bytes or line endings changed.' }
 Rejects { & $assemble -Destination $result.destination } 'new or empty'

 # A shared config must keep exactly one vanilla identity in every module.
 Write-Text "$addon/beta/$editConf.meta" ($editMeta.Replace('BB5C6A9E61F5C1F1', 'BB5C6A9E61F5C1F2'))
 Rejects { Assemble } 'one vanilla identity'
 Write-Text "$addon/beta/$editConf.meta" $editMeta
 Remove-Item -LiteralPath "$addon/beta/$editConf.meta"
 Rejects { Assemble } 'one config and metadata'
 Write-Text "$addon/beta/$editConf.meta" $editMeta

 # Path collisions are case-insensitive, like the engine's resource lookup on Windows.
 Write-Text "$addon/beta/scripts/game/alpha/alpha.c" "class FixtureCopy {}`n"
 Rejects { Assemble } 'Undeclared collision'
 Remove-Item -LiteralPath "$addon/beta/scripts/game/alpha" -Recurse -Force

 Write-Text "$addon/beta/UI/Beta.edds.meta" 'MetaFileClass { Name "{A000000000000001}UI/Beta.edds" }'
 Rejects { Assemble } 'Duplicate resource GUID'
 Write-Text "$addon/beta/UI/Beta.edds.meta" 'MetaFileClass { Name "{A000000000000002}UI/Beta.edds" }'
 $null = Assemble

 Write-Text "$addon/beta/Scripts/WorkbenchGame/Plugin/Handler.c" "class FixtureHandler {}`n"
 Rejects { Assemble } 'Workbench-only'
 Remove-Item -LiteralPath "$addon/beta/Scripts/WorkbenchGame" -Recurse -Force

 Write-Text "$addon/beta/Beta.gproj" 'GameProject { GUID "B123456789ABCDEF" }'
 Rejects { Assemble } 'own project'
 Remove-Item -LiteralPath "$addon/beta/Beta.gproj"

 Write-Text "$addon/stray.txt" 'not a module'
 Rejects { Assemble } 'Unexpected pack root'
 Remove-Item -LiteralPath "$addon/stray.txt"

 Pack @($editConf) @{ 'beta/UI/Missing.png'='UI/Relocated.png' }
 Rejects { Assemble } 'Stale relocation'
 Write-Text "$addon/beta/UI/Notice.txt" 'beta notice'
 Pack @($editConf) @{ 'beta/UI/Notice.txt'='Licenses/Beta_Notice.txt' }
 $relocated = Assemble | ConvertFrom-Json
 if (!(Test-Path -LiteralPath (Join-Path $relocated.destination 'Licenses/Beta_Notice.txt')) -or (Test-Path -LiteralPath (Join-Path $relocated.destination 'UI/Notice.txt'))) { throw 'Declared relocation was not applied.' }

 Pack @($editConf)
 Remove-Item -LiteralPath "$addon/beta" -Recurse -Force
 Rejects { Assemble } 'Missing module folder'

 # This repository: every module assembles without merges, collisions or duplicate GUIDs.
 $pack = Get-Content -LiteralPath "$repo/tools/pack.json" -Raw | ConvertFrom-Json -AsHashtable
 $actual = & "$repo/tools/Assemble-Pack.ps1" -Destination (Join-Path $root 'repository') | ConvertFrom-Json
 if ((@($actual.modules) -join ',') -cne (@($pack.modules | ForEach-Object { $_.name }) -join ',') -or @($actual.merged).Count -ne @($pack.merge).Count) { throw 'Repository assembly report differs from tools/pack.json.' }
 $expected = 1 + @(Get-ChildItem -LiteralPath "$repo/addon" -Directory | ForEach-Object { Get-ChildItem -LiteralPath $_.FullName -Recurse -File }).Count
 if ($actual.files -ne $expected -or @(Get-ChildItem -LiteralPath $actual.destination -Recurse -File).Count -ne $expected) { throw 'Repository assembly lost or added files.' }
} finally {
 # Only this newly created fixture under .local is removed.
 if ([IO.Path]::GetFullPath($root).StartsWith([IO.Path]::GetFullPath("$repo/.local/"), [StringComparison]::OrdinalIgnoreCase)) { Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue }
}
'PASS: pack assembly merges declared shared configs in module order, preserves bytes and rejects undeclared/case collisions, duplicate GUIDs, Workbench-only scripts, module projects, stray root content, stale relocations and missing modules.'
exit 0

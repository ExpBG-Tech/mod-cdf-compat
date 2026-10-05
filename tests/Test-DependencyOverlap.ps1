#requires -Version 7.0
# Cross-check the assembled pack against EXPBG GM Tools at the pinned commit in tools/pack.json.
# A same-path file in this dependent addon would replace the GM Tools file; a duplicate resource
# GUID or class name breaks loading/compilation. Every EXPBG symbol, resource reference and
# addon/prefab identity literal used by the merged scripts must exist in GM Tools (or here).
# Reads Git objects only; the GM Tools working tree is never touched. Skips when no checkout
# is found (CI): set EXPBG_GM_TOOLS_REPO or place mod-gm-tools beside this repository.
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$pack = Get-Content -LiteralPath "$repo/tools/pack.json" -Raw | ConvertFrom-Json -AsHashtable
$pin = $pack.dependencies.gmTools
$gm = $env:EXPBG_GM_TOOLS_REPO
if (!$gm) { $gm = Join-Path (Split-Path -Parent $repo) 'mod-gm-tools' }
if (!(Test-Path -LiteralPath (Join-Path $gm '.git')) -or !(& git -C $gm rev-parse --verify --quiet "$($pin.commit)^{commit}" 2>$null)) {
 "SKIP: EXPBG GM Tools checkout with commit $($pin.commit) not found; cross-addon overlap checks need it locally."
 exit 0
}
function Read-GmTools([string[]]$Arguments) {
 $output = & git -C $gm @Arguments
 if ($LASTEXITCODE -ne 0) { throw "git $($Arguments[0]) failed in $gm" }
 return $output
}
$gmPack = (Read-GmTools @('show', "$($pin.commit):tools/pack.json")) -join "`n" | ConvertFrom-Json -AsHashtable
$gmModules = @($gmPack.modules | ForEach-Object { $_.name })
$gmProject = (Read-GmTools @('show', "$($pin.commit):addon/$($gmPack.project)")) -join "`n"
if ($gmProject -cnotmatch ('\bGUID\s+"?' + $pin.id + '\b')) { throw 'Pinned commit is not the configured EXPBG GM Tools identity.' }

# GM Tools runtime layout: module-relative paths after its declared relocations.
$gmPaths = @{}; $metaBlobs = [Collections.Generic.List[string]]::new(); $scriptBlobs = [Collections.Generic.List[string]]::new()
$tree = ((Read-GmTools @('ls-tree', '-r', '-z', '--full-tree', $pin.commit, '--', 'addon')) -join '') -split [char]0
foreach ($line in $tree | Where-Object { $_ }) {
 $header, $path = $line -split "`t", 2
 $sha = ($header -split ' ')[2]
 if ($path -cnotmatch '^addon/([^/]+)/(.+)$' -or $Matches[1] -cnotin $gmModules) { continue }
 $origin = "$($Matches[1])/$($Matches[2])"
 $relative = $Matches[2]
 if ($gmPack.relocate -and $gmPack.relocate.ContainsKey($origin)) { $relative = $gmPack.relocate[$origin] }
 $gmPaths[$relative.ToLowerInvariant()] = $origin
 if ($relative -like '*.meta') { $metaBlobs.Add($sha) }
 if ($relative -like '*.c') { $scriptBlobs.Add($sha) }
}
function Read-Blobs([Collections.Generic.List[string]]$Blobs) { (($Blobs | & git -C $gm cat-file --batch) -join "`n") }
$gmMeta = Read-Blobs $metaBlobs
$gmScripts = Read-Blobs $scriptBlobs
if ($LASTEXITCODE -ne 0 -or !$gmMeta -or !$gmScripts) { throw 'Unable to read GM Tools objects.' }
$gmResources = @{}
foreach ($match in [regex]::Matches($gmMeta, 'Name\s+"\{([A-Fa-f0-9]{16})\}([^"\r\n]+)"')) { $gmResources[$match.Groups[1].Value.ToUpperInvariant()] = $match.Groups[2].Value }
$gmClasses = @{}
foreach ($match in [regex]::Matches($gmScripts, '(?m)^\s*class\s+(\w+)')) { $gmClasses[$match.Groups[1].Value] = $true }
$gmWords = @{}
foreach ($match in [regex]::Matches($gmScripts, '\b(?:EBG|EII|EAD|EXPG|EAC|EAS)_\w+')) { $gmWords[$match.Value] = $true }

# This pack, assembled exactly as build and release assemble it.
$temporary = Join-Path ([IO.Path]::GetTempPath()) ('expbg-cdf-overlap-' + [guid]::NewGuid().ToString('N'))
try {
 $assembled = Join-Path $temporary $pack.project.Replace('.gproj', '')
 & "$repo/tools/Assemble-Pack.ps1" -Destination $assembled | Out-Null
 $problems = [Collections.Generic.List[string]]::new()
 $files = @(Get-ChildItem -LiteralPath $assembled -Recurse -File)
 $scripts = ''
 foreach ($file in $files) {
  $relative = $file.FullName.Substring($assembled.Length + 1).Replace('\','/')
  if ($gmPaths.ContainsKey($relative.ToLowerInvariant())) { $problems.Add("Path $relative would replace GM Tools $($gmPaths[$relative.ToLowerInvariant()])") }
  if ($file.Extension -eq '.meta') {
   $id = [regex]::Match((Get-Content -LiteralPath $file.FullName -Raw), 'Name\s+"\{([A-Fa-f0-9]{16})\}').Groups[1].Value.ToUpperInvariant()
   if ($gmResources.ContainsKey($id)) { $problems.Add("Resource GUID $id duplicates GM Tools $($gmResources[$id])") }
  }
  if ($file.Extension -eq '.c') { $scripts += "`n" + (Get-Content -LiteralPath $file.FullName -Raw) }
 }
 $declared = @{}
 foreach ($match in [regex]::Matches($scripts, '(?m)^\s*class\s+(\w+)')) {
  $declared[$match.Groups[1].Value] = $true
  if ($gmClasses.ContainsKey($match.Groups[1].Value)) { $problems.Add("Class $($match.Groups[1].Value) is already declared by GM Tools") }
 }
 # Names declared here: classes, methods, fields and locals introduced by a type token.
 foreach ($match in [regex]::Matches($scripts, '(?m)^\s*(?:(?:override|protected|private|static|ref|const)\s+)*[A-Za-z_]\w*(?:<[^>\r\n]*>)?\s+([A-Za-z_]\w*)\s*[(;=,]')) { $declared[$match.Groups[1].Value] = $true }
 foreach ($match in [regex]::Matches($scripts, '(?m)^\s*modded\s+class\s+((?:EBG|EII|EAD|EXPG)_\w+)')) {
  if (!$gmClasses.ContainsKey($match.Groups[1].Value)) { $problems.Add("Modded class $($match.Groups[1].Value) does not exist in GM Tools") }
 }
 # Resource references are checked separately below; their file names are not symbols.
 $symbols = [regex]::Replace($scripts, '\{[A-F0-9]{16}\}[A-Za-z0-9_./-]+', '')
 foreach ($word in @([regex]::Matches($symbols, '\b(?:EBG|EII|EAD|EXPG)_\w+') | ForEach-Object Value | Select-Object -Unique)) {
  if (!$gmWords.ContainsKey($word) -and !$declared.ContainsKey($word)) { $problems.Add("Symbol $word is neither in GM Tools nor declared here") }
 }
 foreach ($match in [regex]::Matches($scripts, '\{([A-F0-9]{16})\}([A-Za-z0-9_./-]+)')) {
  $id = $match.Groups[1].Value
  if ($gmResources[$id] -cne $match.Groups[2].Value) { $problems.Add("Resource reference {$id}$($match.Groups[2].Value) is not that GM Tools resource") }
 }
 $addons = @('58D0FB3206B6F859', $pin.id, $pack.dependencies.cdf.id)
 foreach ($match in [regex]::Matches($scripts, 'Contains\("([A-F0-9]{16})"\)')) {
  $id = $match.Groups[1].Value
  if ($id -cnotin $addons -and !$gmResources.ContainsKey($id)) { $problems.Add("Identity literal $id is neither a dependency addon nor a GM Tools resource GUID") }
 }
 foreach ($match in [regex]::Matches($scripts, '"([A-F0-9]{16})"')) {
  if ($match.Groups[1].Value -cnotin $addons -and !$gmResources.ContainsKey($match.Groups[1].Value)) { $problems.Add("Identity literal $($match.Groups[1].Value) is unknown") }
 }
 if ($problems.Count) { throw ("Dependency overlap/reference problems:`n" + ($problems -join "`n")) }
 "PASS: $($files.Count) assembled files against EXPBG GM Tools $($pin.version) ($($pin.commit.Substring(0,7))): no replaced paths, duplicate GUIDs or classes; modded classes, EXPBG symbols, resource references and identity literals resolve ($($gmResources.Count) GM Tools resources)."
} finally {
 if ([IO.Path]::GetFullPath($temporary).StartsWith([IO.Path]::GetFullPath([IO.Path]::GetTempPath()), [StringComparison]::OrdinalIgnoreCase)) { Remove-Item -LiteralPath $temporary -Recurse -Force -ErrorAction SilentlyContinue }
}
exit 0

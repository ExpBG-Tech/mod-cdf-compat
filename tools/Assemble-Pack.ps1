#requires -Version 7.0
# Assemble the module folders under addon/ into one engine project. Every module keeps
# its own source folder; build, tests and release all use this single assembly step.
# Undeclared path collisions, duplicate resource GUIDs and Workbench-only scripts fail.
[CmdletBinding()]
param([Parameter(Mandatory)][string]$Destination)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$root = Join-Path $repo 'addon'
$pack = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'pack.json') -Raw | ConvertFrom-Json -AsHashtable
$Destination = [IO.Path]::GetFullPath($Destination)
if ((Test-Path -LiteralPath $Destination) -and @(Get-ChildItem -LiteralPath $Destination -Force).Count) { throw 'Assemble into a new or empty directory.' }

function Find-Close([string]$Text, [int]$Open, [string]$Origin) {
 $depth = 0; $quoted = $false
 for ($i = $Open; $i -lt $Text.Length; $i++) {
  $c = $Text[$i]
  if ($quoted) { if ($c -eq '\') { $i++ } elseif ($c -eq '"') { $quoted = $false }; continue }
  if ($c -eq '"') { $quoted = $true }
  elseif ($c -eq '{') { $depth++ }
  elseif ($c -eq '}') { $depth--; if ($depth -eq 0) { return $i } }
 }
 throw "Unbalanced braces in $Origin"
}

# Shared vanilla overrides contain only array blocks under one root object.
function Read-SharedConfig([string]$Path, [string]$Origin) {
 $text = (Get-Content -LiteralPath $Path -Raw).TrimStart([char]0xFEFF).Replace("`r`n", "`n")
 $head = [regex]::Match($text, '^\s*(\w+)\s*(?::\s*"[^"]*")?\s*\{')
 if (!$head.Success) { throw "Unsupported shared config: $Origin" }
 $members = [Collections.Generic.List[object]]::new()
 $i = $head.Index + $head.Length
 while ($true) {
  while ($i -lt $text.Length -and [char]::IsWhiteSpace($text[$i])) { $i++ }
  if ($i -ge $text.Length) { throw "Unterminated shared config: $Origin" }
  if ($text[$i] -eq '}') { break }
  $block = [regex]::Match($text.Substring($i), '^(\w+)\s*\+?\s*\{')
  if (!$block.Success) { throw "Shared configs may contain only array blocks: $Origin" }
  $open = $i + $block.Length - 1
  $close = Find-Close $text $open $Origin
  $body = ($text.Substring($open + 1, $close - $open - 1) -replace '^[ \t]*\n', '').TrimEnd()
  $members.Add([pscustomobject]@{ Name = $block.Groups[1].Value; Body = $body })
  $i = $close + 1
 }
 if ($text.Substring($i + 1).Trim()) { throw "Trailing content in shared config: $Origin" }
 [pscustomobject]@{ Root = $head.Groups[1].Value; Members = $members }
}

$merge = @{}; foreach ($path in $pack.merge) { $merge[$path.ToLowerInvariant()] = $path }
$relocate = @{}; foreach ($key in $pack.relocate.Keys) { $relocate[$key] = $pack.relocate[$key] }
$usedRelocations = @{}
$placed = @{}
$shared = @{}
$moduleNames = @($pack.modules | ForEach-Object { $_.name })
foreach ($item in Get-ChildItem -LiteralPath $root -Force) {
 if ($item.PSIsContainer -and $item.Name -cin $moduleNames) { continue }
 if (!$item.PSIsContainer -and $item.Name -ceq $pack.project) { continue }
 throw "Unexpected pack root content: addon/$($item.Name)"
}
New-Item -ItemType Directory -Path $Destination -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $root $pack.project) -Destination (Join-Path $Destination $pack.project)
$copied = 0
foreach ($module in $pack.modules) {
 $moduleRoot = Join-Path $root $module.name
 if (!(Test-Path -LiteralPath $moduleRoot -PathType Container)) { throw "Missing module folder: addon/$($module.name)" }
 foreach ($file in Get-ChildItem -LiteralPath $moduleRoot -Recurse -File | Sort-Object FullName) {
  $relative = $file.FullName.Substring($moduleRoot.Length + 1).Replace('\', '/')
  $origin = "$($module.name)/$relative"
  if ($relative -match '(?i)\.gproj$') { throw "Module folders cannot carry their own project: $origin" }
  if ($relative -match '(?i)^Scripts/(WorkbenchGame|Workbench)/') { throw "Workbench-only scripts cannot be packed: $origin" }
  if ($relocate.ContainsKey($origin)) { $relative = $relocate[$origin]; $usedRelocations[$origin] = $true }
  $lower = $relative.ToLowerInvariant()
  if ($merge.ContainsKey(($lower -replace '\.meta$', ''))) {
   if (!$shared.ContainsKey($lower)) { $shared[$lower] = [Collections.Generic.List[object]]::new() }
   $shared[$lower].Add([pscustomobject]@{ Origin = $origin; Path = $file.FullName })
   continue
  }
  if ($placed.ContainsKey($lower)) { throw "Undeclared collision: $origin and $($placed[$lower])" }
  $placed[$lower] = $origin
  $target = Join-Path $Destination $relative
  New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
  Copy-Item -LiteralPath $file.FullName -Destination $target
  $copied++
 }
}
foreach ($key in $relocate.Keys) { if (!$usedRelocations.ContainsKey($key)) { throw "Stale relocation (source missing): $key" } }

$merged = @()
foreach ($path in $pack.merge) {
 $configs = @($shared[$path.ToLowerInvariant()])
 $metas = @($shared[$path.ToLowerInvariant() + '.meta'])
 if (!$configs.Count -or $configs.Count -ne $metas.Count) { throw "Shared config needs one config and metadata per module: $path" }
 $identities = @($metas | ForEach-Object { [regex]::Match((Get-Content -LiteralPath $_.Path -Raw), 'Name\s+"(\{[A-F0-9]{16}\}[^"]+)"').Groups[1].Value } | Select-Object -Unique)
 if ($identities.Count -ne 1 -or $identities[0] -cne ('{' + $identities[0].Substring(1, 16) + '}' + $path)) { throw "Shared config metadata must keep one vanilla identity: $path" }
 $parts = @($configs | ForEach-Object { Read-SharedConfig $_.Path $_.Origin })
 if (@($parts.Root | Select-Object -Unique).Count -ne 1) { throw "Shared config roots differ: $path" }
 $names = [Collections.Generic.List[string]]::new()
 foreach ($member in $parts.Members) { if (!$names.Contains($member.Name)) { $names.Add($member.Name) } }
 $text = [Text.StringBuilder]::new()
 [void]$text.Append("$($parts[0].Root) {`n")
 foreach ($name in $names) {
  [void]$text.Append(" $name + {`n")
  foreach ($member in $parts.Members | Where-Object Name -ceq $name) { if ($member.Body) { [void]$text.Append($member.Body + "`n") } }
  [void]$text.Append(" }`n")
 }
 [void]$text.Append("}`n")
 $target = Join-Path $Destination $path
 New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
 [IO.File]::WriteAllText($target, $text.ToString())
 Copy-Item -LiteralPath $metas[0].Path -Destination ($target + '.meta')
 $merged += [pscustomobject]@{ path = $path; modules = @($configs | ForEach-Object { $_.Origin.Split('/')[0] }) }
}

$guids = @{}
foreach ($meta in Get-ChildItem -LiteralPath $Destination -Recurse -File -Filter '*.meta') {
 $id = [regex]::Match((Get-Content -LiteralPath $meta.FullName -Raw), 'Name\s+"\{([A-Fa-f0-9]{16})\}').Groups[1].Value.ToUpperInvariant()
 if (!$id) { throw "Missing resource GUID: $($meta.FullName)" }
 if ($guids.ContainsKey($id)) { throw "Duplicate resource GUID $id in $($meta.FullName) and $($guids[$id])" }
 $guids[$id] = $meta.FullName
}
[pscustomobject]@{ destination = $Destination; modules = $moduleNames; files = $copied + 2 * $merged.Count + 1; merged = $merged } | ConvertTo-Json -Depth 4 -Compress

[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$settings = & "$PSScriptRoot/Get-ProjectConfig.ps1"
$runtimeCount = 0
$assembledRoots = @()
try {
foreach ($entry in @($settings.addon) + @($settings.companion | Where-Object { $_ })) {
$addon = $entry.sourcePath
if ($entry.assembled -and $entry -eq $settings.addon) {
 $addon = Join-Path ([IO.Path]::GetTempPath()) ('expbg-pack-' + [guid]::NewGuid().ToString('N') + '/' + $entry.name)
 & (Join-Path $PSScriptRoot 'Assemble-Pack.ps1') -Destination $addon | Out-Null
 $assembledRoots += Split-Path -Parent $addon
}
$allowed = '.acp','.c','.conf','.edds','.emat','.et','.gproj','.layout','.md','.meta','.png','.sig','.st','.txt','.wav','.xob'
$runtime = @(Get-ChildItem -LiteralPath $addon -Recurse -File | Where-Object Name -ne 'resourceDatabase.rdb')
$runtimeCount += $runtime.Count
if ((Get-Item -LiteralPath $addon).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Addon source cannot be linked.' }
$runtimePattern = '^(' + [regex]::Escape($entry.project) + '|(?:' + (@($entry.runtimePaths | ForEach-Object { [regex]::Escape($_) }) -join '|') + ')/.+)$'
foreach ($file in $runtime) {
 $relative = $file.FullName.Substring($addon.Length + 1).Replace('\','/')
 if ($file.Extension -notin $allowed -or $relative -cnotmatch $runtimePattern -or $relative -match '(?i)(^|/)(Docs|tests?|tools|logs?|profile|evidence)(/|$)') { throw "Non-runtime content in addon: $relative" }
 if ($file.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Linked file in addon: $relative" }
 if ($file.Extension -in '.conf','.edds','.emat','.et','.layout','.st' -and !(Test-Path -LiteralPath ($file.FullName + '.meta'))) { throw "Missing resource metadata: $relative" }
}
if (Get-ChildItem -LiteralPath $addon -Recurse -Directory | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }) { throw 'Addon contains linked directories.' }
$project = Get-Content -LiteralPath (Join-Path $addon $entry.project) -Raw
if ($project -cnotmatch ('\bGUID\s+"?' + $entry.id + '\b')) { throw 'Project identity differs from repository configuration.' }
$dependencyBlock = [regex]::Match($project, '(?s)Dependencies\s*\{([^}]*)\}')
$dependencies = @([regex]::Matches($dependencyBlock.Groups[1].Value, '[A-Fa-f0-9]{16}') | ForEach-Object Value)
if ($dependencies.Count -ne $entry.dependencies.Count -or (Compare-Object $dependencies $entry.dependencies -CaseSensitive)) { throw 'Project dependencies differ from repository configuration.' }
$metadataIds = @{}
foreach ($meta in $runtime | Where-Object Extension -eq '.meta') {
 $match = [regex]::Match((Get-Content -LiteralPath $meta.FullName -Raw), 'Name\s+"\{([A-Fa-f0-9]{16})\}([^"\r\n]+)"')
 if (!$match.Success) { throw "Missing resource GUID: $($meta.Name)" }
 $resourceFull = [IO.Path]::GetFullPath($meta.FullName.Substring(0, $meta.FullName.Length - 5))
 $addonFull = [IO.Path]::GetFullPath($addon).TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar
 if (!$resourceFull.StartsWith($addonFull, [StringComparison]::OrdinalIgnoreCase)) { throw "Metadata outside the addon: $($meta.FullName)" }
 $resourcePath = $resourceFull.Substring($addonFull.Length).Replace('\','/')
 if ($match.Groups[2].Value -cne $resourcePath) { throw "Resource metadata path/case mismatch: $resourcePath" }
 $guid = $match.Groups[1].Value.ToUpperInvariant()
 if ($metadataIds.ContainsKey($guid)) { throw "Duplicate metadata GUID: $guid" }
 $metadataIds[$guid] = $meta.Name
}
}
} finally { foreach ($temporary in $assembledRoots) { Remove-Item -LiteralPath $temporary -Recurse -Force -ErrorAction SilentlyContinue } }
$version = (Get-Content -LiteralPath (Join-Path $repo 'VERSION') -Raw).Trim()
if ($version -notmatch '^\d+\.\d+\.\d+$') { throw 'VERSION must contain a three-part release version.' }
$files = @(& git -C $repo ls-files --cached --others --exclude-standard | Sort-Object -Unique)
if ($LASTEXITCODE -ne 0) { throw 'Unable to inspect Git file inventory.' }
foreach ($relative in $files) {
 if ($relative -match '(^|/)(\.local|evidence|build|artifacts|logs|profile|profile-data)/|\.(pak|zip|log|mdmp|dmp|private\.json)$|(^|/)(ServerData\.json|resourceDatabase\.rdb)$') { throw "Forbidden repository content: $relative" }
 $path = Join-Path $repo $relative
 if (!(Test-Path -LiteralPath $path)) { continue } # Tracked deletions are absent from the working tree.
 if ($relative.EndsWith('.ps1')) {
  $parseErrors = $null; $tokens = $null
  $null = [Management.Automation.Language.Parser]::ParseFile($path, [ref]$tokens, [ref]$parseErrors)
  if ($parseErrors.Count) { throw ($parseErrors | Out-String) }
 }
 if ($relative.EndsWith('.json')) { $null = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json }
 if ($relative.EndsWith('.md')) {
  foreach ($link in [regex]::Matches((Get-Content -LiteralPath $path -Raw), '\]\(([^)]+)\)')) {
   $target = $link.Groups[1].Value.Trim('<','>')
   if ($target -match '^[a-z]+:|^#') { continue }
   $target = [Uri]::UnescapeDataString(($target -split '#')[0])
   if ($target -and !(Test-Path -LiteralPath (Join-Path (Split-Path -Parent $path) $target))) { throw "Broken local link in ${relative}: $target" }
  }
 }
}
Write-Output "PASS: $runtimeCount addon files; identities, dependencies, metadata, PowerShell syntax, JSON, documentation links and repository content. Version $version."
Write-Output 'This is structural validation; it does not compile Enforce Script or certify gameplay.'

#requires -Version 7.0
param([Parameter(Mandatory)][string]$Source, [Parameter(Mandatory)][string]$AddonsDirectory, [string]$AddonName = '')
$ErrorActionPreference = 'Stop'
$settings = & "$PSScriptRoot/Get-ProjectConfig.ps1"
if (!$AddonName) { $AddonName = $settings.addon.name }
$entries = @($settings.addon) + @($settings.companion | Where-Object { $_ })
$entry = $entries | Where-Object name -CEQ $AddonName
if (!$entry) { throw 'Select an addon declared in tools/project.json.' }
$sourcePath = (Resolve-Path -LiteralPath $Source).Path.TrimEnd('\','/')
$addonsPath = [IO.Path]::GetFullPath($AddonsDirectory).TrimEnd('\','/')
if (!$addonsPath -or $addonsPath -match '^[A-Za-z]:$') { throw 'Choose an addons directory, not a drive root.' }
$target = Join-Path $addonsPath $AddonName
$guid = $entry.id
foreach ($protected in @($sourcePath) + @($entries.sourcePath)) {
 $separator = [IO.Path]::DirectorySeparatorChar
 if ($target -eq $protected -or $target.StartsWith($protected + $separator, [StringComparison]::OrdinalIgnoreCase) -or $protected.StartsWith($target + $separator, [StringComparison]::OrdinalIgnoreCase)) { throw 'Deployment must not overlap source.' }
}
# Backups and staging stay outside the scanned addons directory (no duplicate GUID).
$backupRoot = Join-Path (Split-Path -Parent $addonsPath) ('.' + $AddonName + '-local-backups')
foreach ($path in @($sourcePath,$target,$backupRoot)) {
 for ($ancestor = $path; $ancestor; $ancestor = Split-Path -Parent $ancestor) {
  if ((Test-Path -LiteralPath $ancestor) -and ((Get-Item -LiteralPath $ancestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw "Refusing linked path: $ancestor" }
 }
}
$project = Join-Path $sourcePath ($AddonName + '.gproj')
if (!(Test-Path -LiteralPath "$sourcePath/resourceDatabase.rdb") -or !(Test-Path -LiteralPath $project) -or (Get-Content -LiteralPath $project -Raw) -cnotmatch ('\bGUID\s+"?' + $guid + '\b')) { throw 'Expected an indexed build with the configured project GUID.' }
if (Test-Path -LiteralPath $target) {
 $previousProject = Join-Path $target ($AddonName + '.gproj')
 if (!(Test-Path -LiteralPath $previousProject) -or (Get-Content -LiteralPath $previousProject -Raw) -cnotmatch ('\bGUID\s+"?' + $guid + '\b')) { throw 'Existing destination is not this addon; it was left untouched.' }
}
$stamp = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff') + '-' + [guid]::NewGuid().ToString('N').Substring(0,8)
$incoming = Join-Path $backupRoot ($stamp + '.incoming')
$backup = Join-Path $backupRoot $stamp
New-Item -ItemType Directory -Path $incoming -Force | Out-Null
$count = 0
$runtimePattern = '^(' + [regex]::Escape($entry.project) + '|resourceDatabase\.rdb|(?:' + (@($entry.runtimePaths | ForEach-Object { [regex]::Escape($_) }) -join '|') + ')/.+)$'
foreach ($file in Get-ChildItem -LiteralPath $sourcePath -Recurse -File) {
 $relative = $file.FullName.Substring($sourcePath.Length+1).Replace('\','/')
 if ($relative -cnotmatch $runtimePattern -or $file.Extension -notin '.gproj','.rdb','.acp','.c','.conf','.edds','.emat','.et','.layout','.md','.meta','.png','.sig','.st','.txt','.wav','.xob' -or $relative -match '(?i)(^|/)(Docs|tests?|tools|logs?|profile|evidence)(/|$)') { continue }
 if ($file.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Linked source file is not deployable.' }
 $copy = Join-Path $incoming $relative
 New-Item -ItemType Directory -Path (Split-Path -Parent $copy) -Force | Out-Null
 Copy-Item -LiteralPath $file.FullName -Destination $copy
 if ((Get-FileHash -LiteralPath $copy).Hash -ne (Get-FileHash -LiteralPath $file.FullName).Hash) { throw "Copy verification failed: $relative" }
 $count++
}
if (Get-ChildItem -LiteralPath $sourcePath -Recurse -Directory | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }) { throw 'Linked source directories are not deployable.' }
New-Item -ItemType Directory -Path $addonsPath -Force | Out-Null
$saved = $null
try {
 # All move paths are absolute, checked above and confined to this named addon/backup root.
 if (Test-Path -LiteralPath $target) { Move-Item -LiteralPath $target -Destination $backup; $saved=$backup }
 Move-Item -LiteralPath $incoming -Destination $target
} catch {
 if ($saved -and !(Test-Path -LiteralPath $target)) { Move-Item -LiteralPath $saved -Destination $target }
 throw
}
[pscustomobject]@{ installedAddon=$target; backup=$saved; files=$count; source=$sourcePath }

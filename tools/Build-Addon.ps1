#requires -Version 7.0
param([Parameter(Mandatory)][ValidatePattern('^[A-Za-z0-9_-]+$')][string]$Name, [switch]$Wait, [ValidateRange(10,3600)][int]$TimeoutSeconds = 600, [Alias('CDFCompatibility')][switch]$Companion)
$ErrorActionPreference = 'Stop'
$localConfig = & (Join-Path $PSScriptRoot 'Get-LocalConfig.ps1')
$settings = & "$PSScriptRoot/Get-ProjectConfig.ps1"
$entry = $settings.addon
if ($Companion) {
 if (!$settings.companion) { throw 'No companion is configured in tools/project.json.' }
 $entry = $settings.companion
}
& (Join-Path $PSScriptRoot 'Test-Repository.ps1') | Write-Host
$candidate = Split-Path -Parent $PSScriptRoot
$build = Join-Path $candidate ('build/' + $Name)
if (Test-Path -LiteralPath $build) { throw 'Use a new build name; prior evidence is immutable.' }
if (Get-Process -Name ArmaReforgerSteamDiag,ArmaReforgerServerDiag,ArmaReforgerWorkbenchSteamDiag -ErrorAction SilentlyContinue) { throw 'Finish the active engine test before building.' }
$source = $entry.sourcePath
$addonName = $entry.name
New-Item -ItemType Directory -Path $build | Out-Null
$frozen = Join-Path $build $addonName
if ($entry.assembled -and !$Companion) { & (Join-Path $PSScriptRoot 'Assemble-Pack.ps1') -Destination $frozen | Out-Null }
else { Copy-Item -LiteralPath $source -Destination $frozen -Recurse }
$output = Join-Path $build 'PC'
$profile = Join-Path $build 'profile-data'
New-Item -ItemType Directory -Path $output,$profile | Out-Null
$manifest = @(Get-ChildItem -LiteralPath $frozen -Recurse -File | Sort-Object FullName | ForEach-Object {
 [pscustomobject]@{path=$_.FullName.Substring($frozen.Length+1); bytes=$_.Length; sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash}
})
$manifest | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $build 'source-manifest.json') -Encoding utf8
$project = Join-Path $frozen ($addonName + '.gproj')
$engine = (Join-Path $localConfig.WorkbenchRoot 'ArmaReforgerWorkbenchSteamDiag.exe')
$arguments = '-disableCrashReporter -wbModule=ResourceManager -builddata PC "' + $output + '" ' + $addonName + ' -gproj "' + $project + '" -profile "' + $profile + '"'
$dependencyRecord = $null
if ($Companion -or $entry.installedDependencies) {
 $dependencies = Join-Path $build 'dependencies'
 & "$PSScriptRoot/Copy-AddonDependencies.ps1" -Destination $dependencies -InstalledAddonsRoot $localConfig.InstalledAddonsRoot -SearchRoots ($localConfig.DependencyAddonsRoots -split ';') -Companion:$Companion | Out-Null
 $dependencyRecord = Get-Content -LiteralPath ($dependencies + '.json') -Raw | ConvertFrom-Json
 $arguments += ' -addonsDir "' + $dependencies + '"'
}
if (!(Test-Path -LiteralPath $engine -PathType Leaf)) { throw 'Configure WorkbenchRoot in .local/config.json.' }
$process = Start-Process -FilePath $engine -ArgumentList $arguments -WorkingDirectory $localConfig.GameRoot -WindowStyle Hidden -PassThru
$null = $process.Handle
$receipt = [ordered]@{name=$Name; pid=$process.Id; started=(Get-Date).ToString('o'); source=$project; output=$output; profile=$profile; publish=$false; gitCommit=(git -C $candidate rev-parse HEAD); sourceManifest='source-manifest.json'; executableSHA256=(Get-FileHash -LiteralPath $engine).Hash; dependencies=$dependencyRecord}
$receipt | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $build 'receipt.json') -Encoding utf8
if ($Wait) {
 $clock = [Diagnostics.Stopwatch]::StartNew()
 while (!$process.WaitForExit(1000)) {
  if ($clock.Elapsed.TotalSeconds -gt $TimeoutSeconds) { throw "Build timed out (PID $($process.Id)); inspect the engine/dialog. Nothing was deployed." }
 }
 $receipt.nativeExitCode = $process.ExitCode
 $receipt.completed = (Get-Date).ToString('o')
 $logs = @(Get-ChildItem -LiteralPath (Join-Path $profile 'logs') -Recurse -Filter console.log -File)
 $logText = ($logs | Get-Content -Raw) -join "`n"
 $receipt.succeeded = $process.ExitCode -eq 0 -and $logText -match 'Build successful' -and $logText -match 'Game destroyed' -and $logText -notmatch 'Build failed|Can.t compile|Script compilation failed|Virtual Machine Exception|Assertion failed|ENGINE\s+\(F\): Crashed'
 $receipt | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $build 'receipt.json') -Encoding utf8
 if (!$receipt.succeeded) { throw "Native build failed; inspect $profile. Nothing was deployed." }
}
$receipt | ConvertTo-Json -Depth 5 -Compress

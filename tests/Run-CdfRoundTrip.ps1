#requires -Version 7.0
# Native CDF round trip of EXPBG GM Tools garrisons through this pack's bridge, in one
# diagnostic server process (no GM UI, no multiplayer, no cold restart). The fixture
# builds garrisons, captures a real CDF 1.4.1 document, writes and reads it as a file,
# refuses an append load and an unreadable ledger, restores with clearBeforeLoad and
# checks the exact garrisons. Launched only with the orchestrator's native-slot release.
#
# pwsh -File tests/Run-CdfRoundTrip.ps1 -GmToolsSnapshot <built EXPBG_GM_Tools> -SourceSnapshot <built EXPBG_CDF_Compat> -FixturePath tests/EXPG_CDFGarrisonRoundTrip.c -ExpectResult '<regex>' -TimeoutSeconds 600 -OrchestratorSlotGranted
param(
 [Parameter(Mandatory)][string]$GmToolsSnapshot,
 [Parameter(Mandatory)][string]$SourceSnapshot,
 [Parameter(Mandatory)][string]$FixturePath,
 [Parameter(Mandatory)][string]$ExpectResult,
 [ValidateRange(300,600)][int]$TimeoutSeconds = 600,
 [switch]$OrchestratorSlotGranted
)
$ErrorActionPreference = 'Stop'
function Test-RoundTripEvidence([string]$Text, [string]$Pattern) {
 # One distinct passing result line, clean shutdown, no script error before it. The
 # base game's resupply stations log one stock error during GM_Eden teardown.
 $lines = @([regex]::Matches($Text, $Pattern) | ForEach-Object { $_.Value.TrimEnd() } | Sort-Object -Unique)
 if ($lines.Count -ne 1) { return $false }
 $result = [regex]::Match($Text, $Pattern).Index
 $checked = $Text.Substring(0, $result) + ($Text.Substring($result) -replace "(?m)^.*SCRIPT\s+\(E\): 'SCR_BaseResupplySupportStationComponent' needs a entity catalog manager!\r?$", '')
 return $Text -match 'Game destroyed' -and $checked -notmatch 'Can.t compile|SCRIPT\s+\(E\)|Virtual Machine Exception|Assertion failed|ENGINE\s+\(F\): Crashed'
}
if (!$OrchestratorSlotGranted) { throw 'Explicit orchestrator native-slot handoff required. This launches a diagnostic server.' }
$repo = Split-Path -Parent $PSScriptRoot
$config = & "$repo/tools/Get-LocalConfig.ps1"
$pack = Get-Content -LiteralPath "$repo/tools/pack.json" -Raw | ConvertFrom-Json
$gmId = $pack.dependencies.gmTools.id
$cdfId = $pack.dependencies.cdf.id
$selfId = [regex]::Match((Get-Content -LiteralPath "$repo/addon/$($pack.project)" -Raw), '\bGUID\s+"?([A-F0-9]{16})').Groups[1].Value
if (!$selfId) { throw 'This pack project has no GUID.' }
$nativeNames = @('ArmaReforgerWorkbenchSteamDiag','ArmaReforgerSteam','ArmaReforgerSteamDiag','ArmaReforgerServer','ArmaReforgerServerDiag')
function Assert-NativeSlot { if (Get-Process -Name $nativeNames -ErrorAction SilentlyContinue) { throw 'Native slot occupied; no existing process will be stopped.' } }
Assert-NativeSlot
function Assert-Snapshot([string]$Path, [string]$Id) {
 $full = (Resolve-Path -LiteralPath $Path).Path
 if (!(Test-Path -LiteralPath "$full/resourceDatabase.rdb" -PathType Leaf)) { throw "Use a built/indexed addon snapshot: $Path" }
 $projects = @(Get-ChildItem -LiteralPath $full -Filter '*.gproj' -File)
 if ($projects.Count -ne 1 -or (Get-Content -LiteralPath $projects[0].FullName -Raw) -cnotmatch ('\bGUID\s+"?' + $Id + '\b')) { throw "Snapshot $Path is not addon $Id." }
 return $full
}
$gmSource = Assert-Snapshot $GmToolsSnapshot $gmId
$source = Assert-Snapshot $SourceSnapshot $selfId
$FixturePath = (Resolve-Path -LiteralPath $FixturePath).Path
$cdfFolders = @(Get-ChildItem -LiteralPath $config.InstalledAddonsRoot -Directory | Where-Object {
 $projects = @(Get-ChildItem -LiteralPath $_.FullName -Filter '*.gproj' -File)
 $projects.Count -eq 1 -and (Get-Content -LiteralPath $projects[0].FullName -Raw) -cmatch ('\bGUID\s+"?' + $cdfId + '\b')
})
if ($cdfFolders.Count -ne 1) { throw "Install CDF Game Master Save ($cdfId) in InstalledAddonsRoot first." }
# The GM Tools snapshot's own dependencies (0.1.16+: EXPBG Audio Data 198987BE7BAC4C84) must load with it:
# found by project GUID in DependencyAddonsRoots (root order) and linked like CDF, never copied.
$gmProject = @(Get-ChildItem -LiteralPath $gmSource -Filter '*.gproj' -File)[0]
$gmNeeds = @([regex]::Matches([regex]::Match((Get-Content -LiteralPath $gmProject.FullName -Raw), '(?s)Dependencies\s*\{([^}]*)\}').Groups[1].Value, '[A-Fa-f0-9]{16}') | ForEach-Object { $_.Value.ToUpperInvariant() } | Where-Object { $_ -cne '58D0FB3206B6F859' -and $_ -cne $cdfId -and $_ -cne $selfId -and $_ -cne $gmId })
$dependencyLinks = [ordered]@{}
foreach ($id in $gmNeeds) {
 $found = $null
 foreach ($root in @($config.DependencyAddonsRoots -split ';' | Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Container) })) {
  $found = @(Get-ChildItem -LiteralPath $root -Directory | Sort-Object Name | Where-Object {
   $projects = @(Get-ChildItem -LiteralPath $_.FullName -Filter '*.gproj' -File)
   $projects.Count -eq 1 -and (Get-Content -LiteralPath $projects[0].FullName -Raw) -cmatch ('\bGUID\s+"?' + $id + '\b')
  } | Select-Object -First 1 | ForEach-Object FullName)[0]
  if ($found) { break }
 }
 if (!$found) { throw "EXPBG GM Tools needs addon $id (0.1.16+: EXPBG Audio Data); install it in one of DependencyAddonsRoots: $($config.DependencyAddonsRoots)" }
 $dependencyLinks[$id] = $found
}
$engine = Join-Path $config.ServerRoot 'ArmaReforgerServerDiag.exe'
if (!(Test-Path -LiteralPath $engine -PathType Leaf)) { throw 'Configure ServerRoot with the native diagnostic server installation.' }
$run = Join-Path $repo ('build/cdf-roundtrip-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff'))
$addons = Join-Path $run 'addons'
$logs = Join-Path $run 'logs'
New-Item -ItemType Directory -Path $addons, $logs | Out-Null
Copy-Item -LiteralPath $gmSource -Destination (Join-Path $addons $gmId) -Recurse
Copy-Item -LiteralPath $source -Destination (Join-Path $addons $selfId) -Recurse
$fixture = Join-Path $addons 'EXPG_CdfFixture'
New-Item -ItemType Directory -Path "$fixture/Scripts/Game", "$fixture/Worlds/CdfRoundTrip_Layers" | Out-Null
Copy-Item -LiteralPath $FixturePath -Destination "$fixture/Scripts/Game/EXPG_CDFGarrisonRoundTrip.c"
@"
GameProject {
 ID "EXPG_CdfFixture"
 GUID "5C9D2E7A1B3F4E60"
 TITLE "EXPBG CDF Compat garrison round-trip fixture"
 Dependencies {
  "58D0FB3206B6F859"
  "$gmId"
  "$cdfId"
  "$selfId"
 }
}
"@ | Set-Content -LiteralPath "$fixture/EXPG_CdfFixture.gproj" -Encoding utf8NoBOM
@'
SubScene {
 Parent "{BEF094A5F7F3211B}worlds/GameMaster/GM_Eden.ent"
}
'@ | Set-Content -LiteralPath "$fixture/Worlds/CdfRoundTrip.ent" -Encoding utf8NoBOM
@'
EXPG_CdfRoundTrip CdfRoundTripDriver {
 coords 4773 169 7094
}
'@ | Set-Content -LiteralPath "$fixture/Worlds/CdfRoundTrip_Layers/default.layer" -Encoding utf8NoBOM
'MetaFileClass { Name "{6E3A1C5B9D7F2048}Worlds/CdfRoundTrip.ent" Configurations { ENTResourceClass PC {} ENTResourceClass HEADLESS : PC {} } }' | Set-Content -LiteralPath "$fixture/Worlds/CdfRoundTrip.ent.meta" -Encoding utf8NoBOM
@(Get-ChildItem -LiteralPath $addons -Recurse -File | ForEach-Object {
 [ordered]@{path=$_.FullName.Substring($addons.Length + 1);sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash}
}) | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath "$run/inputs.json"
$link = Join-Path $addons $cdfId
$arguments = @('-disableCrashReporter','-addonsDir',$addons,'-addons','5C9D2E7A1B3F4E60','-profile',"$run/profile",'-logsDir',$logs,'-server','Worlds/CdfRoundTrip.ent','-worldSystemsConfig','{8DDC2A311929D52F}Configs/Systems/GameMasterSystems.conf','-maxFPS','60')
$start = [Diagnostics.ProcessStartInfo]::new($engine)
$start.UseShellExecute = $false
$start.CreateNoWindow = $true
$start.WorkingDirectory = $config.ServerRoot
$start.RedirectStandardOutput = $true
$start.RedirectStandardError = $true
foreach ($argument in $arguments) { $start.ArgumentList.Add($argument) }
$process = [Diagnostics.Process]::new()
$process.StartInfo = $start
Assert-NativeSlot
# Junctions only: removing them never touches the installed CDF (or GM Tools dependency) files.
function Remove-CdfLink {
 foreach ($path in @($link) + @($dependencyLinks.Keys | ForEach-Object { Join-Path $addons $_ })) {
  $item = Get-Item -LiteralPath $path -Force -ErrorAction SilentlyContinue
  if ($item -and $item.LinkType -eq 'Junction') { $item.Delete() }
 }
}
function Test-CdfLinkPresent { [bool]@(@($link) + @($dependencyLinks.Keys | ForEach-Object { Join-Path $addons $_ }) | Where-Object { Get-Item -LiteralPath $_ -Force -ErrorAction SilentlyContinue }).Count }
$started = $false
try {
 New-Item -ItemType Junction -Path $link -Target $cdfFolders[0].FullName | Out-Null
 foreach ($id in $dependencyLinks.Keys) { New-Item -ItemType Junction -Path (Join-Path $addons $id) -Target $dependencyLinks[$id] | Out-Null }
 $started = $process.Start()
} finally { if (!$started) { Remove-CdfLink } }
if (!$started) { throw 'Diagnostic server failed to start.' }
$startTime = $process.StartTime.ToUniversalTime()
$receipt = [ordered]@{gmTools=$gmSource;source=$source;cdf=$cdfFolders[0].FullName;gmToolsDependencies=$dependencyLinks;run=$run;pid=$process.Id;startedUtc=$startTime.ToString('o');executable=$engine;arguments=$arguments;timeoutSeconds=$TimeoutSeconds;timedOut=$false;ownedProcessStopped=$false;nativeExitCode=$null;passed=$false}
$receipt | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath "$run/run.json"
$stdout = $process.StandardOutput.ReadToEndAsync()
$stderr = $process.StandardError.ReadToEndAsync()
try {
 if (!$process.WaitForExit($TimeoutSeconds * 1000)) {
  $receipt.timedOut = $true
  # Only the process launched above may be stopped. Fail closed on identity drift.
  $current = Get-Process -Id $process.Id -ErrorAction Stop
  if ($current.StartTime.ToUniversalTime().Ticks -ne $startTime.Ticks -or [IO.Path]::GetFullPath($current.Path) -ine [IO.Path]::GetFullPath($engine)) { throw 'Timeout, but process ownership could not be verified; no process was stopped.' }
  $process.Kill($true)
  $receipt.ownedProcessStopped = $true
  if (!$process.WaitForExit(10000)) { throw 'Owned diagnostic process did not exit after timeout.' }
 }
 $receipt.nativeExitCode = $process.ExitCode
 $output = $stdout.GetAwaiter().GetResult() + "`n" + $stderr.GetAwaiter().GetResult()
 [IO.File]::WriteAllText("$run/native-output.log", $output)
 $text = $output + "`n" + ((Get-ChildItem -LiteralPath $run -Recurse -Filter '*.log' -File | Where-Object Name -ne 'native-output.log' | Get-Content -Raw) -join "`n")
 $receipt.passed = !$receipt.timedOut -and $process.ExitCode -eq 0 -and (Test-RoundTripEvidence $text $ExpectResult)
 $text -split "`n" | Where-Object { $_ -match '\[EXPG|SCRIPT\s+\(E\)|Can.t compile' } | Select-Object -Unique | Write-Output
} finally {
 if ($process.HasExited) { Remove-CdfLink }
 $receipt | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath "$run/result.json"
 # The addon copies (about 1 GB per run) are only inputs; once the engine is gone and every
 # junction is removed, only the logs, saves and receipts are kept.
 if ($process.HasExited -and !(Test-CdfLinkPresent) -and (Test-Path -LiteralPath $addons)) { Remove-Item -LiteralPath $addons -Recurse -Force -ErrorAction SilentlyContinue }
}
if (!$receipt.passed) { throw "CDF round-trip fixture not passed; inspect $run" }
"PASS: in-process CDF capture, file round trip, refusals and clearBeforeLoad restore of garrisons in one diagnostic server. No GM UI, cold restart or multiplayer. Evidence: $run"

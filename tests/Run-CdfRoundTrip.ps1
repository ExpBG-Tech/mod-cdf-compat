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
# Junction only: removing it never touches the installed CDF files.
New-Item -ItemType Junction -Path $link -Target $cdfFolders[0].FullName | Out-Null
function Remove-CdfLink {
 $item = Get-Item -LiteralPath $link -Force -ErrorAction SilentlyContinue
 if ($item -and $item.LinkType -eq 'Junction') { $item.Delete() }
}
$started = $false
try { $started = $process.Start() } finally { if (!$started) { Remove-CdfLink } }
if (!$started) { throw 'Diagnostic server failed to start.' }
$startTime = $process.StartTime.ToUniversalTime()
$receipt = [ordered]@{gmTools=$gmSource;source=$source;cdf=$cdfFolders[0].FullName;run=$run;pid=$process.Id;startedUtc=$startTime.ToString('o');executable=$engine;arguments=$arguments;timeoutSeconds=$TimeoutSeconds;timedOut=$false;ownedProcessStopped=$false;nativeExitCode=$null;passed=$false}
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
}
if (!$receipt.passed) { throw "CDF round-trip fixture not passed; inspect $run" }
"PASS: in-process CDF capture, file round trip, refusals and clearBeforeLoad restore of garrisons in one diagnostic server. No GM UI, cold restart or multiplayer. Evidence: $run"

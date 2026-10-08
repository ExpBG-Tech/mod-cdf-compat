#requires -Version 7.0
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/../tools/Workshop-Common.ps1"
function Rejects([scriptblock]$Action, [string]$Pattern) {
 $caught = $false
 try { & $Action | Out-Null } catch {
  if ($_.Exception.Message -notmatch $Pattern) { throw }
  $caught = $true
 }
 if (!$caught) { throw "Expected rejection: $Pattern" }
}
$root = Join-Path $PSScriptRoot ('../.local/local-release-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path "$root/addon", "$root/tools", "$root/artifacts/previous" -Force | Out-Null
git -C $root init -q -b main
git -C $root config user.name Fixture
git -C $root config user.email fixture@example.invalid
'0.1.19' | Set-Content "$root/VERSION"
'payload' | Set-Content "$root/addon/example.c"
'notes' | Set-Content "$root/CHANGELOG.md"
'license' | Set-Content "$root/LICENSE"
@{id='A123456789ABCDEF';name='Release Fixture';unlisted=$true;private=$false} | ConvertTo-Json | Set-Content "$root/tools/workshop-asset.json"
@{addon=@{name='Release_Fixture';source='addon';dependencies=@('58D0FB3206B6F859');runtimePaths=@('Scripts');preview='UI/preview.png'};legacyPublishWorkflows=@()} | ConvertTo-Json -Depth 5 | Set-Content "$root/tools/project.json"
'artifacts/' | Set-Content "$root/.gitignore"
git -C $root add .
git -C $root commit -qm initial
git -C $root update-ref refs/remotes/origin/main HEAD
$new = Get-LocalReleaseSource $root 'v0.1.19'
if (!$new.createTag) { throw 'Missing tag should be created only after preflight.' }
git -C $root tag v0.1.19
$original = git -C $root rev-parse HEAD
'workflow docs' | Set-Content "$root/README.md"
git -C $root add .
git -C $root commit -qm docs
git -C $root update-ref refs/remotes/origin/main HEAD
$existing = Get-LocalReleaseSource $root 'v0.1.19'
if ($existing.createTag -or $existing.commit -ne $original) { throw 'Existing release provenance lost.' }
Rejects { Get-LocalReleaseSource $root 'v0.1.18' } 'match VERSION'
'dirty' | Add-Content "$root/addon/example.c"
Rejects { Get-LocalReleaseSource $root 'v0.1.19' } 'clean'
git -C $root add .
git -C $root commit -qm runtime
Rejects { Get-LocalReleaseSource $root 'v0.1.19' } 'pushed main'
git -C $root update-ref refs/remotes/origin/main HEAD
Rejects { Get-LocalReleaseSource $root 'v0.1.19' } 'release inputs'
Assert-NoWorkshopUploadAttempt $root '0.1.19'
@{version='0.1.19';uploadAttempted=$false;uploaded=$false} | ConvertTo-Json | Set-Content "$root/artifacts/previous/workshop-receipt.json"
Assert-NoWorkshopUploadAttempt $root '0.1.19'
@{version='0.1.19';uploadAttempted=$true;uploaded=$false} | ConvertTo-Json | Set-Content "$root/artifacts/previous/workshop-receipt.json"
Rejects { Assert-NoWorkshopUploadAttempt $root '0.1.19' } 'already attempted'
Assert-NoWorkshopUploadAttempt $root '0.1.20'
$cancelled = Join-Path $root '.local/cancelled-run'
New-Item -ItemType Directory -Force -Path "$cancelled/publish-engine-logs" | Out-Null
Set-Content -LiteralPath "$cancelled/publish-engine-logs/console.log" -Value 'DEFAULT      : Validating bundle successful'
if (Test-WorkshopUploadStarted $cancelled) { throw 'A form closed before its upload confirmation must leave its version free.' }
Add-Content -LiteralPath "$cancelled/publish-engine-logs/console.log" -Value 'DEFAULT      : Publishing project to Workshop...'
if (!(Test-WorkshopUploadStarted $cancelled)) { throw 'A confirmed upload must count as an upload attempt.' }
if (!(Test-WorkshopUploadStarted (Join-Path $root '.local/no-such-run'))) { throw 'A run without publish logs must count as an upload attempt.' }
Assert-NoLegacyPublishHold $root
New-Item -ItemType Directory -Path "$root/.local" -Force | Out-Null
'{"runs":[{"id":123,"status":"in_progress"}]}' | Set-Content "$root/.local/legacy-publish-review.json"
Rejects { Assert-NoLegacyPublishHold $root } 'Legacy publishing needs review'
# Completion/cancellation does not establish whether Workbench uploaded already.
'{"runs":[{"id":123,"status":"completed"}]}' | Set-Content "$root/.local/legacy-publish-review.json"
Rejects { Assert-NoLegacyPublishHold $root } 'Legacy publishing needs review'
$savedEmail = $env:BOHEMIA_EMAIL
$savedPassword = $env:BOHEMIA_PASSWORD
try {
 $env:BOHEMIA_EMAIL = 'fixture@example.invalid'
 $env:BOHEMIA_PASSWORD = 'fixture ! $() ; literal password'
 $credential = Get-WorkshopCredential
 if ($credential -isnot [pscredential] -or $credential.UserName -ne $env:BOHEMIA_EMAIL -or $credential.GetNetworkCredential().Password -cne $env:BOHEMIA_PASSWORD) { throw 'Credential conversion altered literal data.' }
} finally {
 $env:BOHEMIA_EMAIL = $savedEmail
 $env:BOHEMIA_PASSWORD = $savedPassword
}
'PASS: local release rejects dirty/unpushed/mismatched inputs and prior upload attempts; existing immutable tag supported.'
# Expected git rejections above leave a native exit code of 1; assertions passed.
exit 0

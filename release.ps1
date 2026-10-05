#requires -Version 7.0
[CmdletBinding()]
param([switch]$Publish, [switch]$Interactive, [switch]$Companion)
$ErrorActionPreference = 'Stop'
if ($Interactive -and !$Publish) { throw 'Interactive requires -Publish and explicit publication authorization.' }
if (!$IsWindows) { throw 'Run this command on the licensed Windows Workbench PC.' }
if ($env:GITHUB_ACTIONS -eq 'true') { throw 'Workshop releases are invoked locally, not by Actions.' }
. "$PSScriptRoot/tools/Workshop-Common.ps1"
$repo = $PSScriptRoot
$project = & "$repo/tools/Get-ReleaseConfig.ps1" -Companion:$Companion
$version = (Get-Content -LiteralPath (Join-Path $repo $project.versionFile) -Raw).Trim()
$tag = "$($project.tagPrefix)$version"
Assert-WorkshopVersion $tag $version $project.tagPrefix
# Both targets build/install core and share last-build.json; serialize by core identity.
$coreId = (& "$repo/tools/Get-ProjectConfig.ps1").addon.id
$mutex = [Threading.Mutex]::new($false, ('Local\Reforger-Workshop-' + $coreId))
$locked = $false
try {
    try { $locked = $mutex.WaitOne(0) } catch [Threading.AbandonedMutexException] { $locked = $true }
    if (!$locked) { throw 'Another local release is running.' }
    if ($Publish) {
        Assert-NoLegacyPublishHold $repo
        $gh = (Get-Command gh -CommandType Application -ErrorAction SilentlyContinue).Source
        if (!$gh -and $env:ProgramFiles) { $gh = Join-Path $env:ProgramFiles 'GitHub CLI/gh.exe' }
        if (!$gh -or !(Test-Path -LiteralPath $gh)) { throw 'Install GitHub CLI, then run gh auth login --hostname github.com --git-protocol ssh --web.' }
        function GitHub([string[]]$CliArguments) {
            $output = & $gh @CliArguments
            if ($LASTEXITCODE -ne 0) { throw "GitHub CLI failed ($($CliArguments[0])). No automatic publish retry." }
            return $output
        }
        GitHub @('auth','status','--hostname','github.com') | Out-Host
        $credential = Get-WorkshopCredential
        Invoke-ReleaseGit $repo @('fetch','origin','main','--tags') | Out-Null
        $source = Get-LocalReleaseSource $repo $tag -Companion:$Companion
        Assert-NoWorkshopUploadAttempt $repo $version $project.addon.id
        Push-Location $repo
        try { $repository = GitHub @('repo','view','--json','nameWithOwner','--jq','.nameWithOwner') }
        finally { Pop-Location }
        # Old tags contain the former event workflow. Disable it server-side too.
        # The list endpoint omits deleted workflows even when old tags can trigger them.
        foreach ($filename in $project.legacyPublishWorkflows) {
            $workflowOutput = & $gh api "repos/$repository/actions/workflows/$filename" 2>&1
            if ($LASTEXITCODE -ne 0) {
                if (($workflowOutput | Out-String) -match 'HTTP 404') {
                    $ref = if ($source.createTag) { 'HEAD' } else { $tag }
                    if (Invoke-ReleaseGit $repo @('ls-tree','--name-only',$ref,'--',".github/workflows/$filename")) { throw 'The release tag still contains an unavailable legacy workflow. Prepare a new version; never move the old tag.' }
                    continue
                }
                throw 'Unable to inspect a legacy workflow. Resolve GitHub access before publishing.'
            }
            $workflow = ($workflowOutput | Out-String) | ConvertFrom-Json
            if ($workflow.state -eq 'deleted') {
                $ref = if ($source.createTag) { 'HEAD' } else { $tag }
                if (Invoke-ReleaseGit $repo @('ls-tree','--name-only',$ref,'--',$workflow.path)) {
                    throw 'The release tag contains a deleted legacy workflow that GitHub can reactivate. Prepare a new version from current main; never move the old tag.'
                }
            }
            if ($workflow.state -eq 'active') { GitHub @('workflow','disable',[string]$workflow.id,'--repo',$repository) | Out-Null }
            $runs = (GitHub @('api',"repos/$repository/actions/workflows/$($workflow.id)/runs?per_page=100") | ConvertFrom-Json).workflow_runs
            $active = @($runs | Where-Object status -ne 'completed')
            if ($active.Count) {
                # Persist before cancellation: the old job may already have uploaded.
                New-Item -ItemType Directory -Path "$repo/.local" -Force | Out-Null
                @{repository=$repository;workflow=$workflow.path;runs=@($active | Select-Object id,html_url,status,head_sha)} |
                    ConvertTo-Json -Depth 5 | Set-Content -LiteralPath "$repo/.local/legacy-publish-review.json"
                foreach ($run in $active) { GitHub @('run','cancel',[string]$run.id,'--repo',$repository) | Out-Null }
                Assert-NoLegacyPublishHold $repo
            }
        }
        $releases = @(GitHub @('api',"repos/$repository/releases",'--paginate','--slurp') | ConvertFrom-Json | ForEach-Object { $_ } | Where-Object tag_name -eq $tag)
        if ($releases.Count -gt 1 -or ($releases.Count -eq 1 -and !$releases[0].draft)) { throw 'This GitHub release is already published. Inspect its Workshop status; do not upload twice.' }
    }
    & "$repo/tests/Test-WorkshopRelease.ps1" | Out-Host
    & "$repo/tests/Test-LocalRelease.ps1" | Out-Host
    if ($Interactive) { & "$repo/tests/Test-InteractiveRelease.ps1" | Out-Host }
    & "$repo/build.ps1" -NonInteractive -WithCompanion:$Companion | Out-Host
    $runName = 'workshop-local-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff')
    if ($Publish) {
        if ($source.createTag) { Invoke-ReleaseGit $repo @('tag','-a',$tag,'-m',"$($project.asset.name) $version") | Out-Null }
        Invoke-ReleaseGit $repo @('push','origin',"refs/tags/$tag") | Out-Null
        $notes = [regex]::Match((Get-Content (Join-Path $repo $project.changelogFile) -Raw), '(?ms)^## ' + [regex]::Escape($version) + '\b[^\r\n]*\r?\n(.*?)(?=^## |\z)')
        if (!$notes.Success) { throw 'Release changelog entry missing.' }
        $notesFile = Join-Path $repo ".local/$runName-notes.md"
        [IO.File]::WriteAllText($notesFile, $notes.Groups[1].Value.Trim())
        if (!$releases.Count) { GitHub @('release','create',$tag,'--repo',$repository,'--verify-tag','--draft','--title',"$($project.asset.name) $version",'--notes-file',$notesFile) | Out-Host }
    }
    & "$repo/tools/Invoke-WorkshopRelease.ps1" -Tag $tag -RunName $runName -Publish:$Publish -Interactive:$Interactive -Companion:$Companion -Credential $credential
    $receiptPath = Join-Path $repo "artifacts/$runName/workshop-receipt.json"
    if ($Publish) {
        $receipt = Get-Content -LiteralPath $receiptPath -Raw | ConvertFrom-Json
        if (!$receipt.uploaded -or !$receipt.packageVerified) { throw 'Native receipt did not confirm uploading and final package integrity.' }
        $zip = Join-Path $repo "artifacts/$runName/$($project.addon.name)_${version}_source.zip"
        GitHub @('release','upload',$tag,$zip,$receiptPath,'--repo',$repository) | Out-Host
        GitHub @('release','edit',$tag,'--repo',$repository,'--draft=false') | Out-Host
        "GitHub release: https://github.com/$repository/releases/tag/$tag" | Write-Host
    }
    "Receipt: $receiptPath" | Write-Host
} finally {
    if ($locked) { $mutex.ReleaseMutex() }
    $mutex.Dispose()
}

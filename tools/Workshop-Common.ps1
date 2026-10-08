#requires -Version 7.0
function Invoke-ReleaseGit([string]$Repo, [string[]]$GitArguments) {
    $output = & git -C $Repo @GitArguments
    if ($LASTEXITCODE -ne 0) { throw "Git release check failed: $($GitArguments[0])." }
    return $output
}

function Get-LocalReleaseSource([string]$Repo, [string]$Tag, [switch]$Companion) {
    $project = & "$PSScriptRoot/Get-ReleaseConfig.ps1" -Repo $Repo -Companion:$Companion
    $version = (Get-Content -LiteralPath (Join-Path $Repo $project.versionFile) -Raw).Trim()
    Assert-WorkshopVersion $Tag $version $project.tagPrefix
    if (Invoke-ReleaseGit $Repo @('status','--porcelain')) { throw 'Publish from a clean checkout, including untracked files.' }
    $head = Invoke-ReleaseGit $Repo @('rev-parse','HEAD')
    $branch = Invoke-ReleaseGit $Repo @('branch','--show-current')
    $main = Invoke-ReleaseGit $Repo @('rev-parse','refs/remotes/origin/main')
    if ($branch -ne 'main' -or $head -ne $main) { throw 'Publish from fully pushed main after fetching origin.' }
    $commit = & git -C $Repo rev-parse --verify --quiet "refs/tags/$Tag^{commit}"
    $createTag = $LASTEXITCODE -ne 0
    if ($createTag) { $commit = $head }
    Invoke-ReleaseGit $Repo @('merge-base','--is-ancestor',$commit,$main) | Out-Null
    # Tooling/docs can advance after tagging; the actual shipped inputs may not.
    $inputs = @($project.addon.source, $project.versionFile, $project.changelogFile, 'LICENSE', $project.assetFile, 'tools/project.json')
    if ($Companion) { $inputs += @((& "$PSScriptRoot/Get-ProjectConfig.ps1" -Repo $Repo).addon.source, 'VERSION') }
    & git -C $Repo diff --quiet $commit $head -- @inputs
    if ($LASTEXITCODE -ne 0) { throw 'Existing tag release inputs differ from this checkout; never move a release tag.' }
    [pscustomobject]@{version=$version;tag=$Tag;commit=$commit;toolCommit=$head;createTag=$createTag}
}

function Assert-NoWorkshopUploadAttempt([string]$Repo, [string]$Version, [string]$Item = '') {
    foreach ($path in Get-ChildItem -Path (Join-Path $Repo 'artifacts/*/workshop-receipt.json') -File -ErrorAction SilentlyContinue) {
        $previous = Get-Content -LiteralPath $path.FullName -Raw | ConvertFrom-Json
        if ($previous.version -eq $Version -and (!$Item -or !$previous.item -or $previous.item -eq $Item) -and ($previous.uploadAttempted -or $previous.uploaded)) {
            throw "Workshop upload already attempted for $Version. Inspect the retained receipt/backend; never automatically retry."
        }
    }
}

# The publishing form logs "Publishing project to Workshop..." once its upload is confirmed. A run whose
# publish console log exists without that line or any upload progress closed the form before uploading,
# so its version stays free; missing logs count as an upload.
function Test-WorkshopUploadStarted([string]$RunDirectory) {
    $logs = @(Get-ChildItem -LiteralPath (Join-Path $RunDirectory 'publish-engine-logs') -Filter '*.log' -Recurse -File -ErrorAction SilentlyContinue)
    if (!($logs | Where-Object Name -eq 'console.log')) { return $true }
    $logs += @(Get-Item -LiteralPath (Join-Path $RunDirectory 'publish-output.log') -ErrorAction SilentlyContinue)
    $text = @($logs | ForEach-Object { Get-Content -LiteralPath $_.FullName -Raw }) -join "`n"
    return $text -match 'Publishing project to Workshop|Uploading status|UploadAssetFile|Publishing successful'
}

function Assert-NoLegacyPublishHold([string]$Repo) {
    $path = Join-Path $Repo '.local/legacy-publish-review.json'
    if (Test-Path -LiteralPath $path) {
        throw "Legacy publishing needs review. Inspect the runs recorded in $path and their Workshop status before resolving this hold; cancellation is not proof that no upload occurred."
    }
}

function Get-WorkshopCredential {
    if ($env:BOHEMIA_EMAIL -and $env:BOHEMIA_PASSWORD) {
        return [pscredential]::new($env:BOHEMIA_EMAIL, (ConvertTo-SecureString $env:BOHEMIA_PASSWORD -AsPlainText -Force))
    }
    if ($IsWindows) {
        $path = Join-Path $env:LOCALAPPDATA 'EXPBG/Workshop/credential.xml'
        if (Test-Path -LiteralPath $path) {
            $credential = Import-Clixml -LiteralPath $path
            if ($credential -is [pscredential] -and $credential.UserName -and $credential.Password.Length) { return $credential }
        }
    }
    throw 'Workshop credentials missing. Run ./tools/Set-WorkshopCredential.ps1 interactively, or supply BOHEMIA_EMAIL and BOHEMIA_PASSWORD in this process environment.'
}

function Assert-WorkshopVersion([string]$Tag, [string]$Version, [ValidateSet('v','cdf-v')][string]$Prefix = 'v') {
    if ($Version -cnotmatch '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$' -or $Tag -cne "$Prefix$Version") {
        throw 'Release tag must use the selected prefix and match VERSION exactly.'
    }
}

function Assert-WorkshopLog([string]$Phase, [int]$ExitCode, [string]$Log) {
    if ($ExitCode -ne 0 -or $Log -match 'Build failed|Can.t compile|Script compilation failed|SCRIPT\s+\(E\)|Virtual Machine Exception|Assertion failed|ENGINE\s+\(F\): Crashed|Addon processing failed|Failed to initialize Enfusion engine') {
        throw "$Phase failed; inspect the private native logs. No automatic upload retry."
    }
    if ($Phase -eq 'build' -and ($Log -notmatch 'Build successful' -or $Log -notmatch 'Game destroyed')) {
        throw 'Native build did not confirm successful completion and shutdown.'
    }
    if ($Phase -eq 'pack' -and ($Log -notmatch 'Packaging project successful' -or $Log -notmatch 'Game destroyed')) {
        throw 'Native packing did not confirm successful completion and shutdown.'
    }
    if ($Phase -eq 'publish' -and ($Log -notmatch 'Project uploaded successfully' -or $Log -notmatch 'Addon processing successful')) {
        throw 'Upload/processing is not confirmed. Check the Workshop before retrying.'
    }
}

function Remove-WorkshopSourceTableReferences([string]$ProjectPath) {
    # .st files are editor inputs, omitted by native packing. Keep their bindings
    # in tracked source; strip only the release staging copy after compilation.
    $source = [IO.File]::ReadAllText($ProjectPath)
    $runtime = [regex]::Replace($source, '(?m)^[\t ]*StringTableSource[\t ]+"[^"\r\n]*"[\t ]*(?:\r?\n|$)', '')
    if ($runtime -match '(?m)^\s*StringTableSource\b') { throw 'Unsupported editor string-table reference syntax in staged project.' }
    if ($runtime -cne $source) { [IO.File]::WriteAllText($ProjectPath, $runtime) }
}

function Write-WorkshopManifest([string]$Directory, [string]$AssetPath, [string]$Version, [string]$Notes, [string]$Preview, [string]$AddonName = (& "$PSScriptRoot/Get-ProjectConfig.ps1").addon.name) {
    Assert-WorkshopVersion "v$Version" $Version
    $asset = Get-Content -LiteralPath $AssetPath -Raw | ConvertFrom-Json -AsHashtable
    $asset.version = $Version
    $asset.description = $asset.description.Replace('{{VERSION}}', $Version)
    $asset.changelog = $Notes
    Copy-Item -LiteralPath $Preview -Destination (Join-Path $Directory 'previewImage.png')
    $types = [ordered]@{'data.pak'='application/enfusion-pak'; "$AddonName.gproj"='application/enfusion-gproj'; 'resourceDatabase.rdb'='application/enfusion-rdb'; 'previewImage.png'='image/png'}
    $files = @(foreach ($name in $types.Keys) {
        $path = Join-Path $Directory $name
        $file = Get-Item -LiteralPath $path -ErrorAction Stop
        if (!$file.Length) { throw "Cannot publish empty file $name." }
        [ordered]@{name=$name;size=$file.Length;contentType=$types[$name];sha256=(Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant()}
    })
    # packAddon produces native payloads only; upload metadata follows the existing item's schema.
    [ordered]@{version=1;files=$files;asset=$asset} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $Directory 'manifest.json') -Encoding utf8NoBOM
}

function Get-WorkshopListingDifferences($Expected, $Actual) {
    foreach ($field in 'name','summary','description','license','tags') {
        $before = $Expected.$field
        $after = $Actual.$field
        if ($field -eq 'tags') { $before = @($before | Sort-Object); $after = @($after | Sort-Object) }
        if ((ConvertTo-Json -InputObject $before -Compress) -cne (ConvertTo-Json -InputObject $after -Compress)) { $field }
    }
}

function Assert-WorkshopPackage([string]$Directory, [Alias('Uploaded')][switch]$Published, [string]$Version, $Project = (& "$PSScriptRoot/Get-ProjectConfig.ps1"), [hashtable]$PreparedHashes) {
    $projectFile = $Project.addon.project
    foreach ($name in @('manifest.json','data.pak','resourceDatabase.rdb',$projectFile,'previewImage.png')) {
        $path = Join-Path $Directory $name
        if (!(Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Item -LiteralPath $path).Length -eq 0) {
            throw "Packed addon is missing $name."
        }
    }
    $manifest = Get-Content -LiteralPath (Join-Path $Directory 'manifest.json') -Raw | ConvertFrom-Json
    if ($manifest.asset.id -cne $Project.addon.id -or $manifest.asset.unlisted -isnot [bool] -or $manifest.asset.unlisted -ne $Project.asset.unlisted -or $manifest.asset.private -isnot [bool] -or $manifest.asset.private -ne $Project.asset.private) {
        throw 'Package must match the configured Workshop item and visibility.'
    }
    if ($Version -and $manifest.asset.version -cne $Version) { throw 'Manifest addon version does not match the release version.' }
    $projectText = Get-Content -LiteralPath (Join-Path $Directory $projectFile) -Raw
    if ($projectText -cnotmatch ('\bGUID\s+"?' + $Project.addon.id + '\b')) {
        throw 'Packed project GUID does not match the existing Workshop item.'
    }
    if ($projectText -match '(?m)^\s*StringTableSource\b') { throw 'Packed project references an editor-only string table.' }
    $runtime = @('data.pak',$projectFile,'resourceDatabase.rdb')
    $names = @($manifest.files.name)
    if ($manifest.version -ne 1 -or $names.Count -ne @($names | Select-Object -Unique).Count) { throw 'Unexpected upload manifest file inventory.' }
    if ($Published) {
        # -publishAddon rewrites manifest.json with Workbench's own inventory: the three native runtime
        # payloads, with or without the preview (0.1.18 listed three files). Never anything else.
        if (@($names | Where-Object { $_ -cnotin $runtime -and $_ -cne 'previewImage.png' }).Count -or (Compare-Object $runtime @($names | Where-Object { $_ -cin $runtime }))) {
            throw 'Unexpected upload manifest file inventory.'
        }
    } elseif ($names.Count -ne 4 -or (Compare-Object ($runtime + 'previewImage.png') $names)) { throw 'Unexpected upload manifest file inventory.' }
    foreach ($file in $manifest.files) {
        $path = Join-Path $Directory $file.name
        if ((Get-Item -LiteralPath $path).Length -ne $file.size -or (Get-FileHash -LiteralPath $path).Hash -ne $file.sha256) { throw 'Packed data changed after manifest generation.' }
    }
    if ($Published -and $PreparedHashes) {
        foreach ($name in @('data.pak',$projectFile)) {
            if (!$PreparedHashes.ContainsKey($name) -or [string]$PreparedHashes[$name] -notmatch '^[0-9A-Fa-f]{64}$') { throw "No prepared hash recorded for $name." }
            if ((Get-FileHash -LiteralPath (Join-Path $Directory $name)).Hash -ne [string]$PreparedHashes[$name]) { throw "$name changed during the upload." }
        }
    }
}

function Invoke-PrivateProcess {
    param([string]$Executable, [string[]]$Arguments, [string]$Directory, [string]$LogPath, [int]$TimeoutSeconds = 900)
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = [Diagnostics.ProcessStartInfo]@{
        FileName=$Executable; WorkingDirectory=$Directory; UseShellExecute=$false
        RedirectStandardOutput=$true; RedirectStandardError=$true; CreateNoWindow=$true
    }
    foreach ($argument in $Arguments) { $process.StartInfo.ArgumentList.Add($argument) }
    # Commands and raw engine output can contain account data. Never echo them.
    try {
        $null = $process.Start()
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        $timedOut = !$process.WaitForExit($TimeoutSeconds * 1000)
        if ($timedOut) {
            $process.Kill($true); $process.WaitForExit()
        }
        $output = $stdout.GetAwaiter().GetResult() + "`n" + $stderr.GetAwaiter().GetResult()
        [IO.File]::WriteAllText($LogPath, $output)
        if ($timedOut) { throw 'Timeout; private output retained.' }
        return [pscustomobject]@{ ExitCode=$process.ExitCode; Text=$output }
    } catch {
        # Do not propagate platform exceptions that may include credential-bearing arguments.
        throw 'Native operation failed or timed out; inspect private local logs. Never blindly retry publication.'
    } finally { $process.Dispose() }
}

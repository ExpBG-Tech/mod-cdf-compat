[CmdletBinding()]
param([Parameter(Mandatory)][ValidatePattern('^[A-Za-z0-9_-]+$')][string]$Name, [switch]$Companion)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$project = & "$PSScriptRoot/Get-ReleaseConfig.ps1" -Companion:$Companion
$gitCommit = & git -C $repo rev-parse --verify HEAD 2>$null
if ($LASTEXITCODE -ne 0) { throw 'Stage from a Git checkout with a commit so the receipt has valid provenance.' }
& (Join-Path $PSScriptRoot 'Test-Repository.ps1') | Write-Host
$source = $project.addon.sourcePath
$destination = Join-Path $repo ('artifacts/' + $Name)
if (Test-Path -LiteralPath $destination) { throw 'Use a new stage name; never overwrite a prior artifact.' }
if ($project.addon.assembled -and !$Companion) {
 $source = Join-Path $destination ('Assembled/' + $project.addon.name)
 & (Join-Path $PSScriptRoot 'Assemble-Pack.ps1') -Destination $source | Out-Null
}
$stage = Join-Path $destination ('Stage/' + $project.addon.name)
New-Item -ItemType Directory -Path $stage -Force | Out-Null
$manifest = @(foreach ($file in Get-ChildItem -LiteralPath $source -Recurse -File | Where-Object Name -ne 'resourceDatabase.rdb' | Sort-Object FullName) {
 $relative = $file.FullName.Substring($source.Length+1)
 $target = Join-Path $stage $relative
 New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
 Copy-Item -LiteralPath $file.FullName -Destination $target
 [pscustomobject]@{ path=($project.addon.name + '/' + $relative.Replace('\','/')); bytes=$file.Length; sha256=(Get-FileHash -LiteralPath $target).Hash }
})
# License stays beside the addon, never in its resource tree.
Copy-Item -LiteralPath (Join-Path $repo 'LICENSE') -Destination (Join-Path $destination 'Stage/LICENSE')
$version = (Get-Content -LiteralPath (Join-Path $repo $project.versionFile) -Raw).Trim()
$zip = Join-Path $destination "$($project.addon.name)_${version}_source.zip"
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory((Split-Path -Parent $stage), $zip)
$archive = [IO.Compression.ZipFile]::OpenRead($zip)
try {
 $entries = @($archive.Entries | Where-Object Name -ne '')
 if ($entries.Count -ne $manifest.Count + 1) { throw 'ZIP file count mismatch.' }
 foreach ($row in $manifest) {
  $entry = @($entries | Where-Object { $_.FullName.Replace('\','/') -eq $row.path })
  if ($entry.Count -ne 1) { throw "ZIP entry missing: $($row.path)" }
  $stream = $entry[0].Open(); $hasher = [Security.Cryptography.SHA256]::Create()
  try { $hash = [BitConverter]::ToString($hasher.ComputeHash($stream)).Replace('-','') }
  finally { $stream.Dispose(); $hasher.Dispose() }
  if ($hash -ne $row.sha256) { throw "ZIP hash mismatch: $($row.path)" }
 }
} finally { $archive.Dispose() }
$manifest | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $destination 'source-manifest.json') -Encoding utf8
$receipt = [ordered]@{version=$version; createdUtc=[DateTime]::UtcNow.ToString('o'); gitCommit=$gitCommit; workingTreeDirty=[bool](& git -C $repo status --porcelain); runtimeFiles=$manifest.Count; zip=$zip; sha256=(Get-FileHash -LiteralPath $zip).Hash; stage=$stage; compiled=$false; published=$false}
$receipt | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $destination 'receipt.json') -Encoding utf8
$receipt | ConvertTo-Json

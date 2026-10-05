#requires -Version 7.0
$ErrorActionPreference = 'Stop'
& "$PSScriptRoot/../tools/Test-Repository.ps1" | Out-Host
# Test-*.ps1 are portable guards only: no native engines or live publication.
# Run-/Start- scripts are explicit native entry points and never discovered here.
foreach ($test in Get-ChildItem -LiteralPath $PSScriptRoot -Filter 'Test-*.ps1' -File | Where-Object Name -NE 'Test-Tools.ps1' | Sort-Object Name) {
 & (Join-Path $PSHOME $(if ($IsWindows) {'pwsh.exe'} else {'pwsh'})) -NoProfile -File $test.FullName | Out-Host
 if ($LASTEXITCODE -ne 0) { throw "$($test.Name) failed with exit $LASTEXITCODE." }
}
'PASS: portable source, install, release and test-runner guard checks. Native engines were not launched.'
# Expected rejection tests may leave a native nonzero exit code after their assertions pass.
exit 0

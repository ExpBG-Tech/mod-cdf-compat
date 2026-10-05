#requires -Version 7.0
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
if (!$IsWindows) { throw 'Credential storage requires Windows DPAPI.' }
$credential = Get-Credential -Message 'Bohemia account linked to the existing Workshop item'
if (!$credential -or !$credential.UserName -or !$credential.Password.Length) { throw 'A complete credential is required; nothing saved.' }
$directory = Join-Path $env:LOCALAPPDATA 'EXPBG/Workshop'
New-Item -ItemType Directory -Path $directory -Force | Out-Null
$credential | Export-Clixml -LiteralPath (Join-Path $directory 'credential.xml')
'Workshop credential saved with Windows DPAPI for this Windows user and PC. No password was written to the repository.'

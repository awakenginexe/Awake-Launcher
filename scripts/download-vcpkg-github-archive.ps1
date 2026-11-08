# SPDX-License-Identifier: GPL-3.0-only
param(
    [Parameter(Mandatory)][uri]$Url,
    [Parameter(Mandatory)][ValidatePattern('^[0-9a-fA-F]{128}$')][string]$Sha512,
    [Parameter(Mandatory)][string]$Destination
)
$ErrorActionPreference = 'Stop'
if ($Url.Scheme -ne 'https' -or $Url.Host -ne 'github.com' -or
    $Url.AbsolutePath -notmatch '^/(?<repo>[^/]+/[^/]+)/archive/(?<ref>.+)\.(?<format>tar\.gz|zip)$') { exit 1 }
$format = if ($Matches.format -eq 'tar.gz') { 'tar.gz' } else { 'zip' }
$mirror = "https://codeload.github.com/$($Matches.repo)/$format/$($Matches.ref)"
Invoke-WebRequest -Uri $mirror -OutFile $Destination -TimeoutSec 60 -MaximumRetryCount 1 -RetryIntervalSec 2
if ((Get-FileHash -LiteralPath $Destination -Algorithm SHA512).Hash -ne $Sha512) {
    throw 'GitHub archive does not match the pinned SHA-512.'
}
exit 0

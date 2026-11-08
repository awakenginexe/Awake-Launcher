# SPDX-License-Identifier: GPL-3.0-only
$ErrorActionPreference = 'Stop'
$directory = Join-Path ([IO.Path]::GetTempPath()) "awake-download-test-$([guid]::NewGuid())"
New-Item -ItemType Directory -Path $directory | Out-Null
try {
    $fixture = [Text.Encoding]::UTF8.GetBytes('pinned source archive fixture')
    $hash = [Convert]::ToHexString([Security.Cryptography.SHA512]::HashData($fixture))
    $requests = [Collections.Generic.List[string]]::new()
    function Invoke-WebRequest {
        param($Uri, $OutFile, $TimeoutSec, $MaximumRetryCount, $RetryIntervalSec)
        $requests.Add([string]$Uri)
        [IO.File]::WriteAllBytes($OutFile, $fixture)
    }
    $downloader = Join-Path $PSScriptRoot 'download-vcpkg-github-archive.ps1'
    foreach ($format in 'tar.gz','zip') {
        $destination = Join-Path $directory "fixture.$format"
        & $downloader "https://github.com/owner/repo/archive/refs/tags/v1.$format" $hash $destination
        if ($requests[-1] -ne "https://codeload.github.com/owner/repo/$format/refs/tags/v1" -or
            (Get-FileHash -LiteralPath $destination -Algorithm SHA512).Hash -ne $hash) { throw 'Archive URL or hash check failed.' }
    }
    foreach ($url in 'http://github.com/owner/repo/archive/v1.tar.gz','https://example.com/owner/repo/archive/v1.tar.gz','https://github.com/owner/repo/releases/download/v1/file.zip') {
        $requests.Clear()
        & $downloader $url $hash (Join-Path $directory 'unsupported')
        if ($LASTEXITCODE -ne 1 -or $requests.Count) { throw 'Unsupported URLs must fall back to vcpkg.' }
    }
    $rejected = $false
    try { & $downloader 'https://github.com/owner/repo/archive/v1.tar.gz' ('0' * 128) (Join-Path $directory 'bad-hash') }
    catch { $rejected = $_.Exception.Message -eq 'GitHub archive does not match the pinned SHA-512.' }
    if (!$rejected) { throw 'An incorrect archive hash was accepted.' }
    'Archive rewriting, unsupported URL fallback and hash rejection passed.'
} finally {
    Get-ChildItem -LiteralPath $directory -File | Remove-Item
    Remove-Item -LiteralPath $directory
}
exit 0

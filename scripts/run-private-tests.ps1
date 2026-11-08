# SPDX-License-Identifier: GPL-3.0-only
param([Parameter(Mandatory)][string]$BuildRoot, [switch]$ProbeOnly, [switch]$Worker)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/prepare-private-test-storage.ps1" -BuildRoot $BuildRoot
$pwsh = (Get-Command pwsh.exe).Source
if ($Worker) {
    try {
        $env:AWAKE_CI_CHILD_PROBE = Join-Path $env:TEMP "child-owner-$([guid]::NewGuid()).txt"
        $owner = & $pwsh -NoProfile -NonInteractive -Command '[IO.File]::WriteAllText($env:AWAKE_CI_CHILD_PROBE,"fixture"); (Get-Acl -LiteralPath $env:AWAKE_CI_CHILD_PROBE).GetOwner([Security.Principal.SecurityIdentifier]).Value'
        $sid = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value
        if ($LASTEXITCODE -or ($owner -join '').Trim() -ne $sid) { throw 'Child-process files must be owned by the test user.' }
        'Standard-user child-process ownership verified.' | Set-Content $env:AWAKE_CI_LOG -Encoding utf8
        if ($env:AWAKE_CI_PROBE_ONLY -ne '1') {
            & ctest --test-dir $BuildRoot -C Release --output-on-failure -R '^(Task|ResourceFolderModel|AwakeWebPolicy|AwakeWebBridge)$' *> $env:AWAKE_CI_LOG
            if ($LASTEXITCODE) { throw "Native tests failed: $LASTEXITCODE" }
        }
        @{passed=$true} | ConvertTo-Json | Set-Content $env:AWAKE_CI_RESULT -Encoding utf8
    } catch {
        @{passed=$false; error=$_.Exception.Message} | ConvertTo-Json | Set-Content $env:AWAKE_CI_RESULT -Encoding utf8
    }
    return
}
$name = "standard-user-$([guid]::NewGuid())"
$env:AWAKE_CI_RESULT = Join-Path $env:TEMP "$name.json"
$env:AWAKE_CI_LOG = Join-Path $env:TEMP "$name.log"
$env:AWAKE_CI_PROBE_ONLY = if ($ProbeOnly) { '1' } else { '0' }
# Use Windows' Basic User token so file ownership matches a normal desktop session, including child processes.
$command = '"{0}" -NoProfile -NonInteractive -File "{1}" -BuildRoot "{2}" -Worker' -f $pwsh,$PSCommandPath,([IO.Path]::GetFullPath($BuildRoot))
& "$env:WINDIR/System32/runas.exe" /trustlevel:0x20000 $command
if ($LASTEXITCODE) { throw "Basic User process could not start: $LASTEXITCODE" }
$deadline = [DateTime]::UtcNow.AddSeconds($(if ($ProbeOnly) { 20 } else { 180 }))
while (!(Test-Path -LiteralPath $env:AWAKE_CI_RESULT) -and [DateTime]::UtcNow -lt $deadline) { Start-Sleep -Milliseconds 200 }
if (!(Test-Path -LiteralPath $env:AWAKE_CI_RESULT)) { throw 'Basic User test process did not return a result.' }
$result = Get-Content -LiteralPath $env:AWAKE_CI_RESULT -Raw | ConvertFrom-Json
if (Test-Path -LiteralPath $env:AWAKE_CI_LOG) { Get-Content -LiteralPath $env:AWAKE_CI_LOG }
if (!$result.passed) { throw $result.error }

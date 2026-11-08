# SPDX-License-Identifier: GPL-3.0-only
param([Parameter(Mandatory)][string]$BuildRoot, [switch]$ProbeOnly, [switch]$Worker, [string]$Tests = '^(Task|ResourceFolderModel|AwakeWebPolicy|AwakeWebBridge)$')
$ErrorActionPreference = 'Stop'
$pwsh = (Get-Command pwsh.exe).Source
if ($Worker) {
    try {
        $sid = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value
        $profile = (Get-ItemProperty "HKLM:/SOFTWARE/Microsoft/Windows NT/CurrentVersion/ProfileList/$sid").ProfileImagePath
        $env:USERPROFILE = [Environment]::ExpandEnvironmentVariables($profile)
        $env:APPDATA = Join-Path $env:USERPROFILE 'AppData/Roaming'
        $env:LOCALAPPDATA = Join-Path $env:USERPROFILE 'AppData/Local'
        $env:USERNAME = [Security.Principal.WindowsIdentity]::GetCurrent().Name.Split('\')[-1]
        $local = [Environment]::GetFolderPath('LocalApplicationData')
        if ([IO.Path]::GetFullPath($local) -ne [IO.Path]::GetFullPath($env:LOCALAPPDATA)) { throw 'Test-user LocalAppData resolves to another profile.' }
        $localOwner = (Get-Acl -LiteralPath $local).GetOwner([Security.Principal.SecurityIdentifier]).Value
        if ($localOwner -ne $sid) { throw 'Test-user LocalAppData must be owned by the test user.' }
        "Verified test-user profile: $env:USERPROFILE"

        $env:TEMP = $env:TMP = (Resolve-Path (Join-Path $BuildRoot '.validation/native-temp')).Path
        $env:AWAKE_CI_CHILD_PROBE = Join-Path $env:TEMP "child-owner-$([guid]::NewGuid()).txt"
        $owner = & $pwsh -NoProfile -NonInteractive -Command '[IO.File]::WriteAllText($env:AWAKE_CI_CHILD_PROBE,"fixture"); (Get-Acl -LiteralPath $env:AWAKE_CI_CHILD_PROBE).GetOwner([Security.Principal.SecurityIdentifier]).Value'
        $sid = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value
        if ($LASTEXITCODE -or ($owner -join '').Trim() -ne $sid) { throw 'Child-process files must be owned by the test user.' }
        'Standard-user child-process ownership verified.' | Set-Content $env:AWAKE_CI_LOG -Encoding utf8
        if ($env:AWAKE_CI_PROBE_ONLY -ne '1') {
            & ctest --test-dir $BuildRoot -C Release --output-on-failure -R $env:AWAKE_CI_TESTS *> $env:AWAKE_CI_LOG
            if ($LASTEXITCODE) { throw "Native tests failed: $LASTEXITCODE" }
        }
        @{passed=$true} | ConvertTo-Json | Set-Content $env:AWAKE_CI_RESULT -Encoding utf8
    } catch {
        @{passed=$false; error="$($_.Exception.Message) at $($_.InvocationInfo.PositionMessage)"} | ConvertTo-Json | Set-Content $env:AWAKE_CI_RESULT -Encoding utf8
    }
    return
}
$state = Join-Path $env:RUNNER_TEMP 'AwakePrivateTests'
$credentialFile = Join-Path $state 'credential.xml'
if (!(Test-Path -LiteralPath $credentialFile)) {
    if (Get-LocalUser -Name AwakeBetaTests -ErrorAction SilentlyContinue) { throw 'Disposable test account already exists without its credential record.' }
    New-Item -ItemType Directory -Path $state -Force | Out-Null
    $acl = [Security.AccessControl.DirectorySecurity]::new()
    $runnerSid = [Security.Principal.WindowsIdentity]::GetCurrent().User
    $acl.SetOwner($runnerSid)
    $acl.SetAccessRuleProtection($true,$false)
    $acl.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new($runnerSid,'FullControl','ContainerInherit,ObjectInherit','None','Allow'))
    Set-Acl -LiteralPath $state -AclObject $acl
    $password = ConvertTo-SecureString ([guid]::NewGuid().ToString('N')+'aA1!') -AsPlainText -Force
    $user = New-LocalUser -Name AwakeBetaTests -Password $password
    Add-LocalGroupMember -Group (Get-LocalGroup -SID 'S-1-5-32-545') -Member $user.Name
    [pscredential]::new("$env:COMPUTERNAME\AwakeBetaTests",$password) | Export-Clixml -LiteralPath $credentialFile
}
$credential = Import-Clixml -LiteralPath $credentialFile
$testSid = (Get-LocalUser -Name AwakeBetaTests).SID
foreach ($directory in @($BuildRoot, (Split-Path $PSScriptRoot -Parent), $env:CMAKE_PREFIX_PATH) | Where-Object { $_ -and (Test-Path -LiteralPath $_) }) {
    $acl = Get-Acl -LiteralPath $directory
    $acl.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new($testSid,'ReadAndExecute','ContainerInherit,ObjectInherit','None','Allow'))
    Set-Acl -LiteralPath $directory -AclObject $acl
}
. "$PSScriptRoot/prepare-private-test-storage.ps1" -BuildRoot $BuildRoot -OwnerSid $testSid.Value
$name = "standard-user-$([guid]::NewGuid())"
$env:AWAKE_CI_RESULT = Join-Path $env:TEMP "$name.json"
$env:AWAKE_CI_LOG = Join-Path $env:TEMP "$name.log"
$env:AWAKE_CI_PROBE_ONLY = if ($ProbeOnly) { '1' } else { '0' }
$env:AWAKE_CI_TESTS = $Tests
# The disposable runner account has no Administrators membership; product ownership checks remain enabled.
$arguments = '-NoProfile -NonInteractive -File "{0}" -BuildRoot "{1}" -Worker' -f $PSCommandPath,([IO.Path]::GetFullPath($BuildRoot))
$process = Start-Process -FilePath $pwsh -ArgumentList $arguments -Credential $credential -LoadUserProfile -WorkingDirectory ([IO.Path]::GetFullPath($BuildRoot)) -PassThru -WindowStyle Hidden
$deadline = [DateTime]::UtcNow.AddSeconds($(if ($ProbeOnly) { 20 } else { 1500 }))
while (!(Test-Path -LiteralPath $env:AWAKE_CI_RESULT) -and [DateTime]::UtcNow -lt $deadline) { Start-Sleep -Milliseconds 200 }
if (!(Test-Path -LiteralPath $env:AWAKE_CI_RESULT)) { throw "Standard-user test process did not return a result (exited: $($process.HasExited))." }
$result = Get-Content -LiteralPath $env:AWAKE_CI_RESULT -Raw | ConvertFrom-Json
if (Test-Path -LiteralPath $env:AWAKE_CI_LOG) { Get-Content -LiteralPath $env:AWAKE_CI_LOG }
if (!$result.passed) { throw $result.error }

# SPDX-License-Identifier: GPL-3.0-only
param([Parameter(Mandatory)][string]$BuildRoot, [string]$OwnerSid = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value)
$ErrorActionPreference = 'Stop'
$sid = [Security.Principal.SecurityIdentifier]::new($OwnerSid)
foreach ($relative in 'tests','tests/.validation','.validation/native-temp') {
    $path = Join-Path $BuildRoot $relative
    New-Item -ItemType Directory -Path $path -Force | Out-Null
    $acl = [Security.AccessControl.DirectorySecurity]::new()
    $acl.SetOwner($sid)
    $acl.SetAccessRuleProtection($true, $false)
    $acl.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new($sid, 'FullControl', 'ContainerInherit,ObjectInherit', 'None', 'Allow'))
    foreach ($trusted in 'S-1-5-18','S-1-5-32-544') {
        $acl.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new([Security.Principal.SecurityIdentifier]::new($trusted), 'FullControl', 'ContainerInherit,ObjectInherit', 'None', 'Allow'))
    }
    Set-Acl -LiteralPath $path -AclObject $acl
}
$env:TEMP = $env:TMP = (Resolve-Path (Join-Path $BuildRoot '.validation/native-temp')).Path

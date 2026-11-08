# SPDX-License-Identifier: GPL-3.0-only
param([Parameter(Mandatory)][string]$BuildRoot)
$ErrorActionPreference = 'Stop'
$sid = [Security.Principal.WindowsIdentity]::GetCurrent().User
foreach ($relative in 'tests/.validation','.validation/native-temp') {
    $path = Join-Path $BuildRoot $relative
    New-Item -ItemType Directory -Path $path -Force | Out-Null
    $acl = [Security.AccessControl.DirectorySecurity]::new()
    $acl.SetOwner($sid)
    $acl.SetAccessRuleProtection($true, $false)
    $acl.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new($sid, 'FullControl', 'ContainerInherit,ObjectInherit', 'None', 'Allow'))
    Set-Acl -LiteralPath $path -AclObject $acl
}
$env:TEMP = $env:TMP = (Resolve-Path (Join-Path $BuildRoot '.validation/native-temp')).Path

# SPDX-License-Identifier: GPL-3.0-only
param([Parameter(Mandatory)][string]$BuildRoot)
$ErrorActionPreference = 'Stop'
# Elevated hosted runners default new objects to Administrators; fixtures need the actual user's owner SID.
Add-Type @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
public static class AwakeCiToken {
    [DllImport("advapi32.dll", SetLastError=true)] static extern bool OpenProcessToken(IntPtr process, uint access, out IntPtr token);
    [DllImport("advapi32.dll", SetLastError=true)] static extern bool GetTokenInformation(IntPtr token, int kind, IntPtr data, int size, out int needed);
    [DllImport("advapi32.dll", SetLastError=true)] static extern bool SetTokenInformation(IntPtr token, int kind, IntPtr data, int size);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
    public static void UseUserAsDefaultOwner() {
        IntPtr token, user=IntPtr.Zero, owner=IntPtr.Zero;
        if (!OpenProcessToken(new IntPtr(-1), 0x88, out token)) throw new Win32Exception();
        try {
            int size;
            GetTokenInformation(token, 1, IntPtr.Zero, 0, out size);
            user=Marshal.AllocHGlobal(size);
            if (!GetTokenInformation(token, 1, user, size, out size)) throw new Win32Exception();
            owner=Marshal.AllocHGlobal(IntPtr.Size);
            Marshal.WriteIntPtr(owner, Marshal.ReadIntPtr(user));
            if (!SetTokenInformation(token, 4, owner, IntPtr.Size)) throw new Win32Exception();
        } finally {
            if (owner!=IntPtr.Zero) Marshal.FreeHGlobal(owner);
            if (user!=IntPtr.Zero) Marshal.FreeHGlobal(user);
            CloseHandle(token);
        }
    }
}
'@
[AwakeCiToken]::UseUserAsDefaultOwner()
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
$probe = New-Item -ItemType Directory (Join-Path $env:TEMP 'owner-probe')
$owner = (Get-Acl -LiteralPath $probe.FullName).GetOwner([Security.Principal.SecurityIdentifier])
if ($owner.Value -ne $sid.Value) { throw 'New test directories are not owned by the runner user.' }
'Private test storage and default directory ownership verified.'

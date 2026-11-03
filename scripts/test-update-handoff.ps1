param([string]$Makensis = 'makensis.exe')
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$id = 'AwakeHandoff' + [guid]::NewGuid().ToString('N')
$root = Join-Path ([IO.Path]::GetTempPath()) $id
$target = Join-Path $root 'Install Folder'
$payload = Join-Path $root 'payload'
$marker = Join-Path $root 'result.txt'
$startShortcut = Join-Path ([Environment]::GetFolderPath('Programs')) "$id.lnk"
$desktopShortcut = Join-Path ([Environment]::GetFolderPath('DesktopDirectory')) "$id.lnk"
$saved = @{}
foreach ($name in @('AWAKE_RESTART_PROBE', 'QSG_RHI_PREFER_SOFTWARE_RENDERER', 'QTWEBENGINE_CHROMIUM_FLAGS', 'AWAKE_HANDOFF_SENTINEL')) { $saved[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
try {
    New-Item -ItemType Directory -Path $payload -Force | Out-Null
    & cl.exe /nologo /EHsc "$PSScriptRoot/update-handoff-probe.cpp" "/Fo$root\probe.obj" "/Fe$payload\awakelauncher.exe" /link /SUBSYSTEM:WINDOWS Shell32.lib
    if ($LASTEXITCODE) { throw 'Probe compilation failed.' }
    $source = Get-Content "$repo/program_info/awake_install.nsi" -Raw
    $source = $source.Replace('Software\AwakeLauncher', "Software\$id").Replace('Uninstall\AwakeLauncher', "Uninstall\$id").Replace('Awake Launcher.lnk', "$id.lnk")
    Copy-Item "$repo/program_info/awake_install_languages.nsh", "$repo/program_info/awakelauncher.ico" $root
    [IO.File]::WriteAllText("$root\installer.nsi", $source, [Text.UTF8Encoding]::new($false))
    'Delete "$INSTDIR\awakelauncher.exe"' | Set-Content "$root\delete.nsh"
    & $Makensis /WX /V2 /DAWAKE_VERSION=2.0.1 "/DAWAKE_PACKAGE=$payload" "/DAWAKE_OUTPUT=$root\handoff.exe" "/DAWAKE_ICON=$root\awakelauncher.ico" "/DAWAKE_DELETE_MANIFEST=$root\delete.nsh" "$root\installer.nsi"
    if ($LASTEXITCODE) { throw 'Installer compilation failed.' }
    $env:AWAKE_RESTART_PROBE = $marker
    $env:QSG_RHI_PREFER_SOFTWARE_RENDERER = '1'
    $env:QTWEBENGINE_CHROMIUM_FLAGS = '--disable-gpu --ignore-gpu-blocklist --disable-partial-raster'
    $env:AWAKE_HANDOFF_SENTINEL = 'preserved'
    $data = 'D:/My Launcher Data/我的实例'
    $run = Start-Process "$root\handoff.exe" -ArgumentList "/S /AWAKEUPDATE=99999999 /AWAKEDATA=`"$data`" /DESKTOP=0 /D=$target" -WindowStyle Hidden -PassThru -Wait
    if ($run.ExitCode -ne 0) { throw "Installer failed: $($run.ExitCode)" }
    $expected = "--dir`n$data`n<unset>`n<unset>`npreserved`n"
    $actual = ''
    for ($i = 0; $i -lt 100 -and $actual -cne $expected; $i++) {
        if (Test-Path $marker) {
            try { $actual = [IO.File]::ReadAllText($marker, [Text.Encoding]::Unicode) } catch [IO.IOException] { }
        }
        if ($actual -cne $expected) { Start-Sleep -Milliseconds 100 }
    }
    if ($actual -cne $expected) { throw "Unexpected restart arguments or environment: $actual" }
    Write-Output 'Update restart preserves Unicode arguments and unrelated environment while clearing legacy renderer variables.'
} finally {
    foreach ($name in $saved.Keys) { [Environment]::SetEnvironmentVariable($name, $saved[$name], 'Process') }
    Remove-Item "HKCU:/Software/$id", "HKCU:/Software/Microsoft/Windows/CurrentVersion/Uninstall/$id" -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $startShortcut, $desktopShortcut -Force -ErrorAction SilentlyContinue
    $resolvedRoot = [IO.Path]::GetFullPath($root)
    if ([IO.Path]::GetDirectoryName($resolvedRoot) -ine [IO.Path]::GetTempPath().TrimEnd('\') -or (Split-Path $resolvedRoot -Leaf) -ne $id) { throw 'Refusing cleanup outside the isolated handoff directory.' }
    Remove-Item -LiteralPath $resolvedRoot -Recurse -Force -ErrorAction SilentlyContinue
}

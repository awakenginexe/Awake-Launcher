param(
    [Parameter(Mandatory)][string]$Installer,
    [Parameter(Mandatory)][string]$PackageDir,
    [Parameter(Mandatory)][string]$TestDir
)
$ErrorActionPreference = 'Stop'
$setup = (Resolve-Path -LiteralPath $Installer).Path
$payload = (Resolve-Path -LiteralPath $PackageDir).Path
$target = [IO.Path]::GetFullPath($TestDir)
if (Test-Path -LiteralPath $target) { throw 'Setup smoke-test directory must be new.' }
if (Test-Path 'HKCU:/Software/AwakeLauncher') { throw 'Run the setup smoke test only on a clean Windows account.' }
$shortcut = Join-Path ([Environment]::GetFolderPath('Programs')) 'Awake Launcher.lnk'
if (Test-Path -LiteralPath $shortcut) { throw 'An Awake Launcher Start Menu shortcut already exists.' }
function Run-Setup {
    $process = Start-Process -FilePath $setup -ArgumentList "/S /D=$target" -PassThru -Wait -WindowStyle Hidden
    if ($process.ExitCode -ne 0) { throw "Setup failed with exit code $($process.ExitCode)" }
}
try {
    Run-Setup
    if (Test-Path -LiteralPath (Join-Path $target 'portable.txt')) { throw 'Setup installed the portable marker.' }
    foreach ($file in Get-ChildItem -LiteralPath $payload -Recurse -File) {
        $installed = Join-Path $target ([IO.Path]::GetRelativePath($payload, $file.FullName))
        if (!(Test-Path -LiteralPath $installed) -or (Get-FileHash -LiteralPath $installed).Hash -ne (Get-FileHash -LiteralPath $file.FullName).Hash) { throw "Installed file differs: $($file.Name)" }
    }
    $uninstall = (Get-ItemProperty 'HKCU:/Software/Microsoft/Windows/CurrentVersion/Uninstall/AwakeLauncher').UninstallString
    if ($uninstall -ne ('"' + (Join-Path $target 'uninstall.exe') + '"')) { throw 'The uninstall command is incorrectly quoted.' }
    $sentinel = Join-Path $target 'user-preservation.txt'
    'preserve this user file' | Set-Content -LiteralPath $sentinel
    Run-Setup
    if ((Get-Content -LiteralPath $sentinel -Raw).Trim() -ne 'preserve this user file') { throw 'Upgrade changed user data.' }
    $process = Start-Process -FilePath (Join-Path $target 'uninstall.exe') -ArgumentList "/S _?=$target" -PassThru -Wait -WindowStyle Hidden
    if ($process.ExitCode -ne 0) { throw "Uninstall failed with exit code $($process.ExitCode)" }
    foreach ($file in Get-ChildItem -LiteralPath $payload -Recurse -File) {
        if (Test-Path -LiteralPath (Join-Path $target ([IO.Path]::GetRelativePath($payload, $file.FullName)))) { throw "Uninstall left a shipped file: $($file.Name)" }
    }
    if (!(Test-Path -LiteralPath $sentinel)) { throw 'Uninstall removed user data.' }
    if (Test-Path 'HKCU:/Software/AwakeLauncher') { throw 'Uninstall left the registration key.' }
    if (Test-Path -LiteralPath $shortcut) { throw 'Uninstall left the Start Menu shortcut.' }
    Write-Output 'Setup installation, upgrade, quoted uninstall command, file hashes, and user-data preservation passed.'
} finally {
    if (Test-Path -LiteralPath (Join-Path $target 'uninstall.exe')) {
        Start-Process -FilePath (Join-Path $target 'uninstall.exe') -ArgumentList "/S _?=$target" -Wait -WindowStyle Hidden
    }
}

param(
    [Parameter(Mandatory)][string]$PackageDir,
    [Parameter(Mandatory)][string]$Version,
    [Parameter(Mandatory)][string]$OutputDir,
    [string]$MakeNsis = 'makensis'
)
$ErrorActionPreference = 'Stop'
if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw 'Version must be MAJOR.MINOR.PATCH.' }
$packageRoot = (Resolve-Path -LiteralPath $PackageDir).Path
if (Test-Path -LiteralPath (Join-Path $packageRoot 'portable.txt')) { throw 'Setup payload must not contain portable.txt.' }
foreach ($required in @('awakelauncher.exe','QtWebEngineProcess.exe','Qt6WebEngineCore.dll','resources/icudtl.dat','resources/qtwebengine_resources.pak','translations/qtwebengine_locales/en-US.pak')) {
    if (!(Test-Path -LiteralPath (Join-Path $packageRoot $required))) { throw "Setup payload is missing $required" }
}
$outputRoot = [IO.Path]::GetFullPath($OutputDir)
New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
$files = @(Get-ChildItem -LiteralPath $packageRoot -Recurse -File)
foreach ($file in $files) {
    $relative = [IO.Path]::GetRelativePath($packageRoot, $file.FullName)
    if ($relative -match '[\r\n"$]' -or $relative.StartsWith('..') -or $file.Attributes.HasFlag([IO.FileAttributes]::ReparsePoint)) { throw "Unsafe package entry: $relative" }
    if ($relative -match '^(accounts|instances|logs|secrets)([\\/]|\.)' -or $relative -eq 'awakelauncher.cfg') { throw "User data found in setup payload: $relative" }
}
$deleteList = Join-Path $outputRoot 'setup-delete-files.nsh'
$lines = @($files | ForEach-Object { 'Delete "$INSTDIR\' + [IO.Path]::GetRelativePath($packageRoot, $_.FullName) + '"' })
$lines += @(Get-ChildItem -LiteralPath $packageRoot -Recurse -Directory | Sort-Object { $_.FullName.Length } -Descending | ForEach-Object { 'RMDir "$INSTDIR\' + [IO.Path]::GetRelativePath($packageRoot, $_.FullName) + '"' })
$lines | Set-Content -LiteralPath $deleteList -Encoding utf8
$setup = Join-Path $outputRoot "Awake-Launcher-v$Version-Windows-x64-Setup.exe"
$repoRoot = Split-Path $PSScriptRoot -Parent
& $MakeNsis /WX "/DAWAKE_VERSION=$Version" "/DAWAKE_PACKAGE=$packageRoot" "/DAWAKE_OUTPUT=$setup" "/DAWAKE_ICON=$repoRoot/program_info/awakelauncher.ico" "/DAWAKE_DELETE_MANIFEST=$deleteList" "$repoRoot/program_info/awake_install.nsi"
if ($LASTEXITCODE) { throw "NSIS failed with exit code $LASTEXITCODE" }
if (!(Test-Path -LiteralPath $setup)) { throw 'NSIS did not create the setup installer.' }
$hash = (Get-FileHash -LiteralPath $setup -Algorithm SHA256).Hash.ToLowerInvariant()
"$hash  $(Split-Path $setup -Leaf)" | Set-Content -LiteralPath "$setup.sha256" -Encoding ascii
Write-Output $setup

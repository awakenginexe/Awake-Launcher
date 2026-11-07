param(
    [Parameter(Mandatory=$true)][string]$Executable,
    [Parameter(Mandatory=$true)][string]$QtBin,
    [Parameter(Mandatory=$true)][string]$Destination,
    [string]$DependencyPrefix
)
$ErrorActionPreference = 'Stop'
$binary = (Resolve-Path -LiteralPath $Executable).Path
$deployer = (Resolve-Path -LiteralPath (Join-Path $QtBin 'windeployqt.exe')).Path
if (Test-Path -LiteralPath $Destination) { throw 'Use a new, empty package destination.' }
$package = New-Item -ItemType Directory -Path $Destination
$target = Join-Path $package.FullName 'awake-core-host.exe'
Copy-Item -LiteralPath $binary -Destination $target
if (-not $env:VCINSTALLDIR) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio redist discovery requires vswhere or VCINSTALLDIR.' }
    $visualStudio = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $visualStudio) { throw 'Visual C++ runtime deployment tools are unavailable.' }
    $env:VCINSTALLDIR = Join-Path $visualStudio 'VC'
}
& $deployer --release --no-translations --no-quick-import --no-opengl-sw --no-system-d3d-compiler --no-compiler-runtime $target
if ($LASTEXITCODE) { throw "Qt deployment failed: $LASTEXITCODE" }
$redist = Get-ChildItem -LiteralPath (Join-Path $env:VCINSTALLDIR 'Redist/MSVC') -Directory |
    Where-Object Name -Match '^\d+\.\d+\.\d+$' | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
if (-not $redist) { throw 'Licensed Visual C++ redistributable files were not found.' }
$runtime = @(Get-ChildItem -LiteralPath (Join-Path $redist.FullName 'x64') -Directory -Filter 'Microsoft.VC*.CRT')
if ($runtime.Count -ne 1) { throw 'Cannot identify the x64 application-local CRT.' }
$dlls = Get-ChildItem -LiteralPath $runtime[0].FullName -File -Filter '*.dll'
if ($dlls.Name -notcontains 'vcruntime140.dll' -or $dlls.Name -notcontains 'msvcp140.dll') { throw 'Incomplete x64 CRT.' }
foreach ($dll in $dlls) { Copy-Item -LiteralPath $dll.FullName -Destination $package.FullName }
Set-Content -LiteralPath (Join-Path $package.FullName 'qt.conf') -Encoding utf8NoBOM -Value "[Paths]`nPrefix=.`nPlugins=."
Copy-Item -LiteralPath (Join-Path $PSScriptRoot '../COPYING.md') -Destination (Join-Path $package.FullName 'COPYING.md')
if ($DependencyPrefix) {
    $dependencies = (Resolve-Path -LiteralPath $DependencyPrefix).Path
    foreach ($name in @('archive.dll','zlib1.dll','bz2.dll','lzma.dll','zstd.dll','lz4.dll','libcrypto-3-x64.dll','libssl-3-x64.dll','libiconv-2.dll','libcharset-1.dll')) {
        $source = Join-Path $dependencies "bin/$name"
        if (Test-Path -LiteralPath $source) { Copy-Item -LiteralPath $source -Destination $package.FullName }
    }
    $licenses = New-Item -ItemType Directory -Path (Join-Path $package.FullName 'licenses')
    foreach ($name in @('libarchive','zlib','bzip2','liblzma','zstd')) {
        $source = Join-Path $dependencies "share/$name/copyright"
        if (Test-Path -LiteralPath $source) { Copy-Item -LiteralPath $source -Destination (Join-Path $licenses.FullName "$name.txt") }
    }
}
Write-Output $target

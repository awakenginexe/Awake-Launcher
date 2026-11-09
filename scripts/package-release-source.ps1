param(
    [Parameter(Mandatory)][ValidatePattern('^\d+\.\d+\.\d+$')][string]$Version,
    [Parameter(Mandatory)][ValidatePattern('^[0-9a-f]{40}$')][string]$Commit,
    [Parameter(Mandatory)][string]$PackageDir,
    [Parameter(Mandatory)][string]$OutputDir
)
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$package=(Resolve-Path -LiteralPath $PackageDir).Path
$output=(Resolve-Path -LiteralPath $OutputDir).Path
if ((git -C $repo rev-parse HEAD) -ne $Commit) { throw 'Source commit mismatch.' }
if ((Test-Path -LiteralPath (Join-Path $package 'core-host')) -or (Get-ChildItem -LiteralPath $package -Recurse -File -Filter 'awake-core-host.exe')) { throw 'Experimental helper must not be bundled with stable Launcher.' }
$helper='3127db4507671ae587a61cd75a6ee5da381e0274'
git -C $repo fetch origin $helper --depth=1
if ($LASTEXITCODE) { throw 'Pinned helper source is unavailable.' }
Add-Type -AssemblyName System.IO.Compression.FileSystem
function Write-SourceArchive([string]$Revision,[string]$Name,[string]$Prefix) {
    $destination=Join-Path $output $Name
    if (Test-Path -LiteralPath $destination) { throw 'Refusing to overwrite a source archive.' }
    git -C $repo archive --format=zip "--prefix=$Prefix/" "--output=$destination" $Revision
    if ($LASTEXITCODE) { throw 'Source archive failed.' }
    $archive=[IO.Compression.ZipFile]::Open($destination,[IO.Compression.ZipArchiveMode]::Update)
    try {
        foreach($module in @('libraries/libnbtplusplus','cmake/vcpkg')) {
            $entry=git -C $repo ls-tree $Revision $module
            $id=($entry -split '\s+')[2]
            if ($id -notmatch '^[0-9a-f]{40}$') { throw 'Submodule source identity unavailable.' }
            $temporary=Join-Path $output ('module-'+[guid]::NewGuid().ToString('N')+'.zip')
            git -C (Join-Path $repo $module) archive --format=zip "--prefix=$Prefix/$module/" "--output=$temporary" $id
            if ($LASTEXITCODE) { throw 'Pinned submodule source unavailable.' }
            $sub=[IO.Compression.ZipFile]::OpenRead($temporary)
            try {
                foreach($item in $sub.Entries) {
                    if($item.FullName.EndsWith('/') -or $archive.GetEntry($item.FullName)) { continue }
                    $target=$archive.CreateEntry($item.FullName,[IO.Compression.CompressionLevel]::Optimal)
                    $target.ExternalAttributes=$item.ExternalAttributes; $target.LastWriteTime=$item.LastWriteTime
                    $entrySource=$item.Open(); $stream=$target.Open()
                    try { $entrySource.CopyTo($stream) } finally { $stream.Dispose(); $entrySource.Dispose() }
                }
            } finally { $sub.Dispose(); Remove-Item -LiteralPath $temporary }
        }
    } finally { $archive.Dispose() }
}
Write-SourceArchive $Commit "Awake-Launcher-v$Version-Source.zip" "Awake-Launcher-v$Version"
Write-SourceArchive $helper 'Awake-Native-Core-3127db450-Source.zip' 'Awake-Native-Core-3127db450'
$notices=Join-Path $package 'licenses'
New-Item -ItemType Directory -Path $notices -Force | Out-Null
Invoke-WebRequest 'https://www.gnu.org/licenses/lgpl-3.0.txt' -OutFile (Join-Path $notices 'LGPL-3.0.txt')
Invoke-WebRequest 'https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt' -OutFile (Join-Path $notices 'LGPL-2.1.txt')
foreach($port in Get-ChildItem -LiteralPath (Join-Path $repo 'build/vcpkg_installed/x64-windows/share') -Directory) {
    $copyright=Join-Path $port.FullName 'copyright'
    if(Test-Path -LiteralPath $copyright) {
        $target=Join-Path $notices ('vcpkg/'+$port.Name)
        New-Item -ItemType Directory -Path $target -Force | Out-Null
        Copy-Item -LiteralPath $copyright -Destination (Join-Path $target 'copyright')
    }
}
$source=@"
Awake Launcher v$Version corresponding source: $Commit
https://github.com/awakenginexe/Awake-Launcher/tree/$Commit
The Launcher source ZIP includes pinned libnbt++ and vcpkg source trees.
Build instructions: README.md and CMakePresets.json in the source archive.
Alternatively: git clone --recurse-submodules https://github.com/awakenginexe/Awake-Launcher.git
Then check out v$Version and run git submodule update --init --recursive.

Separate Manager native-helper corresponding source (helper binary is not bundled):
$helper
https://github.com/awakenginexe/Awake-Launcher/tree/$helper
Native helper build instructions: native-core/CMakeLists.txt and docs/modpack-manager/core-protocol.md.
The helper-source ZIP contains the exact tracked source at that commit, including pinned submodules.

Qt 6.11.2 is dynamically linked; compatible modified Qt DLLs can be substituted.
Complete Qt/Chromium preferred source, copyright and third-party notices:
https://download.qt.io/official_releases/qt/6.11/6.11.2/single/qt-everywhere-src-6.11.2.tar.xz
https://doc.qt.io/qt-6.11/qtwebengine-licensing.html
https://doc.qt.io/qt-6.11/licenses-used-in-qt.html
Qt source is available without additional charge; archive URL verified during release preparation.
See packaged LICENSE, COPYING.md, LGPL texts, web/font/translation notices and vcpkg copyright files.
Microsoft application-local runtime DLLs are the licensed release redistributables, not development DLLs.
"@
$source | Set-Content -LiteralPath (Join-Path $notices 'SOURCE.txt') -Encoding utf8
$source | Set-Content -LiteralPath (Join-Path $output "Awake-Launcher-v$Version-SOURCE.txt") -Encoding utf8
foreach($file in Get-ChildItem -LiteralPath $output -File | Where-Object Extension -In '.zip','.txt') {
    $hash=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    "$hash  $($file.Name)" | Set-Content -LiteralPath ($file.FullName+'.sha256') -Encoding ascii
}

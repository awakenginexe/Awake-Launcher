# Build Awake Launcher

Requirements: CMake >= 3.28, Ninja, C++23 compiler, Qt >= 6.8 (development uses 6.11.2), JDK 17+, and Git. Qt modules: Core, CoreTools, Concurrent, Widgets, Network, NetworkAuth, OpenGL, XML, Test and LinguistTools. Image format plugins are recommended.

Run `git submodule update --init --recursive`. vcpkg provides the dependencies in vcpkg.json. On Windows, use a Visual Studio 2022 x64 developer shell and set `CMAKE_PREFIX_PATH` to the matching MSVC Qt installation.

If the checkout path contains an apostrophe, vcpkg's pkgconf/Meson build can fail on its generated machine file. Use an unused drive letter as a path alias: `subst W: $PWD.Path`, then build from `W:\`. Keep the alias while using that build cache; remove it with `subst W: /D` when finished.

```powershell
cmake --preset windows_msvc -DVCPKG_TARGET_TRIPLET=x64-windows -DLauncher_BUILD_PLATFORM=development -DLauncher_BUILD_ARTIFACT= -DENABLE_LTO=OFF
cmake --build --preset windows_msvc --config Release --parallel 4
ctest --preset windows_msvc --build-config Release
cmake --install build --config Release --prefix install
cmake --install build --config Release --prefix install --component portable
```

Linux and macOS retain their CMake presets and platform abstractions. They need native compiler/Qt/JDK dependencies and validation before binary distribution.

## Service configuration

CMake variables `Launcher_MSA_CLIENT_ID`, `Launcher_CURSEFORGE_API_KEY` and `Launcher_IMGUR_CLIENT_ID` are empty by default. Use an Awake-owned Microsoft public OAuth application registration and platform-approved credentials. Never commit private credentials.

Settings > APIs supports local Microsoft application ID and CurseForge key overrides. Treat configuration and account files as sensitive; do not add them to Git. Configure Imgur at build time. The launcher never requests Microsoft passwords itself.

Updaters, including macOS Sparkle, are disabled by default. Enable them only with Awake's own artifacts, repository/feed and signing configuration. Never point Awake updates to Prism binaries. Public Prism metadata and legacy Forge library endpoints remain compatibility dependencies.

## User data and packaging

Awake has a separate application identity and `awakelauncher.cfg`. It does not automatically migrate other launchers' data. Import instances explicitly and back up data before migrations. Portable builds keep data beside the executable.

Packages must include runtime dependencies, launcher JARs, required licenses and corresponding source availability. Development CI artifacts are unsigned and are not releases.

Windows development packages require the Microsoft Visual C++ 2015-2022 x64 runtime. The retained installer template can install that prerequisite; the portable development artifact assumes it is already installed.

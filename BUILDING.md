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

CMake variable `Launcher_MSA_CLIENT_ID` defaults to Awake Launcher's public Microsoft OAuth Client ID, `9f3c5cb3-82af-4a3e-ad36-2970397c2395`. A build-time override and the existing `MSAClientIDOverride` setting remain supported. This is a public desktop application identifier; no Microsoft client secret is needed. Existing CMake caches retain their stored values, so set this variable explicitly if an older cache contains an empty or previous Client ID.

`Launcher_CURSEFORGE_API_KEY` and `Launcher_IMGUR_CLIENT_ID` are empty by default. The Windows workflow supplies the CurseForge key from the GitHub Actions secret `AWAKE_CURSEFORGE_API_KEY`; without it, users can configure their own key and the launcher shows a diagnostic. Use platform-approved credentials belonging to Awake Launcher or your own fork. Never commit private credentials. Configure Imgur at build time.

Settings > Services > API Keys supports local Microsoft application ID and CurseForge key overrides. Treat configuration and account files as sensitive; do not add them to Git. The launcher never requests Microsoft passwords itself. Awake Launcher's OAuth, Xbox Live, and XSTS stages have been verified, but Minecraft Services currently rejects its application with HTTP 403; end-to-end sign-in remains blocked, and application approval/allowlisting may be required.

Updaters, including macOS Sparkle, are disabled by default. Enable them only with Awake Launcher's own artifacts, repository/feed and signing configuration. Never point Awake Launcher updates to Prism binaries. Public Prism metadata and legacy Forge library endpoints remain compatibility dependencies.

## User data and packaging

Awake Launcher has a separate application identity and `awakelauncher.cfg`. It does not automatically migrate other launchers' data. Import instances explicitly and back up data before migrations. Portable builds keep data beside the executable.

Packages must include runtime dependencies, launcher JARs, required licenses and corresponding source availability. Development CI artifacts are unsigned and are not releases.

Windows development packages require the Microsoft Visual C++ 2015-2022 x64 runtime. The retained installer template can install that prerequisite; the portable development artifact assumes it is already installed.

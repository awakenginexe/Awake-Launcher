# Build Awake Launcher

Requirements: CMake >= 3.28, Ninja, C++23 compiler, Qt >= 6.8 (development uses 6.11.2), JDK 17+, and Git. Qt modules: Core, CoreTools, Concurrent, Widgets, Network, NetworkAuth, OpenGL, XML, Test and LinguistTools. Image format plugins are recommended.

The primary Awake Launcher shell uses Qt WebEngineWidgets and WebChannel in addition to the modules above. Install matching modules for the same Qt/compiler kit. Node.js 24 and npm are build-time requirements for the isolated Vue 3 + TypeScript + Vite frontend in `launcher/awake-web`; end users need neither. CMake runs `npm ci` against the lockfile, builds production assets, and embeds them in the executable. No development server, CDN or remote fonts are used by the shipped shell.

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

For frontend development, run `npm ci` and `npm run dev` inside `launcher/awake-web`. A standalone browser honestly reports that the native bridge is disconnected. `npm test` verifies frontend bridge and library behavior. The native app always loads the embedded production build. Set `AWAKE_FRONTEND=widgets` to use the retained Widgets fallback, or configure `-DLauncher_ENABLE_AWAKE_WEB=OFF` to build without Chromium/Node. Do not use that option for the primary Awake Launcher release.

Windows deployment must include `QtWebEngineProcess.exe`, Chromium resources and `qtwebengine_locales`, as well as Qt DLLs and plugins. The CMake Qt deployment step handles the renderer and resources; verify these are present in packaged builds. Keep Chromium's sandbox enabled. Runtime remote debugging is only for explicit local developer testing.

## Service configuration

CMake variable `Launcher_MSA_CLIENT_ID` defaults to Awake Launcher's public Microsoft OAuth Client ID, `9f3c5cb3-82af-4a3e-ad36-2970397c2395`. A build-time override and the existing `MSAClientIDOverride` setting remain supported. This is a public desktop application identifier; no Microsoft client secret is needed. Existing CMake caches retain their stored values, so set this variable explicitly if an older cache contains an empty or previous Client ID.

`Launcher_CURSEFORGE_API_KEY` and `Launcher_IMGUR_CLIENT_ID` are empty by default. The Windows release workflow supplies the CurseForge key from the GitHub Actions repository secret `AWAKE_CURSEFORGE_API_KEY` and fails before building if it is missing. Local builds can use a user-configured key override. Use platform-approved credentials belonging to Awake Launcher or your own fork. Never commit private credentials. Configure Imgur at build time.

Settings > Services > API Keys supports local Microsoft application ID and CurseForge key overrides. Treat configuration and account files as sensitive; do not add them to Git. The launcher never requests Microsoft passwords itself. Awake Launcher's OAuth, Xbox Live, and XSTS stages have been verified, but Minecraft Services currently rejects its application with HTTP 403; end-to-end sign-in remains blocked, and application approval/allowlisting may be required.

Windows builds check Awake Launcher's own stable GitHub releases asynchronously, at most once a day automatically. Users can disable automatic checks in the update dialog or check manually from Application. Downloads open in the default browser; the launcher does not install updates silently. Settings are stored in `awake_update.cfg` in the launcher data directory. The native checker validates repository-specific release and asset URLs and compares numeric versions. macOS Sparkle and the legacy external updater remain disabled by default. Public Prism metadata and legacy Forge library endpoints remain compatibility dependencies.

## User data and packaging

Awake Launcher has a separate application identity and `awakelauncher.cfg`. It does not automatically migrate other launchers' data. Import instances explicitly and back up data before migrations. Portable builds keep data beside the executable.

Packages must include runtime dependencies, launcher JARs, required licenses and corresponding source availability. Windows release packages are unsigned.

`scripts/test-windows-setup.ps1` verifies clean installation, installed file hashes, in-place upgrade, and uninstall with a preserved user file. Run it only on a Windows account without an existing Awake Launcher registration or shortcut; the release workflow uses its disposable runner.

The GitHub workflow runs only when a stable version tag such as `v0.3.0` is pushed. The tag must match the version in `CMakeLists.txt`. It builds and tests Windows x64, bundles the Microsoft C++ runtime, and publishes a per-user NSIS setup installer and portable ZIP, each with a SHA-256 checksum. Setup includes Chromium resources and installs in `%LOCALAPPDATA%\Programs\AwakeLauncher`; it excludes `portable.txt` so launcher data stays separate. Uninstall removes only shipped files and preserves launcher data. `scripts/package-windows-setup.ps1` builds setup from a clean nonportable CMake installation using NSIS. Branch pushes and pull requests do not start builds. Publishing uses GitHub's automatic `GITHUB_TOKEN`; no personal access token or Microsoft client secret is required.

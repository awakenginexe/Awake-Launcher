# Awake Launcher

<img src="program_info/awake-launcher.png" width="96" height="96" alt="Awake Launcher logo" />

Awake Launcher is a desktop launcher for Minecraft: Java Edition. It keeps your Minecraft installations in separate instances and combines Awake's interface with Prism Launcher's C++ and Qt foundation.

[Download for Windows x64](https://github.com/awakenginexe/Awake-Launcher/releases/latest) · [Build from source](BUILDING.md) · [Report a bug](https://github.com/awakenginexe/Awake-Launcher/issues)

## Getting started

Download the **Setup.exe** from the latest release for a per-user installation with a Start Menu shortcut and uninstaller. Close Awake Launcher before installing an update. Your accounts and instances are kept separately and preserved when you uninstall.

For a portable installation, download the **Windows x64 ZIP**, extract the entire package to a writable folder, and run `awakelauncher.exe`. Keep the files together and back up the folder before replacing a portable build. Release downloads include the Microsoft C++ runtime and SHA-256 checksums.

Awake Launcher checks for stable updates at startup and daily. Use **Application → Check for updates** to check manually. Installed Windows builds offer **Update now**: download and verify Setup, close the launcher, install in the same folder, and restart while preserving accounts and instances. Close Minecraft before updating. Portable builds download a ZIP through your browser; close the launcher and extract it over your existing app folder. Install v1.2.0 manually once to receive the in-app installer.
Settings → About also shows the installed and latest versions, release notes, update checks, and Update now.

Setup offers **Normal** (`%APPDATA%\AwakeLauncher`), **Compact** (`AwakeLauncherData` inside the install folder), or **Custom** data storage. Updates preserve your choice. Changing the location does not move existing accounts or instances; choose their existing folder or copy the data while the launcher is closed. Portable ZIP builds keep their existing portable data layout.

Add an account in **Accounts**, then choose **Create instance** to install Minecraft or a modpack. You can also import an existing instance. Awake Launcher uses its own data folder and does not move data from other launchers automatically.

Release builds send one anonymous `app_started` event to PostHog EU at startup, containing only `app_version` and `os`, plus PostHog's `$process_person_profile: false` control. A random session ID is never persisted. PostHog infers country from the request; Awake does not detect country or send account details, hardware IDs, paths, instances, mods, or logs. The request is asynchronous, has no retries, and failures show no UI.

Source builds disable telemetry when `POSTHOG_PROJECT_TOKEN` is empty. Supply it through the build environment or `-DPOSTHOG_PROJECT_TOKEN=...`; `POSTHOG_HOST` defaults to `https://eu.i.posthog.com`. The release workflow reads the GitHub Actions secret `POSTHOG_PROJECT_TOKEN`. The project token is embedded in the client; no private PostHog API credential is used.

## What you can do

- Create separate vanilla, modded, and modpack instances.
- Browse and install packs from CurseForge, Modrinth, FTB, Technic, and ATLauncher.
- Manage mods, resource packs, shaders, worlds, and screenshots.
- Choose Java, memory, game settings, and Java arguments globally or per instance.
- View logs and manage accounts from the launcher.
- Save named skins locally with 3D library previews, independently of applying them to a Minecraft account. The library follows your selected data folder.
- Preview and rotate Minecraft skins, load a PNG, or search another player's public skin before applying it to your own Java Edition account.
- Select your owned capes using image previews. Your skin head appears beside the welcome name and account choices.

## Version 1.1.0

This release adds the new Awake logo and the **Skins** panel. **Reset to default** discards pending skin, model, and cape edits and returns to the latest saved account appearance. **Reset to Minecraft default** previews Minecraft's default skin. Changes are saved only when you click **Apply skin**.

The minimum window size is **1320×780**. Skin controls scroll separately from the software-rendered 3D preview. Cape previews remain visible after saving.

## Development status

Awake Launcher is under active development. Windows x64 is the current build and validation platform. Linux and macOS need Awake-specific validation before release.

Minecraft's application review approved Awake Launcher's Microsoft application ID on October 5, 2026 for the Minecraft API allow list. Add your Microsoft account in **Accounts** to sign in.

Official builds use Awake's own Microsoft application registration. Release builds receive the application ID through GitHub Actions configuration; it is absent from the current tracked source. Source builds and forks must supply their own approved application ID. No Microsoft client secret or Microsoft password is embedded in the launcher. Approval covers API allow-list access and does not imply endorsement by Mojang or Microsoft.

## Help and contributions

For a bug report, include your Awake Launcher version, operating system, steps to reproduce, and logs with account information removed. See [how to contribute](CONTRIBUTING.md) before submitting a change. Read the [community standards](CODE_OF_CONDUCT.md) before participating.

## License

Awake Launcher code is licensed under [GNU GPL version 3 only](LICENSE). [COPYING.md](COPYING.md) explains the third-party copyright and license notices. The logo and other branding in `program_info/` use [CC BY-SA 4.0](program_info/LICENSE); bundled translations have a separate [Apache-2.0 license](launcher/awake/translations/LICENSE). Awake Launcher is independent of Prism Launcher, Mojang, and Microsoft.

# Awake Launcher

Awake Launcher is a desktop launcher for Minecraft: Java Edition. It keeps your Minecraft installations in separate instances and combines Awake's interface with Prism Launcher's C++ and Qt foundation.

[Download a Windows development build](https://github.com/awakenginexe/Awake-Launcher/actions/workflows/awake-build.yml) · [Build from source](BUILDING.md) · [Report a bug](https://github.com/awakenginexe/Awake-Launcher/issues)

## Getting started

Download the complete Windows development package, extract it to a writable folder, and run `awakelauncher.exe`. Keep the files together and back up the folder before replacing a portable build. Windows requires the Microsoft Visual C++ 2015–2022 x64 runtime.

Add an account in **Accounts**, then choose **Create instance** to install Minecraft or a modpack. You can also import an existing instance. Awake Launcher uses its own data folder and does not move data from other launchers automatically.

## What you can do

- Create separate vanilla, modded, and modpack instances.
- Browse and install packs from CurseForge, Modrinth, FTB, Technic, and ATLauncher.
- Manage mods, resource packs, shaders, worlds, and screenshots.
- Choose Java, memory, game settings, and Java arguments globally or per instance.
- View logs and manage accounts from the launcher.

## Development status

Awake Launcher is under active development. Windows x64 is the current build and validation platform. Linux and macOS need Awake-specific validation before release.

Microsoft account linking currently reaches Minecraft Services but can be rejected there with HTTP 403. Until the application is approved, an account may not be added successfully.

## Help and contributions

For a bug report, include your Awake Launcher version, operating system, steps to reproduce, and logs with account information removed. See [how to contribute](CONTRIBUTING.md) before submitting a change. Read the [community standards](CODE_OF_CONDUCT.md) before participating.

## License

Awake Launcher code is licensed under [GNU GPL version 3 only](LICENSE). [COPYING.md](COPYING.md) explains the third-party copyright and license notices. The logo and other branding in `program_info/` use [CC BY-SA 4.0](program_info/LICENSE); bundled translations have a separate [Apache-2.0 license](launcher/awake/translations/LICENSE). Awake Launcher is independent of Prism Launcher, Mojang, and Microsoft.

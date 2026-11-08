# Play a Manager workspace in Awake Launcher

Requires **Awake Launcher 3.1.0-beta.1** and **Awake Modpack Manager 0.2.0-beta.1**, with Manager's pinned Native Core 0.2.1. Launcher 3.0.0 and Manager 0.1.0 do not support this workflow. These development builds do not change those stable releases.

1. Create a workspace in Manager and finish installing its mods.
2. Choose **Open in Awake Launcher** on its library card. If Launcher is not found, add the compatible beta installation folder in Manager's Settings → Launcher connection.
3. Alternatively, use **Link Existing Manager Workspace** in Launcher's library. Select the folder containing `instance.cfg`, `mmc-pack.json`, and `.minecraft`. Manager displays its workspace path on the library card.
4. In Launcher, choose your Microsoft account and Java settings, then press Play. A build without an embedded Microsoft application ID requires your authorized Awake Launcher application ID under Settings → APIs before signing in.
5. Close Minecraft normally before changing mods in Manager. The next launch uses those changed files directly.

## Storage and safety

Manager owns `<Manager data>/core/workspaces/<workspace UUID>`. Launcher stores only a physical-identity/path registration and its own settings under `<Launcher data>/manager-links/`; it creates no second game directory. Linking can also be requested with `--link-manager-workspace <folder>`. Repeated requests select the same instance, including in a running Launcher.

Linking requires a published Manager catalog entry, matching canonical ownership and directory identity. Unknown, replaced, incomplete, or unrecovered workspaces are rejected. Windows fixed local storage and the same Windows account are required by the existing Native Core checks. Moving a workspace is not supported by this beta.

Launcher resolves Minecraft/loader libraries and runtime files through its existing launch engine. It keeps account/Java/playtime preferences separately, does not save Manager's `instance.cfg` or `mmc-pack.json`, and does not rename mod files or clean provider indexes during scanning. Mod installation, pack replacement, export/copy, and deletion are disabled for linked instances. Manage their content in Manager.

Launch preparation and Manager operations share Native Core's exclusive physical-directory lease. Recovery is checked while holding it. The existing Windows game-process group tracks the game and descendants, including when Launcher exits, so Manager cannot mutate files until the game has stopped. An unverifiable process state stays blocked.

## Existing unrelated defects

Prior large-pack testing reported export omitting Frights and Foliage, approximately seven-minute inventory, 11–13-minute preparation, a normal inventory request returning BUSY after 30.24 seconds, and a BetterEnd conflict preventing full original-pack acceptance. These remain separate defects. This feature does not rerun that bulk suite or alter the original diagnostic evidence.

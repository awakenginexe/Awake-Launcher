# Project: Awake Launcher

Build a new general-purpose Minecraft launcher named **Awake Launcher**, using the latest upstream **Prism Launcher** codebase as the technical foundation.

Upstream:
https://github.com/PrismLauncher/PrismLauncher

This is NOT a server-specific launcher and must never evolve into one.

The goal is to preserve the freedom, flexibility, reliability, instance management, mod-loader support, and launch performance that make Prism Launcher excellent, while replacing its dated/utilitarian user experience with a polished, modern, distinctive desktop application.

Think of the product as:

> The power and freedom of Prism Launcher, with a significantly more refined modern desktop experience.

The launcher must remain useful for ordinary vanilla players, modded players, modpack users, developers, power users, and people who play across many unrelated Minecraft servers.

---

# 1. NON-NEGOTIABLE PRODUCT PRINCIPLES

The following requirements have priority over visual redesign.

1. Preserve Prism Launcher's Minecraft launching reliability.
2. Preserve or improve runtime performance.
3. Preserve its multi-instance philosophy.
4. Preserve user freedom.
5. Do not lock the user into a server, modpack, ecosystem, hosting provider, or content platform.
6. Do not turn the launcher into a server advertisement.
7. Do not sacrifice advanced functionality simply to make the interface simpler.
8. Beginners should find common actions easy.
9. Power users must still be able to access advanced configuration.
10. Existing Prism concepts that already work well should be reused rather than reimplemented unnecessarily.
11. Avoid unnecessary background services.
12. Once Minecraft is running, launcher overhead should be negligible.
13. Do not introduce telemetry unless explicitly requested later.
14. Do not introduce advertisements, sponsored content, or dark patterns.
15. Every interactive UI element must have real functionality.

This is a real desktop application, not a visual prototype.

---

# 2. START WITH AN ARCHITECTURE AUDIT

Do not immediately rewrite the UI.

First inspect the current Prism Launcher architecture and identify:

- launcher core
- instance management
- Minecraft version management
- Java management
- account/authentication system
- Microsoft/Xbox/Minecraft authentication
- launch pipeline
- process management
- download system
- metadata system
- mod loader installation
- Modrinth integration
- CurseForge integration
- import/export
- settings
- translations
- Qt UI dependencies
- models/controllers that are currently coupled to widgets
- components that can remain untouched
- components that would require an adapter for a new UI

Produce an internal architecture map before modifying major components.

The core principle is:

**Reuse the proven Prism engine. Modernize the product around it.**

Do not rewrite stable launcher logic merely because rewriting it is aesthetically cleaner.

---

# 3. UI TECHNOLOGY DECISION

**2026-10-03 owner decision, superseding the options below:** use Qt WebEngine + Vue 3 + TypeScript + Vite with a narrow QWebChannel bridge. Preserve Prism's C++ engine and existing complex dialogs. Production assets are embedded locally. Do not introduce Electron or Tauri, or rewrite backend systems. Retain the old Awake shell temporarily as a fallback. See [DESIGN.md](../DESIGN.md) for the current screenshot-led composition.

Do not force a web stack onto the application without evidence.

Evaluate the practical options, including:

### Option A
Modernized Qt 6 Widgets with custom reusable components.

### Option B
Qt Quick / QML frontend connected to the existing C++ launcher core.

### Option C
Hybrid Qt Widgets + QML migration.

### Option D
A separate modern frontend such as Tauri only if a clean, maintainable boundary with the existing Prism core can be demonstrated without introducing unnecessary complexity or performance regression.

Choose based on:

- integration difficulty
- runtime memory
- startup speed
- rendering performance
- maintainability
- build complexity
- cross-platform behavior
- access to the existing Prism C++ models
- amount of core code that must be rewritten
- long-term development cost

Prefer the solution that gives us the best modern UI **without destabilizing Prism's mature backend**.

Do not rewrite the launcher core in JavaScript.

Document the decision and reasoning before beginning a large UI migration.

---

# 4. PERFORMANCE IS A FIRST-CLASS FEATURE

Prism Launcher is our baseline.

Before large changes, record baseline behavior where practical:

- cold startup time
- warm startup time
- idle CPU usage
- idle memory usage
- instance loading time
- time from pressing Launch until Minecraft process starts
- download performance
- UI responsiveness with many instances
- behavior while Minecraft is running

After major milestones, compare against the baseline.

The redesigned launcher must not meaningfully reduce Minecraft performance.

Avoid:

- unnecessary polling
- constant animations
- unnecessary timers
- background WebViews doing work while hidden
- excessive filesystem watchers
- redundant metadata refreshes
- repeated hashing
- unnecessary network requests
- large synchronous operations on the UI thread
- keeping expensive UI surfaces alive while Minecraft is running

Provide settings for launcher behavior after Minecraft launches:

- Keep launcher open
- Minimize launcher
- Hide launcher
- Close launcher when safe

Idle CPU usage should be effectively negligible.

Do not claim performance improvements without measurement.

---

# 5. GENERAL-PURPOSE INSTANCE MANAGEMENT

Instances remain the heart of the launcher.

Users must be free to create and manage unrelated Minecraft installations.

Support and preserve, where Prism currently supports them:

- Vanilla Minecraft
- Fabric
- Forge
- NeoForge
- Quilt
- custom Minecraft versions where supported
- independent instance directories
- per-instance Java version
- per-instance memory settings
- per-instance JVM arguments
- per-instance environment settings
- per-instance resolution
- per-instance icons
- notes
- groups/categories
- custom launch commands
- mods
- resource packs
- shader packs
- worlds
- screenshots
- logs
- configuration files

Do not make a server address mandatory.

An instance may have:

- zero servers
- one server
- many servers

Servers are content inside an instance, not the identity of the launcher.

---

# 6. INSTANCE LIBRARY EXPERIENCE

Redesign the main launcher library around actual player behavior.

The interface should make it easy to:

- see all instances
- launch a recent instance quickly
- search instances
- filter instances
- group instances
- sort instances
- pin favorites
- create an instance
- duplicate an instance
- rename an instance
- change its icon
- edit its configuration
- export it
- delete it safely
- inspect problems
- view last played information
- see Minecraft version
- see mod loader and loader version

Support both:

- visual card/grid view
- compact list view

Do not make every instance a giant decorative card.

Users with 50+ instances must still have an efficient interface.

---

# 7. INSTANCE DETAIL VIEW

Create a powerful instance detail screen.

It should expose useful sections such as:

### Overview
- instance name
- icon
- Minecraft version
- loader
- loader version
- Java runtime
- memory allocation
- last played
- play time if already supported/reliably available
- main launch action

### Mods
- installed mods
- enable/disable
- version
- source
- update status
- search
- install
- remove
- open file location

### Resource Packs

### Shader Packs

### Worlds

### Screenshots

### Servers

### Logs

### Minecraft / Loader Components

### Java & Runtime

### Advanced Settings

Do not remove advanced settings just because they are uncommon.

Use progressive disclosure instead.

---

# 8. MOD AND MODPACK EXPERIENCE

Preserve Prism's existing integrations and improve discoverability.

Where APIs and licenses permit, support:

- Modrinth
- CurseForge
- local mod files
- modpack import
- modpack export
- common Prism/MultiMC-compatible formats
- drag-and-drop importing where appropriate

Users should be able to understand:

- where a mod came from
- current version
- available version
- whether it is enabled
- whether it appears compatible
- dependencies when this information is available

Do not fabricate compatibility information.

Do not automatically replace local user modifications without clearly defined ownership rules.

Protect user data during updates.

---

# 9. MICROSOFT / MINECRAFT AUTHENTICATION

Preserve Prism's legitimate Microsoft/Xbox/Minecraft authentication architecture where appropriate.

However:

**Do not ship Awake Launcher using Prism Launcher's production identity/API credentials as if they belonged to Awake Launcher.**

Audit every API credential inherited from upstream.

For Awake Launcher:

- use our own Microsoft application registration when available
- use our own API credentials where third-party services require them
- if a credential is unavailable, make the integration configurable or disabled
- provide documented development configuration
- never commit private secrets
- never ask users for their Microsoft password directly
- use official OAuth flows
- securely store refresh/session credentials using appropriate OS facilities where practical

Do not implement cracked account authentication or an authentication bypass.

Offline behavior may only preserve legitimate functionality supported for previously authenticated users.

Account UI should support:

- multiple Microsoft accounts
- account switching
- Minecraft profile name
- UUID where useful
- skin/head preview
- sign out
- authentication errors with understandable explanations

Authentication failures must never destroy unrelated launcher data.

---

# 10. JAVA MANAGEMENT

Preserve or improve Prism's excellent Java management behavior.

The launcher should be able to:

- detect installed Java runtimes
- associate appropriate Java versions with Minecraft versions
- allow automatic Java selection
- allow manual Java selection
- validate selected runtimes
- configure RAM
- configure JVM arguments
- configure Java per instance
- expose diagnostics when Java is incorrect

Never silently change advanced user JVM settings without explanation.

Make the recommended/default path easy while preserving manual control.

---

# 11. DESIGN DIRECTION

Awake Launcher should look like a premium desktop application, not a reskinned Prism build and not a generic AI-generated dashboard.

Desired qualities:

- modern
- calm
- premium
- responsive
- highly readable
- deliberate
- slightly playful where appropriate for Minecraft
- desktop-native feeling
- visually distinctive
- dark-mode-first, but architected cleanly enough for additional themes
- content-focused

Use effects only when they serve hierarchy.

Glass, blur, shadows, transparency and motion are allowed but must be restrained.

Do NOT produce:

- a generic Discord clone
- a generic game launcher clone
- giant gradients everywhere
- neon RGB everywhere
- excessive glow
- excessive glassmorphism
- endless pulsing status indicators
- meaningless badges
- a dashboard full of fake statistics
- giant hero banners that waste space
- arbitrary bento grids
- decorative charts
- fake activity feeds
- placeholder server populations
- fake player counts
- pointless animated particles
- rounded cards inside rounded cards inside rounded cards

Every piece of information must come from real application state.

The interface should still feel unmistakably like a Minecraft launcher when the Awake logo is removed.

---

# 12. VISUAL HIERARCHY

The most important action on the main screen is:

**Launch an instance.**

The second most important job is:

**Find or manage an instance.**

Design around those jobs.

Do not design around marketing content.

A possible navigation model might include:

- Library
- Discover
- Downloads / Tasks
- Accounts
- Settings

But do not blindly implement this structure.

Derive the final navigation from actual launcher functionality.

Instance-specific navigation belongs inside the selected instance rather than cluttering the global navigation.

---

# 13. MOTION

Motion should communicate state.

Good uses:

- selected-instance transitions
- opening detail panels
- progress transitions
- download completion
- context menu appearance
- hover/focus response
- navigation transitions

Avoid:

- animation for animation's sake
- looping floating elements
- perpetual glow
- bounce effects
- long transitions
- anything delaying user input

Respect reduced-motion accessibility settings.

---

# 14. ACCESSIBILITY

The launcher must remain functional with:

- keyboard navigation
- visible keyboard focus
- scalable text
- reasonable contrast
- Windows display scaling
- high DPI displays
- different window sizes
- long Minecraft/mod names
- long Thai strings
- Chinese characters

Avoid relying only on color to communicate state.

Error states must explain:

1. what failed
2. what the user can do next
3. where more technical details can be found

---

# 15. LANGUAGE SUPPORT

Awake Launcher will initially support exactly these user-facing languages:

1. English
2. Thai
3. Chinese (Simplified)
4. Chinese (Traditional)

Use locale identifiers such as:

- `en`
- `th`
- `zh_CN` / `zh-Hans`
- `zh_TW` / `zh-Hant`

Choose the convention most appropriate for the framework and use it consistently.

English is the source/fallback language.

Requirements:

- no user-facing string should be hardcoded directly into application views
- all UI strings must use the localization system
- preserve placeholders safely
- support pluralization where needed
- avoid layouts that assume English word length
- test Thai line wrapping
- test Chinese punctuation and spacing
- do not use Simplified Chinese text as the Traditional Chinese translation
- do not machine-convert translations at runtime

The interface should automatically detect the OS locale on first launch and fall back to English.

Users must be able to override the language in Settings.

Do not inherit dozens of incomplete upstream languages into the Awake user-facing build unless we explicitly decide to later.

Keep the localization architecture extensible so more languages can be added in the future.

---

# 16. THEMING

Build a coherent design-token system instead of scattering color values throughout the project.

At minimum define semantic tokens for:

- app background
- elevated surface
- secondary surface
- primary text
- secondary text
- disabled text
- border
- accent
- accent hover
- success
- warning
- error
- selection
- focus ring

Components should consume semantic tokens.

Do not hardcode appearance into business logic.

Prepare the architecture for future custom themes without making theme creation the first milestone.

---

# 17. DOWNLOAD / TASK MANAGER

Improve visibility into background operations.

Users should be able to see:

- current downloads
- installation operations
- modpack imports
- updates
- progress
- speed where accurately measurable
- errors
- retry actions

Do not create fake smooth progress if the underlying process cannot report progress accurately.

A failed background task must not disappear silently.

---

# 18. ERROR HANDLING

Errors should have two layers:

### Human-readable
Example:

"Java 21 could not be found. Install it automatically or choose an existing Java installation."

### Technical details
Provide expandable details containing the actual diagnostic information.

Never replace useful Prism diagnostic information with vague messages such as:

"Something went wrong."

Preserve logs.

Provide convenient copy-to-clipboard actions for diagnostics.

---

# 19. USER DATA SAFETY

Treat existing instance data as valuable.

Before migrations:

- understand the upstream format
- preserve compatibility where practical
- back up data before destructive schema migrations
- never silently delete unknown files
- never wipe user instances because parsing failed
- never assume every file inside an instance is launcher-managed

Differentiate between:

- files owned by the launcher
- files owned by a modpack
- files created by the user

Destructive actions require clear confirmation.

---

# 20. UPSTREAM COMPATIBILITY

We are building a fork, but do not intentionally make future upstream maintenance impossible.

Where practical:

- isolate Awake-specific UI code
- minimize invasive modifications to stable Prism launcher core
- document deviations from upstream
- keep major backend changes small and reviewable
- avoid unnecessary renames of mature internal components
- keep an `UPSTREAM.md` documenting the upstream commit used
- document how upstream changes can be merged/cherry-picked later

Do not blindly merge upstream changes without testing.

---

# 21. LICENSING AND BRANDING

Treat licensing as an engineering requirement.

Prism Launcher currently identifies its launcher code as **GPL-3.0-only**.

Awake Launcher must comply with the upstream license and licenses of bundled dependencies.

Requirements:

- preserve legally required copyright/license notices
- maintain source-code availability as required by GPL when distributing binaries
- document modifications appropriately
- do not imply that Awake Launcher is Prism Launcher
- do not imply endorsement or affiliation with Prism Launcher
- replace Prism branding with Awake branding
- do not reuse the Prism logo as our product logo
- audit upstream API keys
- replace upstream API credentials with our own where necessary
- leave credentials empty/disabled when appropriate rather than impersonating another project

Add an About/Open Source Licenses area.

Do not remove attribution that licenses require.

---

# 22. BRANDING

Working product name:

**Awake Launcher**

Do not over-brand the interface.

The brand should appear naturally in:

- app icon
- title
- first-run experience
- About page
- subtle identity elements

The actual content of the application should remain Minecraft and the user's instances.

Do not plaster "AWAKE" across every screen.

---

# 23. FIRST-RUN EXPERIENCE

First run should be short.

Possible flow:

1. Welcome
2. Choose language, preselected from OS
3. Add Microsoft account or skip temporarily where technically valid
4. Detect Java
5. Open the instance library

Avoid a long setup wizard.

Experienced users should be able to get to the launcher quickly.

---

# 24. SETTINGS

Organize settings by purpose, not arbitrary technical categories.

Potential categories:

- General
- Appearance
- Accounts
- Java
- Minecraft
- Downloads
- Network
- Integrations
- Advanced
- About

Preserve advanced Prism settings where they remain relevant.

Provide search if the settings surface becomes large enough to justify it.

Do not hide important controls solely for visual simplicity.

---

# 25. SEARCH

Search should be useful throughout the application.

Examples:

- instances
- mods inside an instance
- settings
- downloadable modpacks where supported

Search should be fast and should not require unnecessary network requests for local content.

---

# 26. WINDOW BEHAVIOR

Support desktop expectations:

- minimize
- maximize
- restore
- close
- resize
- remember window size where appropriate
- high DPI
- multiple monitors

If implementing a custom title bar, do not break:

- dragging
- double-click maximize
- snapping
- keyboard accessibility
- Windows 11 behavior

Native behavior is more important than visual novelty.

---

# 27. CROSS-PLATFORM POLICY

Prism is cross-platform.

Do not casually destroy that advantage.

Primary development and visual validation may focus on Windows 11, but architecture should preserve practical support for:

- Windows
- Linux
- macOS

Platform-specific behavior should be isolated behind appropriate abstractions.

Do not litter application logic with platform checks.

---

# 28. CODE QUALITY

Rules:

- understand before rewriting
- keep UI logic separate from launcher/business logic
- no giant god components
- no duplicated state
- no unnecessary abstraction for hypothetical future requirements
- no dead code
- no fake functionality
- no commented-out replacement implementations
- no meaningless generated comments
- comments should explain WHY, not restate WHAT
- handle cancellation
- handle errors
- handle application shutdown during background work
- keep threading/concurrency behavior explicit
- avoid blocking the UI thread

Use existing Prism abstractions when they are already good.

Refactor only where there is a clear benefit.

---

# 29. TESTING

At minimum verify:

### Accounts
- add account
- refresh session
- switch account
- sign out
- auth failure

### Instances
- create
- duplicate
- edit
- launch
- delete
- import
- export where supported

### Minecraft
- vanilla launch
- Fabric launch
- Forge launch
- NeoForge launch
- Quilt launch where currently supported

### Java
- automatic detection
- manual runtime
- missing Java
- wrong Java version

### Content
- install mod
- remove mod
- enable/disable mod
- resource packs
- shaders
- modpack installation

### UI
- English
- Thai
- Simplified Chinese
- Traditional Chinese
- keyboard navigation
- high DPI
- long text
- window resizing
- empty state
- loading state
- error state

### Performance
Compare important metrics against the upstream Prism baseline.

---

# 30. MIGRATION STRATEGY

Do not attempt to rewrite the entire launcher in one giant commit.

Suggested phases:

## Phase 0 - Audit
Understand Prism and document architecture.

## Phase 1 - Fork Identity
Rename/brand correctly, establish license compliance and remove/replace upstream production API credentials.

## Phase 2 - Design Foundation
Design tokens, typography, reusable components, localization foundation.

## Phase 3 - Main Library
Build the new instance library and launch workflow.

## Phase 4 - Instance Detail
Move instance management functionality into the new experience.

## Phase 5 - Accounts & Authentication UI
Modernize account management without replacing proven authentication logic unnecessarily.

## Phase 6 - Settings
Modernize settings while preserving advanced controls.

## Phase 7 - Content Discovery
Improve mod/modpack discovery and installation.

## Phase 8 - Polish
Accessibility, responsiveness, animation, localization, diagnostics.

## Phase 9 - Performance Validation
Benchmark against upstream Prism and address regressions.

## Phase 10 - Release Readiness
Packaging, licenses, migration, clean install testing, upgrade testing.

Each phase must leave the project in a buildable state.

---

# 31. VERSION CONTROL

Work in a dedicated branch.

Make small coherent commits after meaningful milestones.

Commit messages should describe actual changes.

Do not squash everything into one massive commit during development.

Do not push to a remote unless explicitly instructed.

Before finishing each major task:

- build the project
- run relevant tests
- inspect git diff
- verify no credentials or secrets were accidentally committed
- verify the working tree is understood

---

# 32. DO NOT REMOVE FEATURES JUST BECAUSE THEY ARE HARD TO REDESIGN

This is extremely important.

When an old Prism screen contains functionality that does not fit the new design, the solution is NOT automatically to remove it.

Instead:

1. understand what the feature does
2. determine who uses it
3. find an appropriate place for it in the new information architecture
4. redesign the interaction
5. preserve functionality

If functionality truly needs removal, document the reason and ask before removing it.

---

# 33. DEFINITION OF SUCCESS

Awake Launcher succeeds when a Prism user can move to it and think:

> "Everything I used Prism for is still here, but this feels like a modern application."

A new Minecraft player should think:

> "I can understand how to create an instance and play without reading a manual."

A power user should think:

> "The launcher did not take my control away."

And technically:

- Minecraft launches as reliably as upstream
- game performance is not meaningfully worse than upstream Prism
- existing backend strengths are preserved
- UI remains responsive
- no server/ecosystem lock-in exists
- localization works correctly
- advanced controls remain available
- the product has its own visual identity
- the fork complies with upstream licensing requirements

---

# 34. FIRST TASK

Do NOT start by redesigning random screens.

Start now with the following:

1. Inspect the latest Prism Launcher repository.
2. Record the exact upstream commit SHA.
3. Map its major architecture.
4. Identify which systems can remain nearly untouched.
5. Identify the coupling between Qt UI and launcher core.
6. Evaluate Qt Widgets modernization vs QML/hybrid vs a separate frontend bridge.
7. Recommend the lowest-risk architecture capable of achieving the desired premium UI.
8. Identify licensing, branding and API-key changes required for a legitimate fork.
9. Identify the current localization architecture and how to reduce the shipped languages to:
   - English
   - Thai
   - Chinese Simplified
   - Chinese Traditional
10. Establish performance measurements from upstream before significant changes.
11. Produce a concrete implementation plan by phase.
12. Then begin Phase 1.

Do not stop after writing a plan if the repository and development environment are available.

Proceed with implementation.

When something is uncertain, inspect the source instead of guessing.

When upstream already solves a difficult Minecraft problem correctly, prefer adapting it over reimplementing it.

The final product must be **Awake Launcher**, not "Prism Launcher with a new theme."

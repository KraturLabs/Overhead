# NameplateLab

An experimental Ashita 4.30 plugin for FFXI overhead names and damage numbers. Current version: **0.5.0**, resident loader ABI 2. Supports one verified client build; other clients refuse compatibility checks.

## Features

- Native name font, colors, icons and placement, with optional enemy HP coloring.
- Independent Size and Width controls for names and damage numbers.
- **Match original 4:3** presets calculate proportions from display and rendering dimensions while retaining native height.
- Name filtering: Native, Sharp or Smooth. Uses loaded game artwork, including XIPivot overrides.
- Saved settings and live engine updates through `/nplab reload`.

Damage adjustments start off. Select the damage preset or move a damage slider to enable them. Damage retains the game's own animation, font, colors and submission path; separate addon-created battle text is unaffected. A calculated 4:3 baseline does not establish the intended shape of custom font artwork.

## Build

Requires Windows, Visual Studio 2022 C++ tools with the Windows SDK, CMake, and Python 3. Build Win32 even on 64-bit Windows.

```powershell
.\prepare-sdk.ps1
python -m venv .venv
.\.venv\Scripts\python -m pip install capstone==5.0.7 pefile==2024.8.26
.\.venv\Scripts\python tools/prepare_profile.py 'C:\Path\To\FINAL FANTASY XI\FFXiMain.dll'
.\build.ps1
```

The SDK fetcher downloads official headers pinned to Ashita commit `4171c74c8ddb2ca2a31654f199e6c1cee40d7256`. See the [upstream SDK](https://github.com/AshitaXI/Ashita-v4beta/tree/4171c74c8ddb2ca2a31654f199e6c1cee40d7256/plugins/sdk) for its terms. Dependencies are not vendored.

The profile generator reads a user-provided client without executing or modifying it. It accepts only SHA256 `f2245d1c9d06e02c36624942483913f5120c0d40777fc1bb8703c6f4bda823e4`. It generates `src/client_profile.h` locally; game binaries, artwork and extracted compatibility bytes are not distributed here. Generating a profile for arbitrary clients is deliberately unsupported.

Outputs: `build/Release/nameplatelab.dll` (resident loader) and `nameplatelab_engine.dll` (reloadable engine). Local development tests are optional and absent from this repository; a clean checkout builds the production targets. Building does not install or run the plugin.

## Runtime setup

This is a source preview, not a packaged release. The loader embeds the checkout's absolute `runtime` path; keep that checkout in place. Moving it requires rebuilding and replacing the loader with the game closed.

With the game closed, copy the resident loader to the Ashita installation's `plugins/nameplatelab.dll`. Create `runtime` in the checkout, copy the engine there as `engine-<lowercase SHA256>.dll`, and write that filename plus a newline to `runtime/current.txt`. The engine's file hash must match its filename. Keep old generations immutable. Do not overwrite the resident loader after it has loaded; it remains pinned until the game exits, even after `/unload`.

```text
/load nameplatelab
/nplab
/nplab status
```

The settings window has separate Nameplates and Damage numbers sections. Settings save under the Ashita installation's `config/nameplatelab/settings.ini`.

```text
/nplab fit
/nplab size 1.2
/nplab width 0.8
/nplab damage fit
/nplab damage size 1.2
/nplab damage width 0.8
/nplab damage off
```

For a verified engine-only update, add a new hash-named DLL, replace `runtime/current.txt` atomically, then run `/nplab reload`. `/nplab build` identifies the loaded generation. The resident loader serializes callbacks and unloads the previous engine after they return. Loader/ABI changes require a game restart. `/unload nameplatelab` restores owned hooks and unloads the engine; `/load nameplatelab` loads the selected generation again.

## Compatibility and verification

Keep the original Nameplate plugin unloaded. BattleSight integration is not implemented. The supported shared submission-entry detour is left unchanged; unknown changes to the guarded name/damage routines refuse compatibility checks. This is not universal addon compatibility or crash containment.

Version 0.4.1 had live acceptance for names/HP, normal addons, map behavior, unload/load and a genuine update. The user accepted the 0.4.2 name sizing preset. Version 0.5.0 passed offline damage sizing, settings persistence, machine-state preservation and repeated unload/update checks; its live damage appearance and coexistence remain pending. Zoning, device reset, long-session stability and current-version performance remain unverified.

Implementation is independent; no other addon/plugin implementation is incorporated. No game assets, client binaries, local diagnostics, personal settings or generated builds are included.

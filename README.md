# NameplateLab

An experimental Ashita 4.30 plugin for FFXI overhead names and damage numbers. Current source version: **0.9.5**, a single unloadable DLL. Supports one verified client build; other clients refuse compatibility checks.

## Features

- Native name font, colors, icons and placement, with optional enemy HP coloring.
- Independent player status icons: **party seeking / bazaar / linkshell**, ordered left-to-right beside the name. Active icons pack toward the name and extend left without contributing to its centering width. Each retains native single-icon size and vertical placement; linkshell retains its actual color. A checkbox shows or hides all three.
- Independent Size and Width controls for names and damage numbers.
- **Match original 4:3** presets apply the complete correction through Width while retaining native height. Size and Width remain manually adjustable; there are no separate stretch toggles. Existing saved legacy correction settings retain their appearance until a preset or reset is selected. The damage preset is available immediately when display dimensions are available; selecting it enables damage adjustments.
- Name filtering: Native, Sharp or Smooth. Uses loaded game artwork, including XIPivot overrides.
- Opt-in native overhead cursor when an addon hides the target panel; keeps the game's arrow animation and main/subtarget colors.
- Monster levels beside the name, colored by the observed check difficulty. Unknown levels stay hidden; confirmed impossible-to-gauge monsters show magenta `Lv.???`. Automatic checks are silent; manual `/check` output remains visible. Display and automatic checking have separate saved toggles, both on by default.
- Monster detection/linking icons and an aggression strip to the left of the level/name. True sight, sight, sound, magic, job ability, blood and link; true sight replaces ordinary sight. Red means aggressive, blue means passive, and unknown data stays hidden. These are database defaults, not current hostility; private-server behavior may differ.
- Saved settings; update in game with unload, replace the DLL, then load.
- Observed enemy debuffs in a centered icon row above the name, with independent display, target-only and size controls. Uses the client's status artwork through Ashita resources; no addon dependency.

Damage adjustments start off; the hook is installed on first use. Setup failures report their cause; `/nplab damage retry` retries after resolving a conflict. With Keep native overhead cursor enabled, the cursor uses the drawn debuff row's bounds to rise above it and return when it disappears. The 0.9.5 debuff display, corrected icon colors, cursor clearance and steady debuff opacity have been accepted in game. The 0.6.3 names, damage and cursor baseline was accepted in game. The 0.7.1 level feature is accepted in game: levels display, automatic checks stay silent with SimpleLog, and manual checks remain visible.

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

Output: `build/Release/nameplatelab.dll`. Local development tests are optional and absent from this repository; a clean checkout builds the production targets. Building does not install or run the plugin.

## Runtime setup

This is a source preview, not a packaged release. Copy `build/Release/nameplatelab.dll` to the Ashita installation's `plugins/nameplatelab.dll`. No project directory, engine DLL or manifest is needed at runtime.

If upgrading from the old resident-loader version, exit the game once before replacing it: that already-loaded DLL remains pinned even after `/unload`. Subsequent normal updates use the workflow below.

```text
/load nameplatelab
/nplab
/nplab status
```

The settings window has General and Details tabs. General contains names, cursor and damage controls. Details contains the level display/automatic-check toggles and a live Level size slider (25-300%, relative to name size), with a reset to 100%. Level size is saved independently. The Details tab and live size control were accepted in game in 0.7.2. Details also contains a saved monster-traits toggle (on by default), an independent live Trait size slider (25-300%), and Reset trait size. The 0.8.0 traits were accepted in game. Each Details section now has its own saved **Only on current target** option; these default off to preserve existing settings. The target filters and subtle separator were accepted in game in 0.8.1. Settings save under the Ashita installation's `config/nameplatelab/settings.ini`.

```text
/nplab fit
/nplab size 1.2
/nplab width 0.8
/nplab icons show
/nplab icons hide
/nplab cursor on
/nplab cursor off
/nplab levels on
/nplab levels off
/nplab autocheck on
/nplab autocheck off
/nplab traits on
/nplab traits off
/nplab debuffs on
/nplab debuffs off
/nplab damage fit
/nplab damage size 1.2
/nplab damage width 0.8
/nplab damage off
```

For updates, run `/unload nameplatelab`, wait for the DLL to unload, replace `plugins/nameplatelab.dll`, then run `/load nameplatelab`. Settings remain in place. The plugin restores its name, damage and cursor hooks during normal unload.

If cleanup reports that the DLL was retained, exit the game before replacing it. This exceptional path prevents freeing code still reachable through an unremoved hook; normal unload does not pin or retain the DLL.

## Compatibility and verification

Keep the original Nameplate plugin unloaded. Disable BattleSight automatic checking (`/bs autocheck off`) when using NameplateLab automatic checking, so only one feature owns silent check requests. Disable BattleSight's cursor forcing before testing this cursor feature to avoid duplicate arrows; NameplateLab does not change other addons. The supported shared submission-entry detour is left unchanged; unknown changes to the guarded name/damage routines refuse compatibility checks. This is not universal addon compatibility or crash containment.

The earlier resident-engine builds had live acceptance for names, HP coloring, sizing and normal-addon coexistence. That does not establish live verification of this single-DLL version. Offline checks cover actual DLL removal and replacement with saved settings, hook restoration, failed-teardown retention, native machine-state preservation, geometry, cursor behavior and settings. The 0.6.2 live unload/replace/reload cycle passed and the 0.6.3 baseline was accepted. The user accepted 0.7.1 level display and silent automatic/visible manual checks with SimpleLog. Device-reset behavior, long-session stability and isolated performance remain unverified.

Implementation is independent; no other addon/plugin implementation is incorporated. No game assets, client binaries, local diagnostics, personal settings or generated builds are included.

The status icons and future above-name content share the existing name collection and layout. There is no extra actor scan or per-feature glyph collection. Stable client contracts are validated during setup; normal rendering retains narrow hook-ownership and live-data checks. Recognized temporary device loss retains the existing recovery behavior. No background worker or engine-update system is used.

### 0.6.3 baseline cleanup

Sizing presets read current display dimensions on demand and share that reference in the settings window. Native aspect sampling and its per-scene cache are removed. Settings are bounded at input; drawing still validates live native scales and transformed results. Entered-name and total-quad diagnostics are removed; replacement, HP, fallback and drawing-error status remain. The 0.6.2 unload lifecycle is unchanged. This removes recurring work; no measured speedup is claimed.

### Monster level behavior

Levels are learned only from check replies received while loaded and are shown on visible monster names. The level does not shift name/cursor centering or participate in HP coloring. Automatic checks select one living monster target, at most once a second when switching targets, with no automatic retries. They run only while levels and name replacement are enabled. Manual checks can refresh a known level. Values are cleared on zoning, death/despawn and unloading; nothing is written to a level database.

The implementation uses the existing font collection, geometry and draw pass. Packet callbacks maintain a fixed, identity-keyed table; drawing performs a direct atomic lookup with no lock, allocation or actor scan. Request tracking distinguishes automatic replies from manual replies, including a manual check overtaking a queued automatic request. The protocol has no request token, so attribution uses per-target request order. The user verified automatic silence and manual output on the supported server with SimpleLog.

The plugin handles check packets before default-priority addons so chat replacements such as SimpleLog receive automatic replies already blocked. Manual check replies remain available for their usual formatting.

### Standalone monster traits

NameplateLab embeds its own compact trait database and icon atlas in the DLL. No MobDB, XIUI or BattleSight installation is required for traits, and no other addon's files or implementation are loaded. Names, levels and traits share one collection/layout/draw pass. Traits do not shift the name or level and do not participate in HP coloring. Hiding traits skips their lookup and drawing. The subdued red/blue separator is thin and shorter than the name; its dimensions follow name size, not the Trait size or Level size sliders. Scent is not displayed.

Data and seven original icons come directly from [ThornyFFXI/MobDB](https://github.com/ThornyFFXI/mobdb/tree/eee7e1ad5d0a49eb667f1f88602d9fce76276330), revision `eee7e1ad5d0a49eb667f1f88602d9fce76276330`, under its [MIT license](licenses/MobDB.txt). The notice is also embedded as the DLL's `MOBDB_LICENSE` resource. This snapshot covers 245 zones; it is upstream data, not a Phoenix-specific server export. Missing monsters remain unknown rather than being labeled passive. An index override applies only when its name matches; otherwise the zone's name default is used. Database aggression does not predict level-dependent or conditional attacks.

The generated inputs are checked in, so normal builds need neither network access nor Pillow. To regenerate deliberately, clone upstream at the pinned revision and run `python tools/prepare_traits.py <checkout>` with Pillow installed. The converter reads only data/artwork, removes irrelevant fields and redundant index overrides, and never executes Lua. It produces 11,649 name entries, 498 distinct index overrides, and a 32 KiB atlas. A managed D3D8 texture is created at graphics initialization and released after safe detach; ordinary device resets retain it. If setup fails, only traits are omitted and the settings panel reports it.

### Target-only details

The independent **Only on current target** checkboxes under Monster levels and Monster traits restrict that detail to the selected enemy. Previously learned levels remain stored under the same rules as before; switching away hides the label, and retargeting reveals it without another reply. Zoning, despawn/death and unload still invalidate learned levels as before. Automatic/manual checking is unchanged.

Both filters share one current-scene target identity, read only while a target-only detail is enabled in All/HP mode. Each visible entity is already identity-checked by name collection. Other enemies skip the restricted level/trait lookup, optional glyph collection, layout and submissions. Normal names/HP and level packet tracking continue. This removes work but is not a measured FPS improvement.

### Debuffs

Details contains independent **Show enemy debuffs**, **Show debuffs on self**, **Show debuffs on party**, and **Show debuffs on alliance** toggles (default on). **Enemies: only on current target** defaults off. **Debuff icon size** (8-64, default 16) applies to every row. Icons follow the existing name size/width settings. Debuff opacity stays fixed instead of following nameplate fades; the artwork retains its own transparency. The row shares the name layout and draw pass and does not shift the name, levels, traits or HP fill. Rows appear only where the game draws a supported name; this does not force hidden self or party names to appear.

**Preview sample debuffs on target** temporarily substitutes poison, paralysis, blindness, silence and slow on your selected enemy. Enable Show enemy debuffs, select an enemy, then adjust Debuff icon size. Turn preview off to return to observed effects. Preview is not saved and never changes tracked effects.

Self and your own party use the SDK's current status lists, filtered to supported negative effects. Removed effects disappear on the next scene update. Only active members in your current zone are eligible, and status entries must match both server ID and entity index. These lists can show effects already present when the plugin loads.

The SDK exposes status lists for your own party only. For enemies and the other two alliance parties, the plugin learns successful negative effects from combat events observed while loaded, including additional effects and Dancer steps. Wear-off/removal, defeat, despawn and zoning clear observations; positive damage removes sleep and Lullaby. Refreshes update an existing effect instead of adding duplicates. Dia/Bio are inferred from successful damage results, preserving the stronger known tier. Earlier or out-of-range effects remain unknown. Unrefreshed observations disappear after five minutes; this is a stale-data cutoff, not a prediction of the actual duration. Up to 32 effects per observed entity are retained. Step levels above five use the client's fifth-level icon.

Hiding icons or selecting enemy target-only display preserves tracking. `/nplab debuffs on|off` controls enemies; friendly display has separate toggles. No combat packets are injected, changed or blocked by this feature. It supports a fixed set of 94 negative-status icons; unrecognized effects remain hidden. Server-specific result coverage, full alliance tracking and long-session behavior have not been comprehensively verified.

The SDK's status resources populate one managed 512×256 atlas during graphics initialization. No game artwork is embedded or distributed. Missing artwork omits the affected icon; an unavailable atlas leaves names and other details intact. Tracking uses bounded numeric state, a lock for packet writers, and atomic reads while drawing. Friendly rows use an 18-member numeric snapshot refreshed once per scene; alliance combat resolves player IDs through the roster. There is no additional actor scan, hook, worker, per-name allocation or native pointer cache. No isolated speedup or 40+ name capacity has been measured.

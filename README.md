# NameplateLab

An experimental Ashita 4.30 plugin for FFXI overhead names and damage numbers. Current source version: **0.9.19**, a single unloadable DLL. Finds native routines and globals at load time. Discovery is verified offline against the Phoenix, local test-client and Horizon builds. The same 0.9.7 DLL is user-confirmed working on Phoenix and Horizon; local live testing remains pending because that client cannot start.

## Features

- Native name font, colors, icons and placement, with optional enemy HP coloring.
- Player name icons kept apart from the name: whatever status icons the game shows beside a player name (seeking party, bazaar, linkshell, away, GM, mentor, new adventurer, campaign, PvP and others) are drawn just left of it, using the game's own priority, stacking, size and linkshell tint. They no longer count toward the name's centering, so the name and cursor center on the name alone. A checkbox shows or hides them.
- Independent Size and Width controls for names and damage numbers.
- **Match original 4:3** presets apply the complete correction through Width while retaining native height. Size and Width remain manually adjustable; there are no separate stretch toggles. Existing saved legacy correction settings retain their appearance until a preset or reset is selected. The damage preset is available immediately when display dimensions are available; selecting it enables damage adjustments.
- Name filtering: Native, Sharp or Smooth. Uses loaded game artwork, including XIPivot overrides.
- Opt-in native overhead cursor when an addon hides the target panel; keeps the game's arrow animation and main/subtarget colors.
- Monster levels beside the name, colored by the observed check difficulty. Unknown levels show `Lv.??` until a check or widescan reply arrives; confirmed impossible-to-gauge monsters show magenta `Lv.???`. Automatic checks are silent; manual `/check` output remains visible. Display and automatic checking have separate saved toggles, both on by default.
- Monster detection/linking icons and an aggression strip to the left of the level/name. True sight, sight, sound, magic, job ability, blood and link; true sight replaces ordinary sight. Red means aggressive, blue means passive, and unknown data stays hidden. These are database defaults, not current hostility; private-server behavior may differ.
- Distant-target enlargement (General tab): a 25-100% readability size at 20 yalms, with extra enlargement fading completely by 3 yalms. Nearby plates keep their ordinary size. Toggle with `/nplab grow on|off`.
- Scrolling XP, LP, CP and EP gains anchored to your own name, including chains (General tab). Up to three entries drift down and fade over three seconds; toggle with `/nplab xp on|off`.
- Saved settings; update in game with unload, replace the DLL, then load.
- Monster HP% after the name, colored by FFXI's HP warning bands, then distance in yalms. Both default to the current target only.
- Monster weaknesses and resistances (MobDB weapon and element damage modifiers), first right of the name: a green-barred row of weaknesses and a red-barred row of resistances, each its own Plates column. Both shown stack like HP/MP/TP (weaknesses on top); one alone sits on the name's baseline. Target only by default.
- Action names under the name while enemies ready abilities or cast, and for your own, party and alliance casts, weapon skills and job abilities. The result stays six seconds: green for success, red for interrupted, missed or resisted.
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

The profile generator reads a user-provided client without executing or modifying it. It accepts the three independently inspected client checksums listed in `tools/prepare_profile.py` and generates `src/native_signatures.h` locally (plus the local fixture profile). Game binaries, artwork and extracted contract bytes are not distributed here. These extraction coordinates are build-time inputs only: the plugin scans the loaded image once, requires unique instruction-layout matches, derives globals and validates the name/damage/cursor call relationships. It does not select a server or require one DLL per client. Unknown or ambiguous layouts refuse initialization; moving addresses alone do not. The temporary image snapshot is released after discovery; there is no per-frame scanning.

Output: `build/Release/nameplatelab.dll`. Local development tests are optional and absent from this repository; a clean checkout builds the production targets. Building does not install or run the plugin.

## Runtime setup

This is a source preview, not a packaged release. Copy `build/Release/nameplatelab.dll` to the Ashita installation's `plugins/nameplatelab.dll`. No project directory, engine DLL or manifest is needed at runtime.

If upgrading from the old resident-loader version, exit the game once before replacing it: that already-loaded DLL remains pinned even after `/unload`. Subsequent normal updates use the workflow below.

```text
/load nameplatelab
/nplab
/nplab status
```

The settings window has General and Details tabs. General contains names, cursor, target-window and damage controls. Details starts with **Preview on target and yourself**, then the **Plates** table (which details show for each category of name; see below), automatic checking, and size sliders for traits, HP%/MP/TP/distance labels, actions, debuff icons, weaknesses and resistances. Settings save under the Ashita installation's `config/nameplatelab/settings.ini`; settings from earlier versions migrate automatically.

```text
/nplab fit
/nplab size 1.2
/nplab width 0.8
/nplab icons show
/nplab icons hide
/nplab cursor on
/nplab cursor off
/nplab hidetarget on
/nplab hidetarget off
/nplab levels on
/nplab levels off
/nplab autocheck on
/nplab autocheck off
/nplab traits on
/nplab traits off
/nplab health on
/nplab health off
/nplab mp on
/nplab tp on
/nplab distance on
/nplab distance off
/nplab weakness on
/nplab weakness off
/nplab resistance on
/nplab resistance off
/nplab actions on
/nplab actions off
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

### 0.9.8 draw-path cleanup

Each name now sets blend and vertex format once and changes texture and alpha reference only when they differ between its quads, instead of repeating all four for every glyph. The native glyph submission only switches render target, viewport and projection, so these states hold for the whole name; sampler restoration stays per name because native drawing runs between names, and is skipped when the game already uses the selected filter. Consecutive quads that share texture, alpha reference and on-top setting are submitted together as one triangle list (at most 32 quads), in their original order; previously every glyph, icon and detail was its own submission. Icon expansion tables validated at setup are no longer re-read for every name. Settings are still written completely and then replace the prior file, without a forced disk flush. Discovery refuses the load cleanly if its temporary image copy cannot be allocated. `/nplab status` reports the current version. This removes recurring work; no measured speedup is claimed.

### 0.6.3 baseline cleanup

Sizing presets read current display dimensions on demand and share that reference in the settings window. Native aspect sampling and its per-scene cache are removed. Settings are bounded at input; drawing still validates live native scales and transformed results. Entered-name and total-quad diagnostics are removed; replacement, HP, fallback and drawing-error status remain. The 0.6.2 unload lifecycle is unchanged. This removes recurring work; no measured speedup is claimed.

### Monster level behavior

Levels are learned only from check replies received while loaded and are shown on visible monster names. The level does not shift name/cursor centering or participate in HP coloring. Automatic checks select one living monster target, at most once a second when switching targets, with no automatic retries. They run only while levels and name replacement are enabled. Manual checks can refresh a known level. Values are cleared on zoning, death/despawn and unloading; nothing is written to a level database.

The implementation uses the existing font collection, geometry and draw pass. Packet callbacks maintain a fixed, identity-keyed table; drawing performs a direct atomic lookup with no lock, allocation or actor scan. Request tracking distinguishes automatic replies from manual replies, including a manual check overtaking a queued automatic request. The protocol has no request token, so attribution uses per-target request order. The user verified automatic silence and manual output on the supported server with SimpleLog.

The plugin handles check packets before default-priority addons so chat replacements such as SimpleLog receive automatic replies already blocked. Manual check replies remain available for their usual formatting.

### Standalone monster traits

NameplateLab embeds its own compact trait database and icon atlas in the DLL. No MobDB, XIUI or BattleSight installation is required for traits, and no other addon's files or implementation are loaded. Names, levels and traits share one collection/layout/draw pass. Traits do not shift the name or level and do not participate in HP coloring. Hiding traits skips their lookup and drawing. The subdued red/blue separator is thin and shorter than the name; its dimensions follow name size. Scent is not displayed.

Data and seven original icons come directly from [ThornyFFXI/MobDB](https://github.com/ThornyFFXI/mobdb/tree/eee7e1ad5d0a49eb667f1f88602d9fce76276330), revision `eee7e1ad5d0a49eb667f1f88602d9fce76276330`, under its [MIT license](licenses/MobDB.txt). The notice is also embedded as the DLL's `MOBDB_LICENSE` resource. This snapshot covers 245 zones; it is upstream data, not a Phoenix-specific server export. Missing monsters remain unknown rather than being labeled passive. An index override applies only when its name matches; otherwise the zone's name default is used. Database aggression does not predict level-dependent or conditional attacks.

The generated inputs are checked in, so normal builds need neither network access nor Pillow. To regenerate deliberately, clone upstream at the pinned revision and run `python tools/prepare_traits.py <checkout>` with Pillow installed. The converter reads only data/artwork, removes irrelevant fields and redundant index overrides, and never executes Lua. It produces 11,649 name entries (with weakness/resistance masks: any modifier above 1 is a weakness, below 1 a resistance), 535 distinct index overrides, and a 96 KiB atlas (trait and modifier icons, padded to 256x128 on the GPU). A managed D3D8 texture is created at graphics initialization and released after safe detach; ordinary device resets retain it. If setup fails, only traits are omitted and the settings panel reports it.

### Target-only details

"Only on current target" is expressed in the Plates table: tick a detail in the **Target** row only. Learned levels stay stored when hidden; retargeting reveals them without another reply. The current target's identity is read once per scene. Other names skip the lookups, glyph collection and layout for details their row does not show.

### Debuffs

The Plates table's Debuffs column controls where rows appear (Target, You, Party/Alliance and each enemy claim row; all on by default). **Debuff icon size** (4-24, default 16) applies to every row. Icons follow the existing name size/width settings. Debuff opacity stays fixed instead of following nameplate fades; the artwork retains its own transparency. The row shares the name layout and draw pass and does not shift the name, levels, traits or HP fill. Rows appear only where the game draws a supported name; this does not force hidden self or party names to appear.

**Preview on target and yourself** temporarily substitutes poison, paralysis, blindness, silence and slow, plus a sample action cycling white/green/red, on your selected target (enemy or player) and on yourself, regardless of the table. Preview is not saved and never changes tracked effects.

Self and your own party use the SDK's current status lists, filtered to supported negative effects. Removed effects disappear on the next scene update. Only active members in your current zone are eligible, and status entries must match both server ID and entity index. These lists can show effects already present when the plugin loads.

The SDK exposes status lists for your own party only. For enemies and the other two alliance parties, the plugin learns successful negative effects from combat events observed while loaded, including additional effects and Dancer steps. Wear-off/removal, defeat, despawn and zoning clear observations; positive damage removes sleep and Lullaby. Refreshes update an existing effect instead of adding duplicates. Dia/Bio are inferred from successful damage results, preserving the stronger known tier. Earlier or out-of-range effects remain unknown. Supported spells use base-duration estimates for enfeebling magic, ninjutsu, songs, helix and confirmed blue-magic effects; selected job abilities and additional-effect procs also have estimates. For example, Dia I-III use 60/120/180 seconds and Poison I-III use 90/120/150 seconds. Unknown-source status events use the longest listed base spell estimate for that status, or an additional-effect estimate. These estimates do not account for caster bonuses or shortened durations. Successful spell results can supply an omitted status through their spell ID. Short additional-effect procs do not shorten a longer tracked expiry. Sleep II and Lullaby use their own icons while sharing Sleep removal. Damage-only secondary effects from weapon skills, blue magic and pets are not guessed; no-effect/resist messages do not create or refresh observations. Up to 32 effects per observed entity are retained. Step levels above five use the client's fifth-level icon.

Hiding icons preserves tracking. `/nplab debuffs on|off` switches the Debuffs column in every row. No combat packets are injected, changed or blocked by this feature. It supports a fixed set of 94 negative-status icons; unrecognized effects remain hidden. Server-specific result coverage, full alliance tracking and long-session behavior have not been comprehensively verified.

The SDK's status resources populate one managed 512×256 atlas during graphics initialization. No game artwork is embedded or distributed. Missing artwork omits the affected icon; an unavailable atlas leaves names and other details intact. Tracking uses bounded numeric state, a lock for packet writers, and atomic reads while drawing. Friendly rows use an 18-member numeric snapshot refreshed once per scene; alliance combat resolves player IDs through the roster. There is no additional actor scan, hook, worker, per-name allocation or native pointer cache. No isolated speedup or 40+ name capacity has been measured.

### HP%, distance and actions

The Plates table's HP% and Distance columns control where these appear (Target only by default). HP% follows the name's advance in softened warning colors: white, then light yellow below 75%, light orange below 50%, light red below 25%; its '%' is drawn smaller. Distance follows HP% (and MP/TP) to one decimal and hides at 0.0. They read the already validated visible entity, do not shift the name, level, traits, debuffs or cursor, are excluded from HP coloring, and follow name size plus their own size slider.

The Action column controls action rows (Target, You, Party/Alliance and enemy rows by default). The row is smaller than the name (Action size, default 60%) and overlaps its lower right corner. Readies and casts show from their start packet until a result arrives, at most 30 seconds. A result lingers six seconds; duplicate results do not extend it. Player job abilities, which have no ready phase, show their result immediately. Interruptions (including the interrupt marker), defeat, despawn and zoning end the row. Only explicit success/failure result messages color it; unknown results stay white. Names come from the client's resources once per packet, not while drawing. Non-ASCII names and names longer than 31 characters are omitted or shortened. Other players outside your party/alliance are not tracked.

Actions reuse the existing combat callback and bounded identity-keyed tracker pattern: a lock for packet writers, atomic loads while drawing, no per-name allocation, actor scan, hook or worker. All three labels use the loaded game font in the existing draw pass; missing glyphs omit only that label. No isolated speedup is claimed and in-game appearance is not yet verified.

### Plate categories, TP/MP and claims (0.9.10)

The Details tab's **Plates** table has one row per category and one column per detail (Show, HP%, TP, MP, Level, Traits, Debuffs, Action, Distance, Weak, Resist); a dash means not applicable. Each name uses the first row it matches: **You** (also while you target yourself), **Target** (your selected target, any kind), **Party/Alliance** (active members in your zone, including trusts), then monsters **Claimed by you**, **Claimed by party**, **Claimed by others** and **Unclaimed**. Other players show details only while targeted. NPCs get the name restyling but no details, even when targeted, unless **Apply features to NPCs** is on (off by default). Each row's Show switches that row. The earlier per-detail toggles and "Only on current target" options are migrated into the table once: target-only becomes Target only. Defaults keep earlier behavior (levels, traits, debuffs and actions on every monster; HP% and distance on the target) and add HP% and TP on you and your party. `/nplab <detail> on|off` switches a column in every applicable row.

TP (percent of 1000 TP, as in BattleSight; soft blue) and MP (percent, soft green) follow HP% and come from the party roster, sampled once per scene. MP appears only for your own MP pool or a member whose main or support job uses MP. Claims come from the monster's claim field, whose low word holds the claimer's server ID; it is matched against you and your roster once per name, without an actor scan. **Unclaimed: only once damaged** (off by default) skips details on unhurt unclaimed monsters unless targeted. BattleSight's 20-nearest cap is not needed: details follow the names the game already draws.

### Party HP depletion and softer HP colors (0.9.11)

**Deplete your and party/alliance names by HP** (Details > Plates, off by default) dims the lost part of your own and party/alliance names like monster HP. Below 75% the remaining letters take the HP% band color; at 75% and above they keep the native name color. HP% bands are softened: light yellow below 75%, light orange below 50%, light red below 25%. TP now reads as a percent (TP / 10), matching BattleSight.

### Widescan levels and hiding the target window (0.9.13)

Widescan results supply monster levels by target index, shown in neutral white because widescan reports no difficulty; a check reply for the same monster replaces it with its difficulty color. Despawn and zoning clear widescan levels, as they do check results.

**Hide the game's target window** (General > Target cursor, `/nplab hidetarget on|off`, off by default) hides the window without HideParty. Turning it on also turns on Keep native overhead cursor. Before each scene the plugin clears the window's frame and draw-callback visibility bytes, after any Present-time writer such as HideParty. Turning it off or unloading restores exactly the values found, only through the same validated window and only over its own hidden state, so a later writer's choice is never overwritten.

# Overhead

An Ashita v4 plugin that restyles Final Fantasy XI's overhead names and adds combat details to them: monster levels and check difficulty, HP, TP and MP, distance, aggro type, weaknesses and resistances, debuffs, and the action being readied or cast.

It draws with the game's own name placement, colors and fading, so names still sit where the game puts them. You can turn each detail on or off separately for your target, yourself, your party and monsters.

Tested on Phoenix and HorizonXI with Ashita 4.30. On a client it doesn't recognize, the plugin refuses to load and leaves the game untouched.

## Features

- **Custom lettering**: any installed Windows font with a black outline, or the game's own font. Size, width, italic, outline thickness and edge softness are all adjustable, and there's a one-click fix for widescreen stretch.
- **Monster levels**: the level appears next to the name, colored by check difficulty. Levels are checked silently in the background, and widescan fills them in too. Impossible-to-gauge monsters get a magenta `NM` tag.
- **HP on the name**: lost HP dims the name, and HP% follows it in warning colors. TP, MP and distance can be shown too.
- **Aggro icons**: sight, sound, magic, true sight, job ability, blood and link. Red means aggressive and blue means passive.
- **Weaknesses and resistances**: weapon and element modifiers from MobDB.
- **Debuffs**: an icon row above monsters, you and your party.
- **Actions**: TP moves and spells being readied show under the name, then turn green or red depending on the result.
- **Player icons**: status icons move to the side of the name so it stays centered. The linkshell and bazaar icons sit on the name's corners.
- **Target helpers**: the far-away target is enlarged, the target window can be hidden, and the target arrow stays above the name.
- **Damage numbers**: resize them separately from names.
- **Experience points**: XP, LP, CP and EP gains scroll up from your own name.

## Install

1. Download `overhead.dll` from the [latest release](../../releases/latest).
2. Put it in your Ashita `plugins` folder.
3. In game, run `/load overhead`. To load it every time, add `/load overhead` to your Ashita startup script (for example `scripts/default.txt`).
4. Run `/overhead` to open the settings.

Settings save to `config/overhead/settings.ini` in your Ashita folder.

**To update**, run `/unload overhead`, replace the DLL, then run `/load overhead`. Your settings are kept. If an unload ever reports that the DLL was retained, close the game before replacing it.

## Settings

`/overhead` opens a window with five tabs. Hover over any option to see what it does.

- **Names**: which names get restyled (Off, Game font or Custom font), size and width, the widescreen fix, target options and player icons.
- **Font**: font family, italic, outline, edge softness and letter scaling. Click **Apply font** to use your choices.
- **Details**: a table of which details show on each kind of name (You, Target, Party/Alliance, the monster claim states and Other players), plus automatic level checks.
- **Detail style**: preview, icon and text sizes, and which parts draw in front of scenery.
- **Combat**: damage number size and the points-gained display.

## Commands

| Command | What it does |
|---|---|
| `/overhead` | Open or close the settings window |
| `/overhead status` | Show version and status |
| `/overhead all` / `game` / `off` | Custom font, game font, or original names |
| `/overhead hp` | Custom font with HP dimming on enemies |
| `/overhead size <n>` / `width <n>` | Name size and width (0.25 to 3) |
| `/overhead fit` | Fix widescreen stretch |
| `/overhead reset` | Reset size and width |
| `/overhead icons show\|hide` | Player status icons |
| `/overhead levels\|traits\|health\|mp\|tp\|distance\|weakness\|resistance\|debuffs\|actions on\|off` | Turn a detail on or off for every kind of name |
| `/overhead autocheck on\|off` | Silent automatic level checks |
| `/overhead grow on\|off` | Enlarge far-away target |
| `/overhead hidetarget on\|off` | Hide the game's target window |
| `/overhead cursor on\|off` | Keep the arrow above your target |
| `/overhead xp on\|off` | Show points gained at your name |
| `/overhead damage on\|off\|fit\|retry` | Damage number resizing |
| `/overhead damage size <n>` / `width <n>` | Damage number size and width |

## Compatibility

- Unload the original **Nameplate** plugin and **FontProof**.
- If you use **BattleSight**, turn off its automatic checking (`/bs autocheck off`) and its cursor forcing, so the two don't overlap.
- Chat addons such as SimpleLog work normally. Automatic checks stay silent, and your own `/check` still shows in chat.

## Notes

- Levels come from checks and widescan while the plugin is loaded. Aggro, weakness and resistance data comes from MobDB and may differ on private servers.
- Enemy debuffs are learned from combat messages seen while the plugin is loaded. Durations are estimates based on each spell's base duration.
- No game files are included or changed. Status and name icons come from your own client, so icon packs such as XIPivot work.

## Building from source

This needs Windows, Visual Studio 2022 with C++ tools and the Windows SDK, CMake and Python 3. Build as Win32.

```powershell
.\prepare-sdk.ps1
python -m venv .venv
.\.venv\Scripts\python -m pip install capstone==5.0.7 pefile==2024.8.26
.\.venv\Scripts\python tools/prepare_profile.py 'C:\Path\To\FINAL FANTASY XI\FFXiMain.dll'
.\build.ps1
```

The build writes `build/Release/overhead.dll`. `prepare-sdk.ps1` downloads the Ashita SDK headers pinned to commit `4171c74c8ddb2ca2a31654f199e6c1cee40d7256`. `prepare_profile.py` reads your own `FFXiMain.dll` without running or changing it, and generates local build inputs. At load time, the plugin finds the game routines it needs in the running client, so one DLL works across the supported clients.

## Credits

Monster trait data and seven trait icons come from [ThornyFFXI/MobDB](https://github.com/ThornyFFXI/mobdb/tree/eee7e1ad5d0a49eb667f1f88602d9fce76276330) under the [MIT license](licenses/MobDB.txt). The DLL also embeds that license notice.

## License

Overhead is released under the [MIT license](LICENSE).

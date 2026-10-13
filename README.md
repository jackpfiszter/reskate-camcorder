# ReSkate Camcorder

**Film your own 90s skate part in skate.** A Sony VX1000 with a fisheye, right in the game: chase
your friends with the camera, set up tripods at the spot, press REC, and get MP4 clips that look
like they came off a MiniDV tape.

https://github.com/user-attachments/assets/0f3513a0-e78b-4346-97ff-7b38b3346e1e

<sub>Filmed in game with the camcorder (an earlier version: the on-screen look has come on
since).</sub>

> **Early days.** It works and it's being tested. Expect rough edges, and please
> [tell us](https://github.com/jackpfiszter/reskate-camcorder/issues) what you find.

## What you get

- 📹 **The VX1000 and the Century fisheye.** Real zoom, the round black corners at full wide, and
  straight lines bending like through the real glass.
- 🛹 **Film handheld while you skate.** Low for the classic follow shot or up high, raised and
  lowered as you go, or fixed to your board as a board cam. It keeps your friend in frame for you,
  a beat late like a real filmer, and you nudge the framing and zoom without stopping. Or stop and
  take full control of the camera, roll and all.
- 🔭 **Tripods.** Set up to four at a spot, aim them, and cut between angles.
- 📼 **The MiniDV look.** Soft 480-line picture, hard whites, colour bleed, and a date stamp set to
  any day you like.
- 🔊 **The game's sound in your clips,** heard from where the camera stands. Nothing else from your
  PC gets in.
- 💾 **Clips straight to your Videos folder** as MP4s, ready to edit. Or join a whole "tape" into
  one file.
- 🧩 **Make your own cameras.** Hi8, a worn VHS camcorder, Super 8, a 16:9 HVX200:
  every camera is a small mod anyone can make. Its picture is built from 34 effects (VHS tracking,
  film dust and light leaks, bloom, colour grades from a `.cube`...), and its viewfinder and
  picture shape are its own. A few come with it.

## Try it

You need skate. on Steam and
[ReSkate](https://github.com/Dingo-Shenanigans/ReSkate) already working.

1. **Download** the newest `ReSkate-camcorder-…zip` from
   [Releases](https://github.com/jackpfiszter/reskate-camcorder/releases/latest).
2. **Swap two files.** Close the game. In your skate. folder (beside `Skate.exe`), back up
   `ReSkate.dll` and `ReSkateLauncher.exe`, then put the two from the zip there.
3. **Play.** Start the game with that `ReSkateLauncher.exe`. If it offers a ReSkate update, skip it:
   updating puts the official `ReSkate.dll` back (no camcorder). Load a map, press **Insert** for
   the ReSkate menu, open **CAMCORDER** and click **SET UP KEYS**.

Now **F5** turns the camera on and **F6** records. To go back to normal ReSkate, put your two
backed-up files back.

Each release is ReSkate's newest code at the time with the camcorder added, and nothing else of
ReSkate's changed (the release notes say which ReSkate commit). When ReSkate puts out something
newer, the next camcorder release catches up.

The extra cameras are a normal ReSkate mod: unzip `Camcorder-Extras-mod-…zip` from the same
release into your `Mods` folder.

## Controls

| | Keyboard and mouse | Controller |
|---|---|---|
| Camera on / off | F5 | *bind it in Settings > Controls* |
| Record / stop | F6 | |
| Quick menu (camera, lens, look, handheld / board cam, camera control, tripods) | F7 | |
| Take a still | F8 | |
| Full camera control (the skater waits), or film while skating | F9 | |
| Set up a tripod, then done | F10, F10 again | |
| Aim (nudge the framing; set up a tripod) | Mouse | Right stick |
| Zoom | Q / E, or the mouse wheel | Triggers |
| Camera higher / lower | R / F | Bumpers |
| Roll the frame (full camera control) | A / D | Left stick |
| Back out of the quick menu, a tripod's setup or full camera control | Esc | Start |

The F keys are what **SET UP KEYS** gives you. Change any of them in ReSkate's Settings > Controls.
Aim, zoom and height work the same handheld, on the board cam and setting up a tripod, and while
you skate: while you film handheld those keys and sticks are the camera's, not the skater's. Next
tripod is in the quick menu, or bind it.

## Your clips

Everything lands in **Videos\ReSkate Camcorder** (stills in its **Stills** folder). **OPEN MY CLIPS**
on the CAMCORDER page takes you there.

## Make your own camera

A camera is a few lines of JSON in an ordinary ReSkate mod: zoom, frame rate, picture shape, its
picture as a stack of effects, its viewfinder, its date stamp. [docs/MODDING.md](docs/MODDING.md) explains every setting, and
[examples/mods/Camcorder Extras](examples/mods/Camcorder%20Extras) is one to copy.

## Help and feedback

- Something broken or weird? Open an [issue](https://github.com/jackpfiszter/reskate-camcorder/issues)
  with what you did and, if you can, `logs\ReSkate.log` from beside `Skate.exe`.
- [docs/TESTING.md](docs/TESTING.md) has the full checklist of what should work, and what's known
  not to yet.
- Playing online: your friends don't need the camcorder to be filmed, and nothing extra goes over
  the network: your camera is on your screen only.

<details>
<summary><b>For developers</b></summary>

### Why it's part of ReSkate, not a mod

ReSkate mods can only change game data. Moving the camera every frame takes native code inside
`ReSkate.dll`, the way ReSkate's Trainer and Hall of Meat are built. So the camcorder is a change to
ReSkate (`integration/reskate/reskate.patch`, plus its own files), meant to be offered to ReSkate
itself. Everything about a particular camera is data, so cameras can still change through mods
with no ReSkate update.

### What it changes in ReSkate

Its own code lives in `Extension/Camcorder/` (copied in by `install.ps1`, built by its own
`camcorder.cmake`). `reskate.patch` is what touches ReSkate's files, against the commit in
`integration/reskate/reskate.commit`: about 280 lines added across 15 files, and nothing of
ReSkate's taken out (every line it replaces comes back extended).

| Where | What for |
|---|---|
| `cmake/Runtime.cmake` | one line: include `camcorder.cmake` |
| `Runtime/client_tick.cpp` | the camcorder's tick after Spectate's; holding the pad while it needs it; skate.'s HUD off while filming |
| `Extension/Multiplayer/Session/session.cpp` | where the skaters are (the local one, with their board a few times a second, and the others) |
| `Extension/Multiplayer/Hud/native_party*` | whether Spectate has the camera (they share it, never both) |
| `Extension/UI/Overlay/input_capture.cpp`, `overlay_input.cpp`, `overlay_internal.h`, `overlay.h` | filming while skating: the mouse, right stick, triggers, bumpers and Q / E / R / F to the camera, the rest to the skater; mouse look and wheel for the camera |
| `Extension/UI/Overlay/overlay_render.cpp` | two descriptor slots, and the picture, clip and review hooks around the overlay's draw |
| `Extension/UI/Overlay/skate_menu.cpp` | the CAMCORDER page |
| `Extension/Console/commands.*`, `Engine/Game/Input/controller_bindings.h` | the `camcorder` commands and seven bindable actions |
| `Extension/Multiplayer/Voice/voice_chat.cpp` | push-to-talk read past the input hold (it was blanked while Freecam held the keys too) |

The cursor hooks also stop swallowing the game's own cursor hiding while the input is held, so
Windows' arrow no longer shows over a held picture (Freecam's too).

### Layout

| Path | What |
|---|---|
| `src/camcorder/` | the model: cameras, lenses, handheld and board cam, tripods, tape, look, sound. No game types; builds anywhere |
| `tests/` | its unit tests |
| `integration/reskate/` | the ReSkate side: its files, the patch, and `install.ps1` |
| `docs/DESIGN.md` | the design |
| `docs/TESTING.md` | testing: unit tests, builds, the in-game checklist |
| `docs/MODDING.md` | camera, lens and handling files |
| `examples/mods/` | the example camera mod |

### Build and test the model

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

C++20, warning-free with `-Wall -Wextra -Wpedantic -Werror` (GCC, Clang) and `/W4 /WX` (MSVC, as
ReSkate builds). CI builds ReSkate with the camcorder on Windows for every push, and compiles the
picture shader with fxc. Building ReSkate yourself is in [docs/TESTING.md](docs/TESTING.md).

### Console commands

Every action is a console command (`~` in game): `camcorder power|rec|stop|still|quick|mode|camera|lens|rig|operate|lockon|subject|frame|tripod|head|height|tape|clean|sound|look|dv|date|sensitivity|invert|guide|open|review|mods|effects|export|status`.
Settings are kept in `%LOCALAPPDATA%\ReSkate Camcorder\settings.txt`.

### Releases

Each version on the [Releases page](https://github.com/jackpfiszter/reskate-camcorder/releases) is a
zip with `ReSkate.dll` and `ReSkateLauncher.exe`, plus the example camera mod. To publish the newest
green build: Actions > **release** > Run workflow, with a version like `v0.19.0`. The zip holds a
modified `ReSkate.dll`, so under the GPL its source is this repository at that version plus
ReSkate at the commit in `integration/reskate/reskate.commit`.

</details>

## License

GPL-3.0, the same as ReSkate. A fan project: not affiliated with or endorsed by Electronic Arts,
Full Circle, Sony or Century Optics.

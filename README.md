# ReSkate Camcorder

**Film your own 90s skate part in skate.** A Sony VX1000 with a fisheye, right in the game: chase
your friends with the camera, set up tripods at the spot, press REC, and get MP4 clips that look
like they came off a MiniDV tape.

https://github.com/user-attachments/assets/0f3513a0-e78b-4346-97ff-7b38b3346e1e

<sub>Filmed in game with the camcorder (an earlier version: the on-screen look and the camera light
have come on since).</sub>

> **Early days.** It works and it's being tested. Expect rough edges, and please
> [tell us](https://github.com/jackpfiszter/reskate-camcorder/issues) what you find.

## What you get

- 📹 **The VX1000 and the Century fisheye.** Real zoom, the round black corners at full wide, and
  straight lines bending like through the real glass.
- 🛹 **Film handheld while you skate.** Low on a long grip for the classic follow shot, at the hip,
  or at eye level. It keeps your friend in frame for you, a beat late like a real filmer.
- 🔭 **Tripods.** Set up to four at a spot, aim them, and cut between angles.
- 📼 **The MiniDV look.** Soft 480-line picture, hard whites, colour bleed, a date stamp set to any
  day you like, and a camera light for night sessions.
- 🔊 **The game's sound in your clips,** heard from where the camera stands. Nothing else from your
  PC gets in.
- 💾 **Clips straight to your Videos folder** as MP4s, ready to edit. Or join a whole "tape" into
  one file.
- 🧩 **Make your own cameras.** Hi8, Super 8, a VX2000, a pole cam: every camera is a small mod
  anyone can make. A few come with it.

## Try it

You need skate. on Steam and
[ReSkate](https://github.com/Dingo-Shenanigans/ReSkate) already working.

1. **Download** the newest `ReSkate-camcorder-…zip` from
   [Releases](https://github.com/jackpfiszter/reskate-camcorder/releases/latest).
2. **Swap two files.** Close the game. In your skate. folder (beside `Skate.exe`), back up
   `ReSkate.dll` and `ReSkateLauncher.exe`, then put the two from the zip there.
3. **Play.** Start the game with that `ReSkateLauncher.exe` (if it says an update is available,
   ignore it). Load a map, press **Insert** for the ReSkate menu, open **CAMCORDER**
   and click **SET UP KEYS**.

Now **F5** turns the camera on and **F6** records. To go back to normal ReSkate, put your two
backed-up files back.

The extra cameras are a normal ReSkate mod: unzip `Camcorder-Extras-mod-…zip` from the same
release into your `Mods` folder.

## Controls

| | Keyboard and mouse | Controller |
|---|---|---|
| Camera on / off | F5 | *bind it in Settings > Controls* |
| Record / stop | F6 | |
| Quick menu (camera, lens, look, light, height, tripod) | F7 | |
| Take a still | F8 | |
| Aim while filming (you keep skating) | F9 | |
| Set up a tripod, then done | F10, F10 again | |
| Aim | Mouse | Right stick |
| Zoom | Mouse wheel (Q / E with camera controls) | Triggers |
| Camera higher / lower | R / F (with camera controls) | Bumpers |
| Back out of a menu or camera controls | Esc | Start |

The F keys are what **SET UP KEYS** gives you. Change any of them in ReSkate's Settings > Controls.

## Your clips

Everything lands in **Videos\ReSkate Camcorder** (stills in its **Stills** folder). **OPEN MY CLIPS**
on the CAMCORDER page takes you there.

## Make your own camera

A camera is a few lines of JSON in an ordinary ReSkate mod: zoom, frame rate, how its picture
looks, its date stamp, its mic. [docs/MODDING.md](docs/MODDING.md) explains every setting, and
[examples/mods/Camcorder Extras](examples/mods/Camcorder%20Extras) is one to copy.

## Help and feedback

- Something broken or weird? Open an [issue](https://github.com/jackpfiszter/reskate-camcorder/issues)
  with what you did and, if you can, `logs\ReSkate.log` from beside `Skate.exe`.
- [docs/TESTING.md](docs/TESTING.md) has the full checklist of what should work, and what's known
  not to yet.
- Playing online: your friends don't need the camcorder to be filmed. Friends on the same build
  see a little camcorder by your name, with a red light while you're recording.

<details>
<summary><b>For developers</b></summary>

### Why it's part of ReSkate, not a mod

ReSkate mods can only change game data. Moving the camera every frame takes native code inside
`ReSkate.dll`, the way ReSkate's Trainer and Hall of Meat are built. So the camcorder is a change to
ReSkate (`integration/reskate/reskate.patch`, plus its own files), meant to be offered to ReSkate
itself. Everything about a particular camera is data, so cameras can still change through mods
with no ReSkate update.

### Layout

| Path | What |
|---|---|
| `src/camcorder/` | the model: cameras, lenses, rigs, tape, look, sound. No game types; builds anywhere |
| `tests/` | its unit tests |
| `integration/reskate/` | the ReSkate side: its files, the patch, and `install.ps1` |
| `docs/DESIGN.md` | the design |
| `docs/TESTING.md` | testing: unit tests, builds, the in-game checklist |
| `docs/MODDING.md` | camera, lens, rig and handling files |
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

Every action is a console command (`~` in game): `camcorder power|rec|stop|still|quick|mode|camera|lens|warp|rig|lockon|subject|operate|aim|tripod|head|height|tape|clean|quality|sound|look|dv|date|light|sensitivity|invert|guide|open|review|mods|effects|export|status`.
Settings are kept in `%LOCALAPPDATA%\ReSkate Camcorder\settings.txt`.

### Releases

Each version on the [Releases page](https://github.com/jackpfiszter/reskate-camcorder/releases) is a
zip with `ReSkate.dll` and `ReSkateLauncher.exe`, plus the example camera mod. To publish the newest
green build: Actions > **release** > Run workflow, with a version like `v0.12.0`. The zip holds a
modified `ReSkate.dll`, so under the GPL its source is this repository at that version plus
ReSkate at the commit in `integration/reskate/reskate.commit`.

</details>

## License

GPL-3.0, the same as ReSkate. A fan project: not affiliated with or endorsed by Electronic Arts,
Full Circle, Sony or Century Optics.

# ReSkate Camcorder

A camcorder for [ReSkate](https://github.com/Dingo-Shenanigans/ReSkate) (skate.): film yourself
and your friends the way 90s skate videos were shot, without the replay editor. Hold a Sony
VX1000 with a Century fisheye and chase the skater, or set up tripods at the spot, press REC,
and skate the line.

> **Status: early, in testing.** Filming, recording to MP4 and sound work in game. The
> camcorder's model (camera, lens, zoom, tape, rigs, viewfinder, commands) is unit-tested, and
> CI builds ReSkate with the camcorder on Windows (MSVC, warnings as errors); tagged versions
> are on the Releases page. [docs/TESTING.md](docs/TESTING.md) has how to try it and a checklist. See
> [docs/DESIGN.md](docs/DESIGN.md) for the design and
> [integration/reskate/README.md](integration/reskate/README.md) for how it plugs in.

## What it does

- **The VX1000, by the numbers.** A 10:1 zoom (5.9-59 mm on a 1/3" sensor, about 44 to 4.7
  degrees across), the servo's real speeds (4.1 s end to end flat out, 20 s creeping), a 4:3
  picture, and the Century Optics 0.3x fisheye: about 125 degrees, straight lines bending as
  through the real glass, with round black corners at full wide that clear with a touch of zoom.
- **Handheld.** You are the filmer; the camera rides with your skater on a long grip (low, the
  fisheye follow shot), at the hip or at eye level. Lock on to a friend and the camera keeps
  them framed by itself, a little late like a real operator, so you can keep both hands on
  skating, or steer while filming: skate on the keys or left stick, aim with the mouse or right
  stick. Hand shake comes from breathing, the board's wheels and, on foot, footsteps.
- **Tripods.** Set up to four at a spot: carry each over the ground (it stands on what's under
  it), set the height (ground pod
  to 2 m) and aim, then lock it off, pan it by hand against a fluid head's drag, or let it follow
  the skater. Cut between them live.
- **The viewfinder.** STBY / red REC, the tape counter, minutes of tape left, battery, the W-T
  zoom bar; 4:3 bars either side. A clean feed keeps the frame and lens for screen capture.
- **Clips to MP4.** REC to STBY writes the 4:3 picture (fisheye corners and all, readouts not) to
  Videos\ReSkate Camcorder, at 29.97 frames a second, in HD or the VX1000's own 640 x 480, with
  the game's own sound (nothing else on the PC), clean or through the VX mic. Stills go to
  Pictures\ReSkate Camcorder as JPEGs.
- **The tape look.** Optionally, clips and stills look like MiniDV: 60 fields of video motion,
  a CCD's hard white, drifting colour, sharpened edges, smeared colour, and the camcorder's date
  stamp (any year you like).
- **More cameras and lenses, from mods.** Cameras and lenses are small JSON files in an ordinary
  ReSkate mod: zoom, sensor, frame rate (60i, 50i, 30p, 25p, 24p, Super 8's 18), tape length, the
  look and the date stamp. `examples/mods/Camcorder Extras` adds a VX2000, a Hi8 handycam, Super 8
  film and a wide converter; [docs/MODDING.md](docs/MODDING.md) has every field.
- **The tape.** 29.97 drop-frame timecode, a 60-minute MiniDV cassette that runs out, and a
  numbered clip list per tape (`camcorder tape clips`). Capture the tape to get all its clips
  back to back in one MP4, ready to edit.

## Why it is a ReSkate feature, not a drop-in mod

ReSkate mods (`Mods/`) can only change game data, and custom Lua is switched off. Moving the
camera every frame takes native code inside `ReSkate.dll`, the way the Trainer and Hall of Meat
are built. So this is meant to go into ReSkate as a pull request (or a fork). It reuses
ReSkate's party-Spectate camera lease, Freecam's input capture, the session's player poses and
the overlay.

## Layout

| Path | What |
|---|---|
| `src/camcorder/` | the model: no game types, builds anywhere |
| `tests/` | its unit tests |
| `integration/reskate/` | the ReSkate side: its files, the patch to ReSkate and `install.ps1` |
| `docs/DESIGN.md` | the design: shooting modes, recording, the look, controls, milestones |
| `docs/TESTING.md` | how to test: unit tests, getting a build, an in-game checklist |
| `docs/MODDING.md` | making cameras and lenses |
| `examples/mods/` | an example camera mod |

## Build and test the model

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

C++20; builds warning-free with `-Wall -Wextra -Wpedantic -Werror` (GCC and Clang) and `/W4 /WX`
is set for MSVC, as ReSkate uses.

## Console commands

Every action is a console command (`~` in game), so players can bind them to buttons:
`camcorder power|rec|stop|still|mode|camera|lens|warp|grip|lockon|subject|operate|aim|tripod|head|height|tape|clean|quality|sound|look|dv|date|mods|export|status`.
Settings are kept between games in `%LOCALAPPDATA%\ReSkate Camcorder\settings.txt`.
See [docs/DESIGN.md](docs/DESIGN.md#controls-proposed).

## Releases

Every version is on the [Releases page](https://github.com/jackpfiszter/reskate-camcorder/releases):
a zip with `ReSkate.dll` and `ReSkateLauncher.exe`, and from v0.7.0 the example camera mod beside
it. To publish the newest green build: Actions > **release** > Run workflow, with a version like
`v0.7.0` (pushing a tag like that works too). The zip holds a modified `ReSkate.dll`, so under the
GPL its source is this repository at that version plus ReSkate at the commit in
`integration/reskate/reskate.commit`.

## License

GPL-3.0, the same as ReSkate. Not affiliated with or endorsed by Electronic Arts, Full Circle,
Sony or Century Optics.

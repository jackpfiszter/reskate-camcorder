# ReSkate Camcorder

A camcorder for [ReSkate](https://github.com/Dingo-Shenanigans/ReSkate) (skate.): film yourself
and your friends the way 90s skate videos were shot, without the replay editor. Hold a Sony
VX1000 with a Century fisheye and chase the skater, or set up tripods at the spot, press REC,
and skate the line.

> **Status: early.** The camcorder's model (camera, lens, zoom, tape, rigs, viewfinder,
> commands) is built and unit-tested. The code that wires it into ReSkate is written but **not
> yet compiled or tried in the game**. See [docs/DESIGN.md](docs/DESIGN.md) for the full
> design and [integration/reskate/README.md](integration/reskate/README.md) for how it plugs in.

## What it does

- **The VX1000, by the numbers.** A 10:1 zoom (5.9-59 mm on a 1/3" sensor, about 44 to 4.7
  degrees across), the servo's real speeds (4.1 s end to end flat out, 20 s creeping), a 4:3
  picture, and the Century Optics 0.3x fisheye: about 125 degrees, with round black corners at
  full wide that clear with a touch of zoom.
- **Handheld.** You are the filmer; the camera rides with your skater on a long grip (low, the
  fisheye follow shot), at the hip or at eye level. Lock on to a friend and the camera keeps
  them framed by itself, a little late like a real operator, so you can keep both hands on
  skating. Hand shake comes from breathing, the board's wheels and footsteps.
- **Tripods.** Set up to four at a spot: carry each over the ground, set the height (ground pod
  to 2 m) and aim, then lock it off, pan it by hand against a fluid head's drag, or let it follow
  the skater. Cut between them live.
- **The viewfinder.** STBY / red REC, the tape counter, minutes of tape left, battery, the W-T
  zoom bar; 4:3 bars either side. A clean feed keeps the frame and lens for screen capture.
- **The tape.** 29.97 drop-frame timecode, a 60-minute MiniDV cassette that runs out, and a
  numbered clip list per tape (`camcorder tape clips`).

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
| `integration/reskate/` | the ReSkate side (reference, untested) and where it plugs in |
| `docs/DESIGN.md` | the design: shooting modes, recording, the look, controls, milestones |
| `docs/TESTING.md` | how to test: unit tests, getting a build, an in-game checklist |

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
`camcorder power|rec|stop|mode|lens|grip|lockon|subject|operate|tripod|head|height|tape|clean|status`.
See [docs/DESIGN.md](docs/DESIGN.md#controls-proposed).

## License

GPL-3.0, the same as ReSkate. Not affiliated with or endorsed by Electronic Arts, Full Circle,
Sony or Century Optics.

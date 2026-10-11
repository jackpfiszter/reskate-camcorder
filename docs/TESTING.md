# Testing the camcorder

## 1. The model (any computer, no game)

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

This covers the camera behaviour: lens angles, zoom speeds, timecode, the rigs, the viewfinder
layout and the console commands. CI runs it on every push.

## 2. Get a ReSkate build with the camcorder

Every push builds ReSkate with the camcorder on a Windows runner (`.github/workflows/build.yml`).
Open the repository's **Actions** tab, pick the latest green **build** run, and download the
**ReSkate-camcorder** artifact: `ReSkate.dll` and `ReSkateLauncher.exe`. Crash uploads are switched
off in these builds, because a crash in them is ours, not the ReSkate developers'.

To build it yourself instead (Windows, Visual Studio 2022 with C++, CMake 3.24+):

```powershell
git clone https://github.com/Dingo-Shenanigans/ReSkate C:\src\ReSkate
git -C C:\src\ReSkate checkout (Get-Content integration\reskate\reskate.commit)
pwsh integration\reskate\install.ps1 -ReSkate C:\src\ReSkate
cd C:\src\ReSkate
cmake --preset vs2022-x64 "-DDINGOSDK_BACKTRACE_URL="
cmake --build --preset release --parallel 4 --target dingosdk_runtime dingosdk_launcher
```

## 3. In the game

1. Get stock ReSkate working first (its README: the supported skate. build, the launcher).
2. Close the game. Beside `Skate.exe`, back up `ReSkate.dll` and `ReSkateLauncher.exe`, then put
   the camcorder build's two files there.
3. Run `ReSkateLauncher.exe`. A local build never replaces itself; it may say an update exists.
   Ignore that (or start it with `--no-update`).
4. Load a map, open the console with **~**, and work through the checklist below.
5. To go back, restore the two backed-up files.

Logs are in `logs\ReSkate.log` beside `Skate.exe` (`-wconsole` shows them live). Anything the
camcorder says goes there too.

Multiplayer: everyone in a session should run the same build. Other players don't need the
camcorder to be filmed, but a different ReSkate build may not be allowed to join.

### Checklist

Solo:

- [ ] `camcorder power on`: the view jumps to a low, fisheye-ish handheld shot with black bars
      either side, STBY, `0:00:00` and `60min` in the corners, round black corners at full wide.
- [ ] Skate around: the camera rides with you, shakes a little, never jumps or spins.
- [ ] `camcorder operate on`: the pad drives the camera. Right stick aims (pushing right turns
      right), triggers zoom (left wide, right tele), the W-T bar shows while zooming, and the round
      corners vanish after a little zoom. `camcorder operate off` gives the pad back.
- [ ] `camcorder lens stock`: much narrower (about 44 degrees), no round corners.
- [ ] `camcorder grip eye`, `camcorder grip hip`, `camcorder grip long`: the camera height changes.
- [ ] `camcorder rec`: red dot and REC, counter running. `camcorder rec` again: STBY.
      `camcorder tape clips` lists the clip.
- [ ] `camcorder tripod place`: the view jumps to a tripod just ahead of you, in SETUP with a faint
      frame. Left stick moves it, right stick aims, triggers raise and lower it. Does it stand on
      the ground? (Known gap: it doesn't follow the ground yet; see below.)
- [ ] `camcorder rec`: setup ends. Skate away: the tripod pans after you.
      `camcorder head locked`: it stops panning. `camcorder head manual` + `camcorder operate on`:
      pan by hand, with drag.
- [ ] Place a second tripod somewhere else; `camcorder tripod next` cuts between them.
- [ ] `camcorder power off`: your normal camera comes back. Open and close the menu, load another
      map, bail: nothing sticks or crashes.
- [ ] Bind buttons: Controls in the ReSkate menu lists "Camcorder on / off", "Camcorder REC / STBY",
      "Camcorder: pad to camera", "Camcorder: set up a tripod" and "Camcorder: next tripod".

With a friend:

- [ ] `camcorder subject next` (or `camcorder subject <name>`): lock on follows them handheld while
      you skate after them.
- [ ] A tripod with a subject follows them, not you.
- [ ] Party Spectate while the camcorder is on: the camcorder turns off ("stop spectating to film"),
      and Spectate works.

### Known gaps (expected)

- Tripods keep the height they were placed at; they don't follow the ground yet.
- Hand shake always uses the on-board kind (wheel buzz), even on foot.
- The fisheye is the wide angle plus round corners; straight lines don't bend yet.
- Nothing is saved as a video file: capture the screen (OBS, the GPU's recorder).
  `camcorder clean on` hides the readouts and keeps the frame and lens.

### Worth reporting

How the aim speed feels on a pad and with a mouse, whether the lock-on lag feels like a person,
what the game draws at full tele (about 3.5 degrees tall) and full wide, and anything in
`ReSkate.log` with "camcorder", "Spectate" or "camera" in it.

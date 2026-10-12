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

Tagged versions are on the repository's **Releases** page: a zip with `ReSkate.dll`,
`ReSkateLauncher.exe` and this file (pushing a tag like `v0.3.0` makes one).

Every push builds ReSkate with the camcorder on a Windows runner (`.github/workflows/build.yml`).
For the newest build, open the repository's **Actions** tab, pick the latest green **build** run, and download the
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
   Ignore that (or start it with `--no-update`). Use the camcorder build's launcher, not the
   official one: the official launcher puts the released `ReSkate.dll` back on every launch
   (unless its Settings > **Install ReSkate updates** is off).

The camcorder can't come from the launcher's MODS page: mods there are game data only, and
ReSkate never runs code from a mod. Until the camcorder is part of ReSkate itself, these two
files are how to get it.
4. Load a map, open the console with **~**, and work through the checklist below.
5. To go back, restore the two backed-up files.

Logs are in `logs\ReSkate.log` beside `Skate.exe` (`-wconsole` shows them live). Anything the
camcorder says goes there too.

Multiplayer: everyone in a session should run the same build. Other players don't need the
camcorder to be filmed, but a different ReSkate build may not be allowed to join.

### Controls

Everything is on the **CAMCORDER** page of the ReSkate menu (Insert): on / off, REC, handheld or
tripod, lens, grip, camera controls, who to film, tripods, tape. Keys and buttons can be bound in
SETTINGS > CONTROLS. While the camcorder is on, skate.'s own HUD is hidden.

**Camera controls** (the menu's toggle, or `camcorder operate on`), or setting up a tripod, give the
mouse, keys and pad to the camera; the skater stands still until you turn them off:

| | Mouse and keyboard | Pad |
|---|---|---|
| Aim | move the mouse (no button needed) | right stick |
| Zoom (wide / tele) | Q / E | left / right trigger |
| Hold the camera higher / lower | R / F | RB / LB |
| Tripod setup: carry it | W A S D | left stick |
| Tripod setup: lower / raise it | Q / E | left / right trigger |
| Tripod setup: zoom instead | hold Shift + Q / E | hold L3 + triggers |

The viewfinder shows a line of these hints while they apply.

**Steer while filming** (the menu's toggle, `camcorder aim on`, or a bound key) splits the controls,
for following a friend's line: you keep skating, the camera takes only the look and the zoom.

| | Mouse and keyboard | Pad |
|---|---|---|
| Skate | the keyboard, as always | left stick and buttons |
| Aim | move the mouse | right stick |
| Zoom (wide / tele) | mouse wheel | left / right trigger |
| Higher / lower | a bound key (Camcorder: hold it higher / lower) | RB / LB |

While it is on, skate.'s own mouse and right-stick controls (and the triggers) don't reach the
game; turn it off for those.

### Checklist

Solo:

- [ ] `camcorder power on`: the view jumps to a low, fisheye-ish handheld shot with black bars
      either side, STBY, `0:00:00` and `60min` in the corners, round black corners at full wide.
- [ ] Skate around: the camera rides with you, shakes a little, never jumps or spins.
- [ ] It feels held, not bolted on: a push, a hard stop or a sharp turn swings the camera a little
      and it settles; dropping off a ledge, the frame dips and nods on the landing and comes back.
      Rolling steadily it rides smoothly with no lag.
- [ ] Aim has weight: a quick flick of the mouse carries on a touch past and settles back; a
      locked-on friend stays framed but not perfectly (small corrections, a little behind when
      they change direction). Too much or too little: tell me which, or tune `handling.json`.
- [ ] Camera controls on (menu or `camcorder operate on`): the skater stops; the mouse (or right
      stick) aims, right turns right and up looks up; Q / E (or the triggers) zoom, the W-T bar shows
      while zooming, and the round corners vanish after a little zoom. Off again: you skate.
- [ ] True fisheye (on by default): straight lines bow outward toward the edges (rails, ledges,
      building edges), the middle looks slightly magnified, and zooming in straightens them.
      `camcorder warp off` goes back to the flat stand-in: wide, round corners, no bend. Say if the
      picture looks too soft in the middle, or if anything at the edges is cut off.
- [ ] `camcorder lens stock`: much narrower (about 44 degrees), no round corners.
- [ ] `camcorder grip long`: the camera is low, about knee height (0.45 m), not at your head;
      `hip` at the waist; `eye` at eye level. Same on a board and on foot, and on a ledge.
- [ ] Hold RB / LB (or R / F with camera controls on): the camera rises and drops smoothly, from
      a skate's height off the ground to overhead. A new grip puts it back. `camcorder height up`
      and `down` step it, for binding to keys.
- [ ] `camcorder rec`: red dot and REC, counter running. `camcorder rec` again: STBY.
      `camcorder tape clips` lists the clip.
- [ ] `camcorder tripod place`: the view jumps to a tripod just ahead of you, in SETUP with a faint
      frame. Left stick moves it, right stick aims, triggers raise and lower it. Does it stand on
      the ground? Carry it onto a ledge and off again: it steps up and drops down with the ground.
- [ ] `camcorder rec`: setup ends. Skate away: the tripod pans after you.
      `camcorder head locked`: it stops panning. `camcorder head manual` + `camcorder operate on`:
      pan by hand, with drag.
- [ ] Place a second tripod somewhere else; `camcorder tripod next` cuts between them.
- [ ] Turn on Freecam, then `camcorder power on`: no viewfinder, and `camcorder status` says the
      game kept its camera. Freecam off: the camcorder takes over.
- [ ] `camcorder power off`: your normal camera comes back. Open and close the menu, load another
      map, bail: nothing sticks or crashes.
- [ ] Handheld at eye level, get off the board and walk: the shake changes from a fine wheel buzz
      to a slower footstep bob. Back on the board: the buzz again.
- [ ] Steer while filming on (menu or `camcorder aim on`): WASD / the keyboard still skates; the
      mouse aims without moving the skater's view of anything else; the wheel zooms a step per
      notch. With a friend as subject the mouse nudges the framing; solo it aims freely and the
      aim turns with you. Open the ReSkate menu: the mouse works in it. Off again: all as before.
- [ ] `camcorder still` (or TAKE A STILL, or a bound key): "Still saved." and a JPEG in
      **Pictures\ReSkate Camcorder** of the 4:3 picture, no readouts.
- [ ] Bind buttons: Controls in the ReSkate menu lists "Camcorder on / off", "Camcorder REC / STBY",
      "Camcorder: pad to camera", "Camcorder: steer while filming", "Camcorder: set up a tripod",
      "Camcorder: next tripod" and "Camcorder: take a still".

With a friend:

- [ ] `camcorder subject next` (or `camcorder subject <name>`): lock on follows them handheld while
      you skate after them.
- [ ] A tripod with a subject follows them, not you.
- [ ] Both on this build: turn your camcorder on. Your friend sees a small white camcorder after your
      name; REC and it gets a blinking red light and "REC". STBY: the light goes; power off: the
      camcorder goes. If your friend joins after you started, it shows within 10 seconds.
- [ ] A friend on the official ReSkate build still plays with you normally (their log gets an
      "unreadable message" line now and then while you film; nothing else).
- [ ] Party Spectate while the camcorder is on: the camcorder turns off ("stop spectating to film"),
      and Spectate works.

### Recording

REC writes an MP4 of the 4:3 picture to **Videos\ReSkate Camcorder** ("ReSkate 2026-10-12 14-03-22
clip 01.mp4"); STBY closes it and a notice says where it went. 29.97 frames a second, H.264, the
fisheye's round corners included, the readouts and hints not. HD (1440 x 1080) by default;
`camcorder quality dv` (or the menu) records at the VX1000's own 640 x 480.

Sound: the game's own audio only (Windows' per-program capture, Windows 10 version 2004 or later),
so Discord, music players and notifications stay out. `camcorder sound vx` (or the menu) puts it
through the VX mic: thin low end, rolled-off top, the level control pumping, slams breaking up.
`camcorder sound off` records picture only.

- [ ] REC for ten seconds of skating, then STBY: a notice names the file; it plays in any video
      player, about ten seconds long, smooth, colours right, no REC or hint text in it.
- [ ] With the fisheye at full wide the file has the round black corners; zoomed in it does not.
- [ ] Two clips in a row: two files. A clip shot in DV quality is 640 x 480.
- [ ] HDR display on (if you have one): colours in the file look normal, not washed out or dark.
- [ ] Watch the frame rate while recording, and `logs\ReSkate.log` for "frames were dropped".
- [ ] The clip has the game's sound, in sync with the picture (a board slam lands on the frame).
      Play music or a video on the PC while recording: it must not be in the clip.
- [ ] Skate into a quiet moment and out again: the sound stays in sync after it.
- [ ] VX mic: the same line sounds thinner and more compressed; a hard landing rounds off, not crunches.
- [ ] With a friend doing tricks nearby (or several skaters at once): pops and slams are loud but
      clean in the clip, no clicks or crackle, in both game sound and VX mic.
- [ ] If the notice says "No sound: ...", send that text.
- [ ] Three short clips, then `camcorder tape capture` (or CAPTURE THE TAPE): "Tape captured." and
      one file, "ReSkate ... tape 1 (3 clips).mp4", with the clips back to back, sound in step at
      every cut. Mix a DV clip in: it is scaled to the first clip's size.
- [ ] `camcorder tape new`, one more clip, capture again: only that clip, as tape 2.
- [ ] With the true fisheye on, a clip and a still have the same bend as the screen.

### Tape look

The camera's look and the date stamp show on screen, and clips and stills record what the screen
shows (the TAPE LOOK card, or the console). The look is on unless you turn it off; each camera has
its own (switching cameras changes it straight away). Turning it on from the console also switches
to the camera's own size (640 x 480 for the VX1000).

- [ ] `camcorder look dv`, then a clip with fast skating: the motion is smoother (59.94 frames a
      second), the picture softer top to bottom, white areas clip hard, colours smear sideways off
      bright clothes and signs, hard edges have a halo, and over a minute the colour drifts.
- [ ] Each part turns off on its own (`camcorder dv fields off`, `highlights`, `balance`,
      `sharpen`, `chroma`), and the frame rate goes back to 29.97 with fields off.
- [ ] `camcorder date on`: "4:03:22 PM" over "OCT 12 2026" in the bottom right, the seconds
      ticking with the clip. `camcorder date year 1996`: the year says 1996.
- [ ] A still has the look and the stamp too.
- [ ] HD with the DV look and fields on: watch `ReSkate.log` for "frames were dropped".

### Cameras and lenses from mods

Copy `examples/mods/Camcorder Extras` (or the zip from the Releases page) into the game's `Mods`
folder, and make sure it is enabled in the mod list.

- [ ] `camcorder mods`: lists vx2000, hi8 and super8, and wide-half, from Camcorder Extras, and
      "Nothing wrong". The CAMCORDER card shows them as buttons (hover: which mod).
- [ ] VX2000: zooms further than the VX1000 (12x). Hi8: its look is softer, less saturated, a
      little grainy, with a yellow stamp bottom left. Super 8: its clips are 18 frames a second,
      grainy, no sharpening, with an orange date-only stamp, and the cartridge runs out after
      about 3 minutes 20.
- [ ] The 0.5x wide converter: about twice as wide, no bend, slightly dark corners at full wide.
- [ ] Grips: Pole cam (high overhead, looking down, wobblier) and Board cam (just off the ground,
      looking slightly up) appear beside the three built-in grips.
- [ ] Put a `handling.json` in the mod (`{"controls": {"mouse": 0.006}}`), `camcorder mods reload`:
      the mouse aims faster, and `camcorder mods` says the handling comes from that mod.
- [ ] Sound CAMERA MIC on the Hi8 and on the VX1000: the same mic settings unless a camera's
      profile gives its own.
- [ ] Break a file on purpose (delete a comma, misspell a field), `camcorder mods reload`: the
      problem is named with its file and line, and the rest still load.
- [ ] `camcorder export`: `Mods/My Camcorder` appears with the camera and lens in use as files.

### Settings

- [ ] Change the lens, grip, quality, sound, steer while filming, the tape look and the stamp; quit
      the game and start it again: they are as you left them. The file is
      `%LOCALAPPDATA%\ReSkate Camcorder\settings.txt` (one command a line; delete it to reset).

### Known gaps (expected)

- A tripod is invisible to the other players and in your own shots of it: there is no tripod
  model in the game to stand there yet.
- No second picture: the camcorder's view replaces the game's while it is on.
- The fields (60 a second) are in clips only; the screen moves at the game's frame rate.
- The recording is of what is on screen: it records the camcorder's picture only while you look
  through it.

### Worth reporting

How the aim speed feels on a pad and with a mouse, whether the lock-on lag feels like a person,
what the game draws at full tele (about 3.5 degrees tall) and full wide, and anything in
`ReSkate.log` with "camcorder", "Spectate" or "camera" in it.

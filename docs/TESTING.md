# Testing the camcorder

## 1. The model (any computer, no game)

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

This covers the camera behaviour: lens angles, zoom speeds, timecode, handheld and the board cam, the viewfinder
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
# With Rust installed, ReSkate builds its Spotify device too: fetch its crates first.
if (Test-Path SpotifyHelper\Cargo.toml) { cargo fetch --manifest-path SpotifyHelper\Cargo.toml }
cmake --preset vs2022-x64 "-DDINGOSDK_CRASH_REPORT_URL="
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

Everything is on the **CAMCORDER** page of the ReSkate menu (Insert). The first card has what
most visits are for: on / off, REC, still, capture the tape, new tape, open my clips, camera,
lens, hold it (handheld or board cam), camera control (film while skating or full camera
control), who to film, what to frame on them (board, whole skater,
head), the look, the date stamp and the sound. TRIPODS is next, open. LOOK DETAILS (the stamp's
clock, the clean viewfinder, the look's parts) and CONTROLS are closed until clicked open; they
stay as you leave them. CONTROLS has **SET UP KEYS**: one click binds F5 to F10 (on / off, REC,
quick menu, still, full camera control, tripod) where they are free; next tripod is in the quick
menu (and bindable). Keys and buttons can be changed in
SETTINGS > CONTROLS. While the camcorder is on, skate.'s own HUD is hidden. **Esc or Start** backs
out of the quick menu, a tripod's setup and full camera control; the next press opens the pause
menu.

The **quick menu** (F7 after SET UP KEYS, or `camcorder quick`) is a short list over the
viewfinder: REC, still, camera, lens, look, frame on, hold it, camera control, tripod (and next
tripod while looking through one of two or more). W / S or the wheel (stick
up / down) choose, D (stick right) does it, A (stick left) closes. The skater waits while it is
open; it closes itself after 8 seconds untouched.

**One set of camera controls, everywhere.** Filming handheld (or from the board) you keep skating
with the keyboard, the left stick and the buttons; the camera has these:

| | Mouse and keyboard | Pad |
|---|---|---|
| Aim (filming someone: nudge the framing) | move the mouse | right stick |
| Zoom (wide / tele) | Q / E, or the mouse wheel | left / right trigger |
| Higher / lower (handheld) | R / F | RB / LB |

While filming handheld, skate.'s mouse, right stick, triggers, bumpers and Q / E / R / F don't
reach the game (no flip tricks or grabs while you film). Looking through a tripod, all of it is
the game's again: you skate as normal.

**Full camera control** (F9 after SET UP KEYS, the menu's CAMERA CONTROL, or `camcorder operate`):
the skater waits and every control is the camera's: the same aim, zoom and height, and A / D or
the left stick roll the frame (a dutch angle, level again when you let go). F9 again, or Esc /
Start, and you skate.

**Setting up a tripod** uses the same, and carries it: W A S D (Shift faster) or the left stick
(L3 faster); the tripod key again, REC (done and rolling) or Esc (Start) when it's set. The
skater stands still meanwhile.

The viewfinder shows a line of these hints while they apply.

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
- [ ] Nobody to film: the mouse (or right stick) aims, right turns right and up looks up; Q / E,
      the wheel or the triggers zoom, the W-T bar shows while zooming, and the round corners vanish
      after a little zoom. While you do, Q / E / R / F, the mouse and the right stick do nothing in
      the game (no grabs, no flip tricks); the keyboard's other keys and the left stick skate.
- [ ] Turn the camera toward yourself: it swings round you on your arm, never into your body.
- [ ] Ride down stairs, a slope or off a ledge: the lens never goes under the ground.
- [ ] Film a friend (or CLOSEST with a tripod on you): FRAME ON WHOLE SKATER keeps their whole
      body in the frame, centred about the waist, not their face. BOARD centres their feet and
      board; HEAD their head and shoulders. Through an ollie it rides up with them.
- [ ] The fisheye: straight lines bow outward toward the edges (rails, ledges, building edges),
      the middle looks slightly magnified, and zooming in straightens them. Say if the picture
      looks too soft in the middle, or if anything at the edges is cut off.
- [ ] `camcorder lens stock`: much narrower (about 44 degrees), no round corners.
- [ ] HANDHELD: the camera starts low, about knee height (0.45 m, a little lower crouched on the
      board), not at your waist. Hold R / F (or RB / LB) while you skate: it rises and drops
      smoothly, from a skate's height off the ground to overhead. Same on a board, on foot, on a
      ledge. `camcorder height reset` puts it back low.
- [ ] F9 (full camera control): the skater stops, the hint says so; aim, zoom and R / F height
      work as before, and A / D (left stick) roll the frame up to about 25 degrees, easing back to
      level on release. F9 again, or Esc / Start: you skate, and the camera films while you do.
- [ ] BOARD CAM: the camera sits on the board's nose, a hand's height off the ground, looking
      ahead and a touch up; R / F don't move it.
- [ ] The shake: hold still with the camera raised to eye level and watch for 30 seconds. It wanders like hands, with no
      rhythm you can pick out or a wobble that comes round again.
- [ ] `camcorder rec`: red dot and REC, counter running. `camcorder rec` again: STBY.
      `camcorder tape clips` lists the clip.
- [ ] SET UP A TRIPOD HERE (or F10): the view jumps to a tripod just ahead of you, in SETUP with a
      faint frame and the steps along the bottom. WASD / left stick carry it, mouse / right stick
      aim, Q / E / triggers zoom, R / F / bumpers raise and lower it: the same keys as handheld.
      Does it stand on the ground? Carry it onto a ledge and off again: it steps up and drops down.
- [ ] F10 again (or DONE SETTING UP, or Esc): setup ends, you skate. REC: it rolls. Skate away:
      the tripod pans after you. (REC while setting up: done and rolling at once.)
      `camcorder head locked`: it stops panning. `camcorder head manual` + YOU AIM IT: pan it by
      hand, with drag.
- [ ] Place a second tripod somewhere else; `camcorder tripod next` cuts between them.
- [ ] Turn on Freecam, then `camcorder power on`: no viewfinder, and `camcorder status` says the
      game kept its camera. Freecam off: the camcorder takes over.
- [ ] `camcorder power off`: your normal camera comes back. Open and close the menu, load another
      map, bail: nothing sticks or crashes.
- [ ] With the camera on (and setting up a tripod, and with the quick menu open): Esc (or Start)
      opens skate.'s pause menu, after one press backing out of the setup or the menu. The
      pause menu shows and works with the mouse and pad; a recording stops (STBY). Close it: the
      camcorder's picture comes back.
- [ ] With YOU AIM IT, or setting up a tripod, move the mouse about for a while: Windows' arrow
      never shows over the picture.
- [ ] Handheld at eye level, get off the board and walk: the shake changes from a fine wheel buzz
      to a slower footstep bob. Back on the board: the buzz again.
- [ ] YOU AIM IT: WASD / the keyboard still skates; the mouse aims without moving the skater's
      view of anything else; the wheel zooms a step per notch; the aim turns with you. Open the
      ReSkate menu: the mouse works in it. AUTOMATIC again: all as before.
- [ ] `camcorder still` (or TAKE A STILL, or a bound key): "Still saved." and a JPEG in
      **Videos\ReSkate Camcorder\Stills** of the picture, no readouts.
- [ ] The first time the camera comes on (or SHOW THE GUIDE): a short list of the controls over
      the viewfinder for about 15 seconds, then not again on later games.
- [ ] CAMCORDER page > CONTROLS > SET UP KEYS: F5 to F10 show next to their jobs; F5 turns the
      camera on, F6 records. Keys you had bound already are left alone.
- [ ] F7 (quick menu): the list shows over the picture, the skater stops. W / S and the wheel move
      the amber line; D on Camera, Lens or Hold it changes it and the value updates; D on REC records
      and closes the menu; A closes. Left alone it closes after about 8 seconds.
- [ ] Record a clip, then `camcorder review` (console only now):
      the clip plays in the viewfinder at its own speed, PLAY and the time in the corners, a bar
      along the bottom; no sound. It stops at the end, on `camcorder review stop`, on REC, or with
      the camera off. Before any clip: "Nothing to review." After restarting the game it plays the
      last clip on the tape.
- [ ] Aim speed (CONTROLS card, or `camcorder sensitivity 2`): the mouse and stick aim twice as
      fast. Invert up and down: pushing up looks down.
- [ ] OPEN MY CLIPS: Explorer opens on Videos\ReSkate Camcorder (stills in its Stills folder).
- [ ] Bind buttons: Controls in the ReSkate menu lists "Camcorder on / off", "Camcorder REC / STBY",
      "Camcorder: set up a tripod", "Camcorder: next tripod", "Camcorder: full camera control / film
      while skating", "Camcorder: take a still" and "Camcorder: quick menu".

With a friend:

- [ ] `camcorder subject next` (or `camcorder subject <name>`): lock on follows them handheld while
      you skate after them.
- [ ] A tripod with a subject follows them, not you.
- [ ] A friend whose name has accents, symbols, emoji or other scripts: their button under WHO TO
      FILM picks them (the name may show with ? for letters the menu's font lacks), and the camera
      follows them. `camcorder subject` with the start of a name, any case, finds them too.
- [ ] WHO TO FILM > CLOSEST, on a tripod, two of you skating past it in turns: it pans to whoever
      is nearest, you included, and doesn't flick between two side by side (hover CLOSEST: who).
      Handheld, CLOSEST follows the nearest other player.
- [ ] A friend on the official ReSkate build plays with you normally while you film (nothing new in
      their log).
- [ ] Party Spectate while the camcorder is on: the camcorder turns off ("stop spectating to film"),
      and Spectate works.

### Recording

REC writes an MP4 of the picture (the camera's shape: 4:3 for the VX1000) to **Videos\ReSkate Camcorder** ("ReSkate 2026-10-12 14-03-22
clip 01.mp4"); STBY closes it and a notice says where it went. 29.97 frames a second, H.264, the
fisheye's round corners included, the readouts and hints not. At the camera's own size: 640 x 480
for the VX1000, 1280 x 720 for the HVX200.

Sound: the game's own audio only (Windows' per-program capture, Windows 10 version 2004 or later),
so Discord, music players and notifications stay out, heard from where the camera is (a tripod
across the spot hears you quieter and duller). Nothing else is done to it. `camcorder sound off`
(or the menu) records picture only.

- [ ] REC for ten seconds of skating, then STBY: a notice names the file; it plays in any video
      player, about ten seconds long, smooth, colours right, no REC or hint text in it.
- [ ] With the fisheye at full wide the file has the round black corners; zoomed in it does not.
- [ ] Two clips in a row: two files, 640 x 480 with the VX1000.
- [ ] HDR display on (if you have one): colours in the file look normal, not washed out or dark.
- [ ] Watch the frame rate while recording, and `logs\ReSkate.log` for "frames were dropped".
- [ ] The clip has the game's sound, in sync with the picture (a board slam lands on the frame).
      Play music or a video on the PC while recording: it must not be in the clip.
- [ ] Skate into a quiet moment and out again: the sound stays in sync after it.
- [ ] With a friend doing tricks nearby (or several skaters at once): pops and slams are loud and
      clean in the clip, no clicks or crackle.
- [ ] If the notice says "No sound: ...", send that text.
- [ ] Three short clips, then `camcorder tape capture` (or CAPTURE THE TAPE): "Capturing the
      tape..." then, after a while, "Tape captured." and one file, "ReSkate ... tape 1 (3
      clips).mp4", with the clips back to back, sound in step at every cut. Mix in a clip with
      sound off: silence for that clip.
- [ ] CAPTURE THE TAPE straight after STBY: the clip just shot is in the tape.
- [ ] Listen to a captured tape on headphones: no ticking or buzz under the sound (it was a
      sample filled or cut at every sound packet).
- [ ] Restart the game, CAPTURE THE TAPE: last game's clips are still on the tape (delete one
      first: it is left out). NEW TAPE empties it.
- [ ] On a tripod across the spot, skate away from it and back while recording (sound GAME): the
      sound gets quieter and duller as you go off, and comes from your side of the picture.
      Handheld it is as before.
- [ ] `camcorder tape new`, one more clip, capture again: only that clip, as tape 2.
- [ ] A clip and a still have the same bend as the screen.

### Tape look

The camera's look and the date stamp show on screen, and clips and stills record what the screen
shows (the CAMCORDER card's look switch, or the console). The look is on unless you turn it off
(an older settings file that had it off is ignored once, as it was off by default then); each
camera has its own (switching cameras changes it straight away). Clips are always the camera's
own size (640 x 480 for the VX1000).

- [ ] Look on, looking through the camera (not only in clips), including with Windows display
      scaling above 100% or an upscaler on: obvious at a glance. The picture is 480 lines top to bottom
      (soft, with stair-steps on diagonals), whites burn out early, blacks are dense, edges have a
      halo, a little noise in shade. Look off: crisp again. If it never changes, the notice "The
      camcorder's look can't be shown on screen here" should have appeared: send ReSkate.log.

- [ ] `camcorder look dv`, then a clip with fast skating: the motion is smoother (59.94 frames a
      second), the picture softer top to bottom, white areas clip hard, colours smear sideways off
      bright clothes and signs, hard edges have a halo, and over a minute the colour drifts.
- [ ] Each part turns off on its own (`camcorder dv fields off`, `highlights`, `balance`,
      `sharpen`, `chroma`), and the frame rate goes back to 29.97 with fields off.
- [ ] `camcorder date on`: "4:03:22 PM" over "OCT 12 2026" in the bottom right, the seconds
      ticking with the clip. `camcorder date year 1996`: the year says 1996.
- [ ] LOOK DETAILS (open the folded card) > Date and time: 1996-10-12 and 16:03, SET: the stamp says
      4:03 PM, OCT 12 1996 and runs on; it's still running on after a restart. REAL TIME puts it
      back. (`camcorder date set 1996-10-12 16:03`, `camcorder date set now`.)
- [ ] In a clip with the look's fields on (60i), the stamp sits still: no shimmer or up-and-down
      twitter on its edges.
- [ ] A still has the look and the stamp too.
- [ ] The DV look with fields on, recording: watch `ReSkate.log` for "frames were dropped".

### Cameras and lenses from mods

Copy `examples/mods/Camcorder Extras` (or the zip from the Releases page) into the game's `Mods`
folder, and make sure it is enabled in the mod list.

- [ ] `camcorder mods`: lists vx2000, hi8, super8, vhs and hvx200, and wide-half, from Camcorder Extras, and
      "Nothing wrong". The CAMCORDER card shows them as buttons (hover: which mod).
- [ ] VX2000: zooms further than the VX1000 (12x). Hi8: its look is softer, less saturated, a
      little grainy, with a yellow stamp bottom left. Super 8: its clips are 24 frames a second,
      fine-grained (not blocky), no sharpening, no date stamp (and no Date stamp switch on the
      card), and the cartridge runs out after about 2 minutes 30.
- [ ] Super 8 (its effects): the picture wanders a little in the gate and now and then jolts,
      the brightness flickers, dust specks and the odd scratch come and go, a warm light leak
      drifts in from an edge, the corners are rounded and dark, and lights glow orange. Cool
      shadows, warm highlights.
- [ ] VHS-C: 240 soft lines, colour smeared and lagging to the right of edges, a faint echo,
      the lines wobble, a torn band at the very bottom, a white streak now and then, and every
      few seconds a tracking band of torn lines and snow passes. `camcorder dv chroma off`
      (the tape part) takes all of that away but leaves the tone and grain.
- [ ] VHS-C's own viewfinder: PAUSE top left, a blinking REC on a dark backing while recording,
      SP and the counter top right, the battery bottom right, x1.0 (the zoom) bottom middle while
      zooming, faint corner brackets. Switch back to the VX1000: its viewfinder is back as it was.
- [ ] `camcorder effects` names the camera's effects in order (`(off)` by those a switch has
      turned off); `camcorder effects list` lists every type.
- [ ] A mod camera with `{"type": "lut", "file": "<name>.cube"}` and a .cube from a grading app
      beside it: the grade shows. A .cube that is broken or missing is named in `camcorder mods`.
- [ ] Frame rate with Super 8 or VHS-C on screen at 1440p: not much lower than the VX1000's.
- [ ] HVX200: a 16:9 picture. On a 16:9 screen it fills it with no black bars; on a 4:3 or
      ultrawide screen, bars where the shapes differ. With the fisheye, the housing's shadow and the
      bend still sit centred, and nothing at the picture's edges is cut off or stretched. Its clips
      are 1280 x 720, 24 frames.
      CAPTURE THE TAPE with clips from it and from the VX1000 on one tape: the ones of the other
      shape are left out and named.
- [ ] The 0.5x wide converter: about twice as wide, no bend, slightly dark corners at full wide.
- [ ] Put a `handling.json` in the mod (`{"controls": {"mouse": 0.006}}`), `camcorder mods reload`:
      the mouse aims faster, and `camcorder mods` says the handling comes from that mod.
- [ ] Break a file on purpose (delete a comma, misspell a field), `camcorder mods reload`: the
      problem is named with its file and line, and the rest still load.
- [ ] `camcorder export` (console only now): `Mods/My Camcorder`
      appears with the camera, lens and handling in use as files.

### Settings

- [ ] Change the camera, lens, handheld / board cam, sound, who to film, frame on, the tape look,
      the stamp, the tripod head, the aim speed and invert; quit
      the game and start it again: they are as you left them. The file is
      `%LOCALAPPDATA%\ReSkate Camcorder\settings.txt` (one command a line; delete it to reset).

### Known gaps (expected)

- A tripod is invisible to the other players and in your own shots of it: there is no tripod
  model in the game to stand there yet.
- No second picture: the camcorder's view replaces the game's while it is on.
- The fields (60 a second) are in clips only; the screen moves at the game's frame rate.
- Review plays the picture only, not the sound, and only the last clip recorded this game (older
  clips: OPEN CLIPS).
- The handheld camera is kept off the ground and swings round the filmer's body, but not off
  walls: aimed into a wall up close, it can see through it.
- The sound is the game's mix moved to the camera's position as well as it can be from outside
  (level, tone, side). Other players' sounds and the music move with it.
- The recording is of what is on screen: it records the camcorder's picture only while you look
  through it.

### Worth reporting

How the aim speed feels on a pad and with a mouse, whether the lock-on lag feels like a person,
what the game draws at full tele (about 3.5 degrees tall) and full wide, and anything in
`ReSkate.log` with "camcorder", "Spectate" or "camera" in it.

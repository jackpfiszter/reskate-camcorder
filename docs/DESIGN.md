# ReSkate Camcorder: design

Film yourself and your friends in skate. the way 90s and early 2000s skate videos were shot:
a Sony VX1000 with a Century fisheye, chasing a skater on a board, or set on a tripod at the
spot. No replay editor: you hold the camera, you press REC, you get the shot (or miss it).

## What the benchmark camera was

The **Sony DCR-VX1000** (1995) is the camera of the era. From Sony's spec sheet:

| | |
|---|---|
| Sensor | three 1/3" CCDs, 410,000 pixels each (4.8 x 3.6 mm active, 4:3) |
| Lens | 10:1 power zoom, f = 5.9-59 mm, F1.6, 52 mm filter thread |
| Zoom servo | wide to tele in 4.1 s (fastest) to 20 s (slowest), by rocker pressure |
| Viewfinder | colour LCD, 180,000 pixels (no flip-out screen; that came with the VX2000) |
| Format | MiniDV, NTSC: 720 x 480, 29.97 frames/s interlaced, 60 min a cassette (SP) |
| Weight | 1.46 kg without tape or battery |

The stock lens is not wide (about 44 degrees across at 5.9 mm), so skaters bayonet-mounted the
**Century Optics 0.3x fisheye** (the "MK1", "the death lens"): about 125 degrees across, with
the adapter's housing showing as round black corners at full wide until a touch of zoom
clears them. Those two looks, fisheye up close and the long lens from a distance, are the
two shots this mod has to nail.

All of these numbers live in `src/camcorder/vx1000.h` and drive the camera directly:
`optics()` gives the angle of view for a zoom position and adapter, and the zoom rocker moves at
the spec's speeds.

## How it reaches the game

ReSkate mods in `Mods/` are data only (custom Lua is switched off), so the camcorder is native
code: a ReSkate feature built into `ReSkate.dll`, upstreamed or in a fork (see
`integration/reskate/README.md`). It is built on things ReSkate already does:

| Need | ReSkate already has |
|---|---|
| Take over the screen with our own camera and FOV | the party Spectate's camera lease, `update_party_camera(transform, fov)` |
| The pad driving a camera, not the skater | Freecam's input capture, `DingoSDKOverlayReadFlightInput` / `SetFreecamInputCapture` |
| Where the local skater and the other players are | the multiplayer session's poses (`note_local_skater`, remote `render_pose`) |
| Drawing over the game | the Dear ImGui overlay (Hall of Meat draws on its background list) |
| Commands, button binds, menu | the console registry and `action_binds`, as the Trainer uses them |
| Standing a tripod on the ground | the Trainer's downward surface ray |
| A real video file | the engine's movie encoder that the replay editor exports with (`replay_export.h`) |

The model (`src/camcorder`) has no game types in it, so all of the camera behaviour is
unit-tested on any machine (`ctest`); the ReSkate side only reads the world and applies the
answer.

## Shooting

### Handheld: you are the filmer

The camera rides on your skater, held in your right hand.

- **Grips.** *Long grip*: low (about 45 cm) and out in front, the fisheye follow shot, a board
  length behind the skater. *Hip*: waist height. *Eye level*: through the viewfinder, the
  long-lens shot.
- **Lock on.** Pick a subject (`camcorder subject <player>|next`) and the filmer keeps them
  framed by themself, a little late like a person (a critically damped "operator" spring, aiming
  slightly ahead of where they are going). You keep both hands on skating; the stick, when the
  camera has it, only nudges the framing, which drifts back when you let go.
- **Operate.** skate.'s right stick does the tricks, so the camera cannot share it. `camcorder
  operate` hands the pad to the camera, as Freecam does: right stick aims (or nudges, locked on),
  triggers zoom. Off again, the pad is the skater's and lock on keeps filming. This is the
  main trade-off to test in play; a chord (hold LB + RB for camera controls, as the Trainer's
  pad shortcuts do) is the alternative if a toggle feels clumsy.
- **Hand shake.** A person breathing and holding 1.5 kg, the wheels' buzz rising with speed on
  a board, a dip with each footstep on foot. Repeatable, small (under 2.5 degrees), steadier as
  `steadiness` goes up, and naturally worse at tele because the frame is narrower.

### Tripod

- `camcorder tripod place` stands one a step ahead of you, aimed where you were aiming, and
  starts **setup**: the pad carries it over the ground (it stands on the surface), raises and
  lowers the head (15 cm "ground pod" to 2 m) and aims it, with the safe-action frame shown.
  Press REC and setup ends: the tripod stays put while you go and skate.
- **The head.** *Locked off*: nothing moves, the skater rolls through the frame. *Follow*:
  pans and tilts after the subject (or you, solo), late the way a fluid head drags. *Manual*: a
  friend operating it pans against the drag.
- **Up to four tripods** at a spot; `camcorder tripod next` cuts between them, live. With
  slots, a solo player can set up a multi-angle line and cut between angles while skating it.

### Filming friends

Everything is local: your camera shows on your screen only and sends nothing. Filming a
friend needs only their position, which the session already has. Ideas for later: a REC light
by a filming player's nametag, and a camcorder prop in the filmer's hands for everyone else.

## Recording

What "record" can mean, from cheapest to most real:

1. **The viewfinder is the product (first milestone).** REC/STBY, the counter, tape and clip
   list are real (`tape.h`: 29.97 drop-frame timecode, a 60-minute cassette that ends, clips
   numbered per tape). Players capture the screen with OBS or the GPU's recorder; `camcorder
   clean` keeps the 4:3 frame and the fisheye shadow and hides the readouts. Cheap, and it works.
2. **Clips to files (later).** The engine already encodes video for the replay editor's export
   (`addr::replay_export::submit_frame`, which ReSkate hooks so exports keep every frame).
   Starting that encoder outside the replay editor, from REC to STBY, would write each clip to
   disk with game audio. Needs reverse engineering how an export session starts.
3. **Tape as data (later).** Keep each clip's camera path and settings with the clip, so a
   take can be re-rendered or re-framed. Overlaps with the game's own replay recorder.

## The look

1. **First milestone: geometry.** The lens's angle of view (from 44 down to 4.7 degrees across
   on the stock lens), 4:3 pillarbox bars, and the fisheye's round shadow drawn over the
   picture. A flat game camera cannot bend lines, so the fisheye is approximated by giving the
   flat camera its coverage (about 94 degrees vertical).
2. **The warp.** A full-screen pass on the overlay's D3D12 command list samples the frame
   through `fisheye_source()` (equidistant projection, already in the model and tested), with
   the game camera opened just wide enough that every visible point has picture behind it
   (`FisheyeRender::warp`). That is the real barrel.
3. **DV.** Interlacing (60 fields), 4:1:1 chroma smear, the edge enhancement and the hard
   highlight clip of a 1/3" CCD, an auto white balance that drifts. Each a small shader step,
   each switchable.

## Controls (proposed)

Nothing is bound by default; players bind what they want in ReSkate's Controls, and every
action is also a console command.

| Action | Command |
|---|---|
| Camera on / off | `camcorder power toggle` |
| REC / STBY | `camcorder rec` |
| Pad to camera / to skater | `camcorder operate toggle` |
| Set up a tripod here | `camcorder tripod place` |
| Cut to next tripod | `camcorder tripod next` |
| Handheld / tripod | `camcorder mode toggle` |
| Fisheye / stock lens | `camcorder lens toggle` |
| Next subject | `camcorder subject next` |

With the pad on the camera: right stick aims, triggers zoom (left wide, right tele). In tripod
setup: left stick carries it, right stick aims, triggers raise and lower the head, and with the
boost button held the triggers zoom instead.

## Milestones

1. **Model** (done here): optics, zoom servo, tape and timecode, handheld and tripod rigs,
   viewfinder layout, console commands, 43 unit tests.
2. **In game**: the ReSkate glue (`integration/reskate`), solo handheld and tripods, the
   viewfinder overlay, binds. Tune feel in play.
3. **Friends**: subject picking from the session's players; Spectate and camcorder sharing
   the one camera lease cleanly.
4. **The warp** pass for a true fisheye.
5. **Clips to files** through the engine's encoder.
6. **DV look**; a camcorder prop in the filmer's hands for other players.

## Open questions

- Does a toggle (`operate`) or a held chord feel better for camera controls while skating?
- The game's camera at very narrow FOVs (full tele is about 3.5 degrees vertical): LOD and
  streaming are tuned for the gameplay camera's distance, not a 10x lens across a plaza.
- Hall of Meat's slow motion is single-player only; any camcorder slow-mo (overcranking was
  not a VX1000 thing anyway) would be too.

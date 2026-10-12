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
- **Aim while filming.** Following a friend's line you need both: `camcorder aim` splits the
  input instead of taking all of it. The game keeps the keyboard, the left stick and the
  buttons; the camera takes the right stick, the triggers and the mouse (the wheel zooms, a
  notch at a time like a thumb on the rocker). ReSkate's overlay does the split where it already
  holds input back for Freecam (`input_capture.cpp`: XInput, PlayStation HID reports, raw mouse
  records, the cursor), so skate. simply sees a centred right stick and a still mouse.
- **Hand shake.** A person breathing and holding 1.5 kg, the wheels' buzz rising with speed on
  a board, a dip with each footstep on foot. Repeatable, small (under 2.5 degrees), steadier as
  `steadiness` goes up, and naturally worse at tele because the frame is narrower.

### Tripod

- `camcorder tripod place` stands one a step ahead of you, aimed where you were aiming, and
  starts **setup**: the pad carries it over the ground (it stands on the surface), raises and
  lowers the head (15 cm "ground pod" to 2 m) and aims it, with the safe-action frame shown.
  Press REC and setup ends: the tripod stays put while you go and skate. While it is carried,
  its feet follow the surface under them (ReSkate's ground ray, looking from just above the
  feet down a long way), so it steps onto a ledge and drops off one.
- **No prop yet.** Nobody sees the tripod, you included. The game has no tripod object a mod can
  place, and a Build Kit item would save into the park; a stand-in needs a placement that
  isn't saved, which ReSkate does not have yet.
- **The head.** *Locked off*: nothing moves, the skater rolls through the frame. *Follow*:
  pans and tilts after the subject (or you, solo), late the way a fluid head drags. *Manual*: a
  friend operating it pans against the drag.
- **Up to four tripods** at a spot; `camcorder tripod next` cuts between them, live. With
  slots, a solo player can set up a multi-angle line and cut between angles while skating it.

### Filming friends

Your camera shows on your screen only. Filming a friend needs only their position, which the
session already has. The one thing sent is whether you are filming (`presence.h`): a small
camcorder after a filmer's nametag, with a blinking tally light and REC while they record.
ReSkate's packets cannot grow a field without shutting out players on other builds, so it rides
in the Throwdowns channel (hosts and servers pass it on unread) with a first byte no throwdown
has; official builds drop it with a log line. In ReSkate itself it would be a proper field.

A camcorder in the filmer's hands needs a model attached to the hand bone, on every player's
screen: ReSkate has no way to attach a model to a skater, and the game has no camcorder model.
That is work for the ReSkate developers (and the strongest reason to bring the camcorder to them).

## Recording

What "record" can mean, from cheapest to most real:

1. **The viewfinder is the product (first milestone).** REC/STBY, the counter, tape and clip
   list are real (`tape.h`: 29.97 drop-frame timecode, a 60-minute cassette that ends, clips
   numbered per tape). Players capture the screen with OBS or the GPU's recorder; `camcorder
   clean` keeps the 4:3 frame and the fisheye shadow and hides the readouts. Cheap, and it works.
2. **Clips to files (built).** From REC to STBY the overlay copies the 4:3 picture off the back
   buffer before it draws the readouts (`camcorder_capture.cpp`, a ring of readback buffers, never
   waiting on the GPU), and a worker thread crops, scales, adds the fisheye's corners, converts to
   NV12 and encodes H.264 to MP4 with Windows' own Media Foundation (`camcorder_video.cpp`). The
   pixel work and the 29.97 pacing are in the tested model (`recorder.h`). Sound is the game's
   process only (WASAPI process loopback, `camcorder_audio.cpp`), placed on the picture's clock
   (gaps filled, overlaps trimmed: `audio.h`) and encoded as AAC beside the H.264; the VX mic
   colours it like the camera's own microphone and level control.
   **Stills** (`camcorder still`) are the next picture off the same readback, saved as a JPEG to
   Pictures\ReSkate Camcorder. **Capturing the tape** joins the current tape's clips, in order,
   into one MP4, the way a MiniDV tape went into the computer: each clip is decoded and encoded
   again on a worker thread (`camcorder_tape.cpp`), scaled to the first clip's size, with
   silence where a clip had none so the sound stays in step at every cut.
3. **Tape as data, re-filmed from replays (later).** Keep each clip's camera path, and play it
   back through the game's replay with the replay editor's own encoder
   (`addr::replay_export::submit_frame`). That is how a tripod could film while the player skates
   with the normal camera, like real self-filming. Needs the replay editor's playback and export
   driven from code.

## Cameras and lenses as data

The VX1000 and the Century are profiles (`profiles.h`): the camera's sensor, zoom range and
speeds, how it records motion, its own recording size, cassette length, look and date stamp; a
lens's fisheye angle or converter factor and its housing's shadow. Mods add more as JSON files in
`Mods/<mod>/camcorders`, read from enabled mods at start (`camcorder mods reload` again), every
field optional and checked, so a mod is data only like every other ReSkate mod. The model reads
and tests them on its own (a small JSON reader, `json.h`), so a mod that loads in the tests loads
in the game. [MODDING.md](MODDING.md) is the reference.

## A second view (not yet possible)

The wish: skate with the normal camera and see the camcorder's shot in a small window, like a
flip-out LCD. skate.'s renderer draws one world view a frame; ReSkate has no way to ask it for a
second one, and nothing in its sources (or the game's known addresses) sets one up. Doing it
properly means finding how the engine adds a view (its per-view render pass runs per view, so
the machinery is there) and driving it from ReSkate: reverse engineering of the game binary,
best done with the ReSkate developers, who keep their own notes on it.

A cheap stand-in, alternating frames (game camera, camcorder, game camera...), would halve the
frame rate of both, flicker, and upset the game's temporal anti-aliasing and motion blur, so it
is not built. Until there is a real second view, "aim while filming" is the answer: you look
through the camcorder and steer by it, as real filmers do.

## The look

1. **First milestone: geometry.** The lens's angle of view (from 44 down to 4.7 degrees across
   on the stock lens), 4:3 pillarbox bars, and the fisheye's round shadow drawn over the
   picture. A flat game camera cannot bend lines, so the fisheye is approximated by giving the
   flat camera its coverage (about 94 degrees vertical).
2. **The warp (built).** The game camera is opened just wide enough that every visible point has
   picture behind it (`FisheyeRender::warp`). The overlay copies the frame into a texture and
   draws it back over the 4:3 picture through a 64 x 48 mesh whose texture coordinates follow the
   equidistant fisheye (`warp_uv`, tested), with ImGui's own pipeline: no new shader, and a draw
   callback switches off the HDR conversion for the mesh. Clips and stills are copied in another
   draw callback after the mesh, so they bend exactly as the screen does. The cost is softness in
   the middle: the flat render spreads its pixels over about 118 degrees, so the centre of the
   fisheye is about half the resolution of the screen.
3. **The look (built, on screen).** `look.h`: a small CCD's hard white and dense blacks, a
   wandering auto white balance, edge enhancement, grain, tape softness and colour smear, each
   switchable, in the amounts the camera's profile gives. The overlay draws it with the fisheye
   in one pixel shader pass (`camcorder_warp.cpp`), so the viewfinder shows what the tape gets;
   clips and stills copy the result. The date stamp is drawn over it, the same marks the CPU
   would burn (`stamp_marks`). Film cameras (24p, 18p) hold each picture on screen for their
   frame time. The 60 fields stay with the recording (clips at 59.94, each frame one field),
   because they follow the clip's frames, not the screen's. Where the shader cannot be built, the
   fisheye falls back to an ImGui mesh and the look to the CPU in the encoder (`recorded_finish`).

## Controls (proposed)

Nothing is bound by default; players bind what they want in ReSkate's Controls, and every
action is also a console command.

| Action | Command |
|---|---|
| Camera on / off | `camcorder power toggle` |
| REC / STBY | `camcorder rec` |
| Pad to camera / to skater | `camcorder operate toggle` |
| Aim while filming | `camcorder aim toggle` |
| Take a still | `camcorder still` |
| Capture the tape | `camcorder tape capture` |
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
  With `aim`, is giving up skate.'s mouse and right-stick tricks while filming acceptable?
- The game's camera at very narrow FOVs (full tele is about 3.5 degrees vertical): LOD and
  streaming are tuned for the gameplay camera's distance, not a 10x lens across a plaza.
- Hall of Meat's slow motion is single-player only; any camcorder slow-mo (overcranking was
  not a VX1000 thing anyway) would be too.

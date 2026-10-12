# Making cameras and lenses

The camcorder's cameras and lenses are data, so anyone can add one. A camera or lens mod is an
ordinary ReSkate mod: a folder in `Mods`, installed, shared and switched on and off from the
launcher's mod list or the mod browser like any other. Nothing in it runs; it is a few small JSON files.

```
Mods/
  My Camcorder/
    manifest.json                  the mod's name, author, version and description
    camcorders/
      vx2000.camera.json           a camera
      hi8.camera.json              another
      wide-half.lens.json          a lens
      pole.grip.json               a way of holding it
      handling.json                how it all feels (optional; one per mod)
```

ReSkate.dll holds only the camcorder's workings (taking the camera, the controls, the picture,
recording, sound). Everything about a particular camera, and every number that sets how filming
feels, is data like this, so it can change from one mod release to the next with no ReSkate
update.

`examples/mods/Camcorder Extras` in this repository is a complete one (the VX2000, a Hi8
handycam, Super 8 film, a 0.5x wide converter, a pole cam and a board cam): copy it and change
it. The quickest start is in the game: choose the camera, lens and grip closest to what you want,
then type `camcorder export`. That writes them, and the handling in use, every field filled in,
to `Mods/My Camcorder`.

## How the camcorder reads them

- The file name is the id: `vx2000.camera.json` is the camera `vx2000`, used in the console
  (`camcorder camera vx2000`) and the settings file. Ids are lowercase letters, digits, `-` and
  `_`, up to 32.
- **Every field is optional.** What a camera leaves out is the VX1000's, what a lens leaves out
  is the Century fisheye's, and what a grip leaves out is the long grip's. So the VX2000 is just
  its name and its longer zoom.
- `handling.json` is layered instead: each mod's changes only the numbers it gives, a higher
  mod's over a lower one's, over the camcorder's own.
- A profile with the id of a built-in one, or of one in a mod lower in the load order, replaces
  it. A `vx1000.camera.json` can retune the built-in VX1000.
- Values outside their range are brought into it, and unknown fields are ignored. Either way the
  game notes it: `camcorder mods` lists what was found and anything wrong (also in
  `logs\ReSkate.log`). After editing a file, `camcorder mods reload` reads them again without a
  restart.
- Profiles are read only from enabled mods.

## Cameras (`<id>.camera.json`)

| Field | Default (VX1000) | Range | What it is |
|---|---|---|---|
| `name` | the id | up to 48 characters | What the menu shows |
| `sensor_mm` | `[4.8, 3.6]` | 1 to 40 each | The sensor's (or film frame's) width and height, mm. With the focal lengths, sets the angle of view |
| `zoom_mm` | `[5.9, 59]` | wide 1 to 200; at most 50x | The lens's focal length at wide and at tele, mm. Equal numbers: a fixed lens |
| `zoom_seconds` | `[4.1, 20]` | 0.5 to 120 | Wide to tele with the rocker pressed fully, and pressed lightly |
| `motion` | `"60i"` | `60i` `50i` `30p` `25p` `24p` `18p` | How it records motion. `60i` is NTSC video, `50i` PAL; with the look's fields on, those record 59.94 or 50 frames a second. `18p` is Super 8 |
| `lines` | `480` | 120 to 1080 | Its own recording height (quality OWN); the width is 4:3 of it |
| `tape_minutes` | `60` | 1 to 600 | One cassette (or film cartridge: Super 8 is about 3.3 minutes at 18 frames) |
| `look` | see below | | What its sensor, processing and tape do to the picture |
| `stamp` | see below | | Its date stamp |

### `look`

These apply to clips and stills when the player turns the look on (the TAPE LOOK card, or
`camcorder look dv`). Each can also be switched off by the player (`camcorder dv <part> off`).

| Field | Default | Range | What it is |
|---|---|---|---|
| `format` | `"MiniDV"` | up to 24 characters | What the menu calls the look ("MiniDV look") |
| `black_crush` | `0.035` | 0 to 0.3 | The share of the darkest range pushed to black |
| `highlight_gain` | `1.1` | 0.5 to 2 | Above 1, white comes early and clips hard (1.1 clips the top 10%) |
| `saturation` | `1` | 0 to 2 | Colour strength; Hi8 and VHS were lower |
| `white_balance_drift` | `0.035` | 0 to 0.2 | How far the colour wanders warm and cool |
| `sharpen` | `0.44` | 0 to 2 | Edge enhancement: halos on hard edges. Film has none |
| `grain` | `0` | 0 to 1 | Noise or film grain, new every frame |
| `chroma_width` | `4` | 0 to 16 | Colour is smeared over this many pixels of a 720-wide picture (DV's 4:1:1 is 4); 0 for none |
| `softness` | `0` | 0 to 8 | Sideways blur, in pixels of a 720-wide picture (analogue tape: 1 to 2) |

### `stamp`

| Field | Default | Choices | What it is |
|---|---|---|---|
| `corner` | `"bottom-right"` | `bottom-right` `bottom-left` | Where it sits |
| `lines` | `"time-date"` | `time-date` `date-time` `date` | What it shows, top line first |
| `date` | `"month-day-year"` | `month-day-year` `day-month-year` | `OCT 12 1996` or `12 OCT 1996` |
| `colour` | `[235, 235, 235]` | 0 to 255 each | Red, green, blue. Film cameras' stamps were orange |

### `mic`

The camera's own microphone, used when the player records with `camcorder sound mic` (CAMERA MIC).

| Field | Default (VX1000) | Range | What it is |
|---|---|---|---|
| `high_cut` | `120` | 10 to 2000 | Hz: little below this (a small capsule has no bass) |
| `low_cut` | `9000` | 1000 to 23000 | Hz: rolled off above this |
| `target` | `0.25` | 0.02 to 0.9 | The level its automatic level control aims for |
| `most_gain` | `3` | 0 to 10 | The most it turns a quiet room up (the pumping) |
| `attack` | `0.01` | 0.001 to 0.5 | Seconds to duck a slam |
| `release` | `0.4` | 0.02 to 5 | Seconds to bring the room back up |
| `drive` | `1.2` | 0.1 to 5 | How hard it pushes into the converter: loud hits round off |

## Grips (`<id>.grip.json`)

A way of holding the camera handheld, listed beside Long grip, Hip and Eye level.

| Field | Default (long grip) | Range | What it is |
|---|---|---|---|
| `name` | the id | up to 48 characters | What the menu shows |
| `height` | `0.45` | 0.05 to 3 | The lens above the feet, standing, metres |
| `side` | `0.35` | -1 to 1 | Out to the right (negative: left) |
| `ahead` | `0.55` | -1 to 2 | In front of the body (negative: behind) |
| `pitch` | `5` | -60 to 60 | Degrees it starts tilted up (negative: down) |
| `crouch` | `0.92` | 0.5 to 1.2 | On a board the filmer crouches: heights times this (1 for a rig fixed to the board) |
| `shake` | `1` | 0 to 4 | The hand shake times this: a pole wobbles more, a mount less |

## Handling (`handling.json`)

How filming feels. Every number is optional; the defaults are the camcorder's own.

| Section | Field | Default | Range | What it is |
|---|---|---|---|---|
| `filmer` | `reaction` | `1.6` | 0.2 to 10 | Hz: how fast the filmer swings onto a locked-on subject |
| | `look_speed` | `2.2` | 0.2 to 10 | Radians a second at full stick, at wide |
| | `aim_height` | `0.8` | 0 to 2 | Metres above a subject's feet to frame (handheld and tripods) |
| | `lead` | `0.12` | 0 to 1 | Seconds: aim where the subject is going |
| | `steadiness` | `0.5` | 0 to 1 | 0 a nervous first-timer, 1 a seasoned filmer |
| | `max_nudge` | `25` | 0 to 60 | Degrees the stick shifts the framing off a locked-on subject |
| | `raise_speed` | `0.8` | 0.05 to 5 | Metres a second the camera rises and drops (LB / RB) |
| | `lowest`, `highest` | `0.12`, `2.3` | 0.02 to 4 | The handheld lens's range above the feet |
| | `height_step` | `0.15` | 0.01 to 1 | Metres a `camcorder height up` / `down` moves it |
| `shake` | `hands`, `wheels`, `footsteps` | `1` each | 0 to 5 | How much of each kind of shake |
| `tripod` | `height` | `1.1` | 0.15 to 2 | A new tripod's height |
| | `drag` | `0.35` | 0.02 to 3 | Seconds the fluid head takes to answer |
| | `pan_speed` | `1.4` | 0.1 to 10 | Radians a second at full handle, at wide |
| | `follow` | `1.1` | 0.1 to 10 | Hz: how fast a following head swings after the subject |
| `controls` | `mouse` | `0.0025` | 0.0002 to 0.02 | Radians of aim a pixel of mouse movement |
| | `wheel_notch` | `0.15` | 0.02 to 1 | Seconds of zoom a mouse wheel notch gives |

For example, a steadier filmer with a faster mouse:

```json
{
  "filmer": { "steadiness": 0.85 },
  "controls": { "mouse": 0.004 }
}
```

## Lenses (`<id>.lens.json`)

A lens goes on the front of any camera.

| Field | Default (Century) | Range | What it is |
|---|---|---|---|
| `name` | the id | up to 48 characters | What the menu shows |
| `fisheye` | `true` | | A fisheye bends straight lines; otherwise it is a flat converter |
| `fov` | `125` | 60 to 170 | A fisheye's degrees across the 4:3 picture at the camera's widest. A flat game picture cannot feed more than 170 |
| `magnification` | `1` | 0.25 to 4 | A flat converter's effect on focal length: `0.5` for a half wide, `1.5` for a tele converter |
| `vignette` | `[1.08, 0.35]` | radius 0 to 1.25, growth 0 to 2 | The housing's round shadow: its radius at full wide, in half picture widths (the corners are at 1.25), and how far it opens with each 1x of zoom. `[0, 0]` for none; a flat converter has none unless it says so |

## Not yet

- **Widescreen cameras.** Every camera is 4:3 for now (the frame, the lens maths and the files).
  16:9 bodies like the HVX200 need the picture's shape to become part of a camera; it is planned.
- **A camera you can see.** Showing a camcorder model in the filmer's hands, or picking one in the
  character menu, needs the game's character attachments worked out with the ReSkate developers.
- **New kinds of effect.** A profile sets how strongly each built-in effect works. An effect the
  camcorder does not have yet (VHS tracking lines, light leaks, gate weave) needs adding to the
  camcorder first; ask for it.

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
      pole.grip.json               a rig (a way of holding it)
      handling.json                how it all feels (optional; one per mod)
```

ReSkate.dll holds only the camcorder's workings (taking the camera, the controls, the picture,
recording, sound). Everything about a particular camera, and every number that sets how filming
feels, is data like this, so it can change from one mod release to the next with no ReSkate
update.

`examples/mods/Camcorder Extras` in this repository is a complete one (the VX2000, a Hi8
handycam, Super 8 film, a 0.5x wide converter, a pole cam and a board cam): copy it and change
it. The quickest start is in the game: choose the camera, lens and rig closest to what you want,
then type `camcorder export`. That writes them, and the handling in use, every field filled in,
to `Mods/My Camcorder`.

## How the camcorder reads them

- The file name is the id: `vx2000.camera.json` is the camera `vx2000`, used in the console
  (`camcorder camera vx2000`) and the settings file. Ids are lowercase letters, digits, `-` and
  `_`, up to 32.
- **Every field is optional.** What a camera leaves out is the VX1000's, what a lens leaves out
  is the Century fisheye's, and what a rig leaves out is the hand's (the built-in "Handheld"). So the VX2000 is just
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
| `aspect` | the sensor's (4:3) | 0.5 to 3 | The picture's shape: a number (`1.778`) or `[width, height]` (`[16, 9]`). The sensor keeps its width and is cut to this shape, as a camera recording 16:9 from a 4:3 chip does. The viewfinder, the fisheye and the clips all take it: `[16, 9]` fills a 16:9 screen and records 1920 x 1080, `1` is square. Or give the real `sensor_mm` (Super 8's frame is `[5.79, 4.01]`) and leave this out |
| `zoom_mm` | `[5.9, 59]` | wide 1 to 200; at most 50x | The lens's focal length at wide and at tele, mm. Equal numbers: a fixed lens |
| `zoom_seconds` | `[4.1, 20]` | 0.5 to 120 | Wide to tele with the rocker pressed fully, and pressed lightly |
| `motion` | `"60i"` | `60i` `50i` `30p` `25p` `24p` `18p` | How it records motion. `60i` is NTSC video, `50i` PAL; with the look's fields on, those record 59.94 or 50 frames a second. `18p` is Super 8 |
| `lines` | `480` | 120 to 1080 | Its own recording height (quality OWN); the width follows the picture's shape |
| `tape_minutes` | `60` | 1 to 600 | One cassette (or film cartridge: Super 8 is about 3.3 minutes at 18 frames) |
| `look` | see below | | What its sensor, processing and tape do to the picture |
| `stamp` | see below | | Its date stamp |

### `look`

These apply on screen, in clips and in stills while the player has the look on (the CAMCORDER
card's look switch, or `camcorder look dv`). The camera's `lines` also show: the picture is drawn
at that many lines from top to bottom. Each can also be switched off by the player (`camcorder dv <part> off`).

| Field | Default | Range | What it is |
|---|---|---|---|
| `format` | `"MiniDV"` | up to 24 characters | What the menu calls the look ("MiniDV look") |
| `black_crush` | `0.05` | 0 to 0.3 | The share of the darkest range pushed to black |
| `highlight_gain` | `1.15` | 0.5 to 2 | Above 1, white comes early and clips hard (1.15 clips the top 13%) |
| `saturation` | `1` | 0 to 2 | Colour strength; Hi8 and VHS were lower |
| `white_balance_drift` | `0.035` | 0 to 0.2 | How far the colour wanders warm and cool |
| `sharpen` | `0.6` | 0 to 2 | Edge enhancement: halos on hard edges. Film has none |
| `grain` | `0.04` | 0 to 1 | Noise or film grain, new every frame |
| `chroma_width` | `4` | 0 to 16 | Colour is smeared over this many pixels of a 720-wide picture (DV's 4:1:1 is 4); 0 for none |
| `softness` | `0` | 0 to 8 | Sideways blur, in pixels of a 720-wide picture (analogue tape: 1 to 2) |
| `highlight_knee` | `0.925` | 0.5 to 1 | Where a clipped colour starts bleeding into the others, so blown highlights go white (of full white) |
| `highlight_spread` | `0.075` | 0.005 to 0.5 | How far above the knee they are fully white: small is a hard edge, large a soft bloom |
| `grain_size` | `1` | 0.25 to 8 | The grain's size, in pixels of a 720-wide picture. Film: 2 to 3 |
| `grain_colour` | `0` | 0 to 1 | 0: the same on every colour (a CCD's noise); 1: each colour its own (cheap tape, film) |
| `sharpen_radius` | `1` | 0.25 to 8 | How wide the sharpening's halo is, in pixels of a 720-wide picture |
| `drift_seconds` | `[37, 11]` | 1 to 3600 each | The white balance's two wanders, a slow one and a quicker one, in seconds a sway |

### `stamp`

| Field | Default | Choices | What it is |
|---|---|---|---|
| `corner` | `"bottom-right"` | `bottom-right` `bottom-left` `top-right` `top-left` | Where it sits |
| `lines` | `"time-date"` | `time-date` `date-time` `date` | What it shows, top line first |
| `date` | `"month-day-year"` | `month-day-year` `day-month-year` | `OCT 12 1996` or `12 OCT 1996` |
| `colour` | `[235, 235, 235]` | 0 to 255 each | Red, green, blue. Film cameras' stamps were orange |
| `size` | `1` | 0.25 to 4 | How big: 1 is a VX1000's (a dot 1/130 of the picture's height) |
| `margin` | `[0.07, 0.08]` | 0 to 0.4 each | From the picture's side, and from its bottom (or top), in fractions of its width and height |
| `outline` | `true` | | A dark edge round each dot, so it reads over anything |
| `outline_colour` | `[16, 16, 16]` | 0 to 255 each | That edge's colour |

### `light`

The lamp on the camera (the CAMCORDER card's Camera light, or `camcorder light`). It brightens
the middle of the picture, falling off to the edges.

| Field | Default | Range | What it is |
|---|---|---|---|
| `strength` | `2.5` | 0 to 10 | How much it brightens the middle: the light there is times 1 + this |
| `reach` | `0.8` | 0.1 to 3 | The beam's width, in half picture widths |
| `colour` | `[255, 245, 224]` | 0 to 255 each | Its colour: a halogen is warm on a daylight-balanced camera, an LED cold |

### `viewfinder`

| Field | Default | Range | What it is |
|---|---|---|---|
| `text` | `[240, 240, 240]` | 0 to 255 each | The readouts' colour (counter, tape, battery, zoom bar) |
| `rec` | `[235, 40, 40]` | 0 to 255 each | The REC light's |

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

## Rigs (`<id>.grip.json`)

A way of holding the camera handheld: a pole cam, a board-mounted cam. The built-in rig is the
hand ("Handheld", id `long`); the player moves it up and down on the fly (LB / RB, or the low, hip
and eye presets), so a rig is for what differs in more than height: reach, tilt, shake. The menu
shows a Rig choice once a mod adds one. (The files keep the name `.grip.json`.)

| Field | Default (Handheld) | Range | What it is |
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
| `arms` | `sway` | `2.5` | 0.3 to 15 | Hz: how stiffly the arms hold the camera side to side and front to back |
| | `bob` | `3.5` | 0.3 to 15 | Hz: the same up and down (a landing dips it) |
| | `damping` | `0.45` | 0.1 to 2 | Under 1 the camera swings past and back after a jolt; 1 settles without |
| | `tilt` | `0.8` | 0 to 4 | Radians the frame tips a metre of swing (the nod on a landing) |
| | `reach` | `0.3` | 0 to 1 | Metres the camera can swing at most |
| `aim` | `swing` | `3` | 0.3 to 15 | Hz the camera's weight follows where the hands aim (lower: heavier) |
| | `damping` | `0.7` | 0.1 to 2 | Under 1 a quick turn carries on a little past and settles |
| | `hunting` | `1` | 0 to 5 | A person's small corrections chasing a subject (0: none) |
| `shake` | `hands`, `wheels`, `footsteps` | `1` each | 0 to 5 | How much of each kind of shake |
| | `speed` | `1` | 0.1 to 8 | How quickly the hands wander: 2 is twice as jittery, 0.5 slow and floaty |
| `sound` | `near` | `3` | 0.5 to 100 | Metres: closer to the skater than this, the camera hears the game's sound as it is |
| | `floor` | `0.12` | 0 to 1 | The quietest a far-off skater gets, of their level close by |
| | `air` | `10` | 1 to 1000 | Metres beyond `near` over which the highs halve (the air takes them) |
| | `pan` | `0.7` | 0 to 1 | How far a far-off skater's sound moves to their side of the picture |
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
| `fov` | `125` | 60 to 170 | A fisheye's degrees across the picture at the camera's widest. A flat game picture cannot feed more than 170 |
| `projection` | `"equidistant"` | `equidistant` `equisolid` `stereographic` `orthographic` | How a fisheye bends. Equidistant: the Century's. Equisolid: most modern fisheyes, the edges squeezed a little more. Stereographic: shapes stay rounder toward the edges. Orthographic: the edges squeezed hardest, a bubble |
| `magnification` | `1` | 0.25 to 4 | A flat converter's effect on focal length: `0.5` for a half wide, `1.5` for a tele converter |
| `vignette` | `[1.08, 0.35]` | radius 0 to 2.5, growth 0 to 2 | The housing's round shadow: its radius at full wide, in half picture widths (a 4:3 picture's corners are at 1.25, a 16:9's at 1.15), and how far it opens with each 1x of zoom. `[0, 0]` for none; a flat converter has none unless it says so |

## Not yet

- **A camera you can see.** Showing a camcorder model in the filmer's hands, or picking one in the
  character menu, needs the game's character attachments worked out with the ReSkate developers.
- **New kinds of effect.** A profile sets how each built-in effect looks and behaves. An effect the
  camcorder does not have yet (VHS tracking lines, light leaks, gate weave, dust and scratches)
  needs adding to the camcorder first; ask for it.
- **The viewfinder's layout and lettering.** A camera sets its readouts' colours, not where they
  sit or their font.

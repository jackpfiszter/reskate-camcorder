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
| `look` | see below | | What its sensor, processing and tape do to the picture: a few numbers |
| `effects` | the look's | up to 32 | Or the picture as a list of effects, in order, for full control: see below |
| `stamp` | see below | | Its date stamp |

### `look`

The quick way to a camera's picture: a few numbers for the classic camcorder look. For anything
more (VHS tracking, film dust, a colour grade, bloom, scanlines), use `effects` instead.

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

### `effects`

The camera's picture as a list of effects, run top to bottom: each takes the picture the one above
it left. They are building blocks from a fixed library, all drawn by the camcorder itself, so a mod
stays data: pick them, order them, tune them. A camera with `effects` uses them instead of its
`look` numbers (`look.format` still names the look in the menu); one without gets the same picture
from its `look` (the VX1000's is `resolution`, a `blur` of the colour, `sharpen`,
`white_balance`, `tone`, `grain`). `camcorder effects` lists a camera's, and `camcorder effects list`
every type.

```json
"effects": [
  {"type": "resolution", "lines": 240},
  {"type": "blur", "channels": "chroma", "direction": "horizontal", "radius": 4, "shift": 2},
  {"type": "blur", "channels": "luma", "direction": "horizontal", "radius": 1.5},
  {"type": "tone", "black_crush": 0.08, "gain": 1.1},
  {"type": "hue_saturation", "saturation": 0.8},
  {"type": "line_wobble", "amount": 1},
  {"type": "tracking", "duty": 0.15, "period": 9},
  {"type": "head_switch"},
  {"type": "dropouts", "rate": 1.5},
  {"type": "grain", "amount": 0.1, "colour": 0.6, "shadows": 0.5}
]
```

That is a worn VHS camcorder. The picture is drawn on screen this way, and clips and stills copy
the screen, so they have it too. (Where a PC can't draw it, clips get the nearest classic look:
the `tone`, `hue_saturation`, `white_balance`, `sharpen`, `grain` and `blur` blocks' numbers.)

- **Units.** Sizes are in pixels of a 720-wide picture, so a block looks the same at any
  resolution. Places are fractions of the picture, 0 to 1 from the top left. Colours are
  `[red, green, blue]`, 0 to 255. Times are in seconds, rates per second.
- **Every block** takes `type` (which one), `mix` (0 to 1, default 1: how much of its change is
  kept) and `part`: which of the player's look switches turns it off (`tone`, `white_balance`,
  `sharpen`, `tape` or `grain`; `always` follows only the look's own on and off). Each type has
  its own part by default, given below.
- **What a block leaves out** is its default. A field that is wrong, out of range or misspelt
  is noted in `camcorder mods`; a block that can't be used (an unknown type, a `.cube` that won't
  read) is left out and the rest still run.
- **A colour grade** from a grading app: export a 3D `.cube` (up to 65 a side), put it beside the
  camera's file, and add `{"type": "lut", "file": "my-grade.cube"}`. 16 MB at most.

#### Recipes

- **Super 8**: `weave`, `flicker`, `gate`, `dust`, `light_leak`, a `lut` or `split_tone` for the
  stock, `grain` with `size` 2.5, and `motion` `"18p"`.
- **A night shot on a CCD**: `bloom` with a low `threshold`, `ccd_smear`, `grain` with `shadows` 1.
- **Black and white with a red filter**: `monochrome` with `weights` `[0.9, 0.3, 0]`, then `levels`
  with `contrast` 1.3.
- **Cross-processed**: `channel_mixer`, then `curves` on `"blue"`.
- **A cheap kids' camera**: `resolution` with `samples` 160 and `filter` `"sharp"`, `posterize`,
  `distort`, `chromatic_aberration`.
- **A TV playing the tape**: everything else, then `scanlines` and `vignette` last.

#### Every effect

#### `resolution`

The format's raster: so many lines (and samples across), each an average. Part: `tape`.

| Field | Default | Range | What it is |
|---|---|---|---|
| `lines` | 480 | 0 to 2160 | Lines from top to bottom, each the average of its height. 0 keeps the screen's |
| `samples` | 0 | 0 to 3840 | Samples across, each the average of its width. 0 keeps the screen's |
| `filter` | `"smooth"` | `smooth` `sharp` | `smooth` blends between neighbouring lines as a TV does; `sharp` shows each as a block |

#### `blur`

Softens: everything, or brightness or colour alone. Part: `tape`.

| Field | Default | Range | What it is |
|---|---|---|---|
| `radius` | 1 | 0 to 60 | How far it spreads, either side |
| `direction` | `"both"` | `horizontal` `vertical` `both` | `horizontal` (along the tape's lines), `vertical` or `both` |
| `channels` | `"all"` | `all` `luma` `chroma` | `all`, `luma` (brightness only) or `chroma` (colour only: tape's colour smear) |
| `shape` | `"gaussian"` | `gaussian` `box` | `gaussian` (soft) or `box` (an even average) |
| `shift` | 0 | -40 to 40 | Moves what it blurred sideways: colour lagging behind on tape |

#### `sharpen`

Edge enhancement. Part: `sharpen`.

| Field | Default | Range | What it is |
|---|---|---|---|
| `amount` | 0.6 | 0 to 5 | Edge enhancement: a bright and a dark halo along edges |
| `radius` | 1 | 0.25 to 10 | The halo's reach |
| `direction` | `"horizontal"` | `horizontal` `vertical` `both` | `horizontal` (as video cameras did), `vertical` or `both` |
| `channels` | `"luma"` | `luma` `all` | `luma` (brightness, as cameras did) or `all` |
| `threshold` | 0 | 0 to 1 | Edges fainter than this are left alone (keeps noise from sharpening) |

#### `bloom`

Bright parts glow (or, tinted red, film's halation). Part: `tone`.

| Field | Default | Range | What it is |
|---|---|---|---|
| `threshold` | 0.75 | 0 to 1 | How bright a part must be to glow |
| `amount` | 0.5 | 0 to 5 | How strongly it glows |
| `radius` | 24 | 1 to 200 | How far the glow spreads |
| `colour` | [255, 255, 255] | 0 to 255 | The glow's colour: white for bloom, red-orange for film's halation |

#### `ghost`

A faint echo of the picture beside it. Part: `tape`.

| Field | Default | Range | What it is |
|---|---|---|---|
| `offset` | [12, 0] | -200 to 200 | Where the echo sits from the picture, [right, down] |
| `amount` | 0.15 | 0 to 1 | How strong the echo is |

#### `ccd_smear`

A bright light's streak up and down the picture, as on a CCD. Part: `tone`.

| Field | Default | Range | What it is |
|---|---|---|---|
| `threshold` | 0.95 | 0 to 1 | How bright a light must be to smear |
| `amount` | 0.6 | 0 to 4 | How bright the streak is |
| `length` | 1 | 0.05 to 1 | How far it runs up and down, of the picture's height (1: top to bottom, as on a CCD) |
| `colour` | [255, 255, 255] | 0 to 255 | The streak's tint |

#### `tone`

A small camera's tone: dense blacks, early white, blown highlights going white. Part: `tone`.

| Field | Default | Range | What it is |
|---|---|---|---|
| `black_crush` | 0.05 | 0 to 0.5 | The share of the darkest range pushed to black |
| `gain` | 1.15 | 0.5 to 3 | Over 1, white arrives early (1.15 clips the top 13%) |
| `knee` | 0.925 | 0.5 to 1 | Where a clipped colour starts going white (of white) |
| `spread` | 0.075 | 0.005 to 1 | How far above the knee it is fully white |
| `bleed` | `true` | `true` `false` | Blown highlights go white (a small CCD's), rather than staying their colour |

#### `levels`

Black and white points, gamma, exposure and contrast. Part: `tone`.

| Field | Default | Range | What it is |
|---|---|---|---|
| `black` | 0 | -0.5 to 0.9 | What becomes black |
| `white` | 1 | 0.1 to 2 | What becomes white |
| `gamma` | 1 | 0.1 to 5 | Over 1 lifts the middle, under 1 sinks it |
| `output` | [0, 1] | -0.5 to 2 | [black, white] it all ends up between: [0.1, 0.9] for a faded print |
| `exposure` | 0 | -5 to 5 | Stops brighter (or darker, under 0) |
| `contrast` | 1 | 0 to 4 | About the pivot: over 1 more, under 1 less |
| `pivot` | 0.5 | 0 to 1 | What contrast leaves alone |

#### `curves`

A curve through up to 8 points. Part: `tone`.

| Field | Default | Range | What it is |
|---|---|---|---|
| `channel` | `"all"` | `all` `luma` `red` `green` `blue` | `all` (each colour through the curve), `luma` (brightness), `red`, `green` or `blue` |
| `points` | `[[0, 0], [1, 1]]` | 2 to 8 points, 0 to 1 | [[in, out], ...]: 2 to 8 points the curve passes through, smoothly, from 0 to 1 |

#### `lift_gamma_gain`

A colourist's shadows, middle and highlights, per colour. Part: `tone`.

| Field | Default | Range | What it is |
|---|---|---|---|
| `lift` | [0, 0, 0] | -1 to 1 | Raises (or sinks) the shadows, [red, green, blue] or one number |
| `gamma` | [1, 1, 1] | 0.1 to 5 | The middle: over 1 brighter |
| `gain` | [1, 1, 1] | 0 to 5 | The highlights: a multiplier |

#### `white_balance`

The colour's balance, and an auto white balance that never settles. Part: `white_balance`.

| Field | Default | Range | What it is |
|---|---|---|---|
| `gain` | [1, 1, 1] | 0 to 4 | [red, green, blue] multipliers |
| `temperature` | 0 | -1 to 1 | Warmer (over 0) or cooler |
| `tint` | 0 | -1 to 1 | Magenta (over 0) or green |
| `drift` | 0 | 0 to 0.5 | How far the auto white balance wanders warm and cool, as a gain (0.035 is 3.5%) |
| `drift_seconds` | [37, 11] | 1 to 600 | Its two wanders' periods: a slow one and a quicker one |

#### `hue_saturation`

Hue, saturation, vibrance and lightness. Part: `tone`.

| Field | Default | Range | What it is |
|---|---|---|---|
| `hue` | 0 | -180 to 180 | Turns every colour round the colour wheel, degrees |
| `saturation` | 1 | 0 to 4 | 0 grey, 1 as it was |
| `vibrance` | 0 | -1 to 1 | Saturation for the dull colours, leaving the strong ones |
| `lightness` | 0 | -1 to 1 | Toward white (over 0) or black |

#### `channel_mixer`

Each colour made of the others: cross-processing, infrared, swaps. Part: `tone`.

| Field | Default | Range | What it is |
|---|---|---|---|
| `red` | [1, 0, 0] | -2 to 2 | The new red, as [red, green, blue] of the old |
| `green` | [0, 1, 0] | -2 to 2 | The new green |
| `blue` | [0, 0, 1] | -2 to 2 | The new blue |
| `offset` | [0, 0, 0] | -1 to 1 | Added to each |

#### `monochrome`

Black and white (through a filter), or toned. Part: `tone`.

| Field | Default | Range | What it is |
|---|---|---|---|
| `weights` | [0.2126, 0.7152, 0.0722] | 0 to 2 | How much of [red, green, blue] makes the grey: a red filter is [0.9, 0.3, 0] |
| `colour` | [255, 255, 255] | 0 to 255 | The tone it is printed in: sepia is [255, 225, 180] |

#### `split_tone`

One tint in the shadows, another in the highlights. Part: `tone`.

| Field | Default | Range | What it is |
|---|---|---|---|
| `shadows` | [90, 140, 160] | 0 to 255 | The shadows' tint (128 on each is none) |
| `highlights` | [255, 190, 120] | 0 to 255 | The highlights' tint |
| `balance` | 0 | -1 to 1 | Where shadows end and highlights start: over 0 more highlights |
| `amount` | 0.5 | 0 to 1 | How strong |

#### `posterize`

Colour in steps. Part: `tone`.

| Field | Default | Range | What it is |
|---|---|---|---|
| `levels` | 8 | 2 to 256 | Steps of each colour |
| `dither` | 0 | 0 to 1 | Noise that breaks up the steps |

#### `invert`

A negative. Part: `tone`.

No numbers of its own (`mix` sets how much).

#### `lut`

A colour grade from a .cube file. Part: `tone`.

| Field | Default | Range | What it is |
|---|---|---|---|
| `file` |  | a `.cube` | A .cube file beside the camera's (3D, up to 65 a side; graders and editors export them) |

#### `flicker`

The brightness flickers, as a film projector's or a cheap auto exposure's. Part: `always`.

| Field | Default | Range | What it is |
|---|---|---|---|
| `amount` | 0.05 | 0 to 1 | How far the brightness swings |
| `speed` | 8 | 0.1 to 60 | Swings a second |
| `smooth` | 0.5 | 0 to 1 | 0 a new brightness each picture, 1 a smooth wander |

#### `weave`

The picture wanders in the film gate, and jolts. Part: `always`.

| Field | Default | Range | What it is |
|---|---|---|---|
| `amount` | 1.5 | 0 to 50 | How far the picture wanders in the gate |
| `rotation` | 0 | 0 to 5 | How far it turns, degrees |
| `speed` | 1.5 | 0.05 to 20 | Wanders a second |
| `jump` | 0 | 0 to 200 | A sudden jolt's size (a splice, a bad frame) |
| `jump_rate` | 0 | 0 to 10 | Jolts a second |

#### `line_wobble`

Lines shift sideways: a worn tape's timing. Part: `tape`.

| Field | Default | Range | What it is |
|---|---|---|---|
| `amount` | 1.5 | 0 to 40 | How far lines shift sideways (a tape's timing error) |
| `wavelength` | 60 | 1 to 960 | The wave's length, in lines of a 480-line picture |
| `speed` | 1 | -20 to 20 | Waves a second rolling through |
| `jitter` | 0.3 | 0 to 1 | Each line's own random shift, against the smooth wave |

#### `tracking`

A VHS tracking band: torn lines and snow. Part: `tape`.

| Field | Default | Range | What it is |
|---|---|---|---|
| `amount` | 24 | 0 to 200 | How far lines in the band are torn sideways |
| `position` | 0.85 | 0 to 1 | The band's middle, from the top |
| `height` | 0.08 | 0.01 to 1 | The band's height, of the picture |
| `roll` | 0 | -5 to 5 | Picture heights a second the band rolls (0: it stays) |
| `noise` | 0.5 | 0 to 1 | Snow in the band |
| `duty` | 1 | 0 to 1 | The share of the time it shows: 1 always |
| `period` | 6 | 0.5 to 60 | Seconds between its comings and goings |

#### `head_switch`

The torn lines at the bottom of a VHS picture. Part: `tape`.

| Field | Default | Range | What it is |
|---|---|---|---|
| `height` | 0.02 | 0 to 0.2 | The torn band at the bottom, of the picture's height |
| `amount` | 16 | 0 to 200 | How far it tears sideways |
| `noise` | 0.3 | 0 to 1 | Snow in it |

#### `dropouts`

Short streaks where the tape lost its signal. Part: `tape`.

| Field | Default | Range | What it is |
|---|---|---|---|
| `rate` | 2 | 0 to 200 | Streaks a second |
| `length` | 0.15 | 0.01 to 1 | A streak's length, of the picture's width |
| `thickness` | 1 | 0.5 to 20 | Lines of a 480-line picture |
| `colour` | [255, 255, 255] | 0 to 255 | The streaks' colour |

#### `scanlines`

A TV's dark gaps between lines. Part: `tape`.

| Field | Default | Range | What it is |
|---|---|---|---|
| `amount` | 0.2 | 0 to 1 | How dark the gaps are |
| `lines` | 240 | 16 to 2160 | Lines from top to bottom |
| `thickness` | 0.5 | 0.05 to 0.95 | The dark share of each line |
| `roll` | 0 | -100 to 100 | Lines a second they creep |

#### `trails`

Motion leaves trails. Part: `always`.

| Field | Default | Range | What it is |
|---|---|---|---|
| `amount` | 0.5 | 0 to 0.98 | How much of the last picture stays: a slow tube's or a CCD's lag |
| `mode` | `"blend"` | `blend` `bright` | `blend` (everything lags) or `bright` (only lights leave trails) |

#### `grain`

Sensor noise or film grain. Part: `grain`.

| Field | Default | Range | What it is |
|---|---|---|---|
| `amount` | 0.04 | 0 to 1 | 0 none, 1 heavy |
| `size` | 1 | 0.25 to 10 | A grain's size (film: 2 or 3) |
| `colour` | 0 | 0 to 1 | 0 the same on every colour (a CCD's), 1 each colour its own (film, cheap tape) |
| `shadows` | 0 | 0 to 1 | More of it in the dark parts, as video noise is |

#### `dust`

Dust and scratches on film. Part: `grain`.

| Field | Default | Range | What it is |
|---|---|---|---|
| `amount` | 3 | 0 to 50 | Specks a picture |
| `size` | 1.5 | 0.5 to 20 | A speck's size |
| `scratches` | 0.1 | 0 to 1 | The chance of a scratch down the picture |
| `tone` | `"dark"` | `dark` `light` `both` | `dark` (dirt on the film), `light` (on a negative) or `both` |

#### `vignette`

Darker edges. Part: `always`.

| Field | Default | Range | What it is |
|---|---|---|---|
| `amount` | 0.35 | 0 to 1 | How dark the edges go |
| `radius` | 1 | 0.1 to 3 | Where it starts, in half picture widths from the middle |
| `softness` | 0.6 | 0.01 to 3 | How gradually |
| `roundness` | 1 | 0 to 1 | 1 a circle, 0 the picture's own shape |
| `colour` | [0, 0, 0] | 0 to 255 | What the edges go to |

#### `light_leak`

Light leaking into a film camera, a warm glow that wanders. Part: `always`.

| Field | Default | Range | What it is |
|---|---|---|---|
| `colour` | [255, 110, 40] | 0 to 255 | The leak's colour |
| `amount` | 0.5 | 0 to 3 | How bright |
| `size` | 0.8 | 0.05 to 3 | How big, in half picture widths |
| `speed` | 0.1 | 0 to 5 | How fast it wanders and pulses |
| `edge` | `"any"` | `any` `left` `right` `top` `bottom` | Where it comes in: `any`, `left`, `right`, `top` or `bottom` |

#### `gate`

A film gate's rounded corners. Part: `always`.

| Field | Default | Range | What it is |
|---|---|---|---|
| `radius` | 0.06 | 0 to 0.5 | The corners' rounding, of the picture's height (a film gate's) |
| `softness` | 0.01 | 0 to 0.2 | How soft its edge is |
| `inset` | 0 | 0 to 0.25 | How far in from the picture's edge |
| `colour` | [0, 0, 0] | 0 to 255 | Outside it |

#### `distort`

A cheap lens's barrel or pincushion. Part: `always`.

| Field | Default | Range | What it is |
|---|---|---|---|
| `amount` | 0.1 | -1 to 1 | Barrel (over 0: the middle bulges) or pincushion (under 0) |
| `cubic` | 0 | -1 to 1 | More of it toward the corners |
| `scale` | 1 | 0.25 to 4 | Zooms in (over 1) to hide the edges it pulls in |

#### `chromatic_aberration`

Colour fringes toward the corners. Part: `always`.

| Field | Default | Range | What it is |
|---|---|---|---|
| `amount` | 2 | -40 to 40 | How far red and blue part at the corners (a cheap lens's colour fringes) |

#### `channel_shift`

Red, green and blue out of register. Part: `tape`.

| Field | Default | Range | What it is |
|---|---|---|---|
| `red` | [2, 0] | -100 to 100 | Where red sits, [right, down] |
| `green` | [0, 0] | -100 to 100 | Where green sits |
| `blue` | [-2, 0] | -100 to 100 | Where blue sits |

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
- **Effects of your own code.** ReSkate mods are data, so a camera builds its picture from the
  library above, not from shaders of its own. If a look needs a block the library doesn't have,
  ask for it.
- **The viewfinder's layout and lettering.** A camera sets its readouts' colours, not where they
  sit or their font.

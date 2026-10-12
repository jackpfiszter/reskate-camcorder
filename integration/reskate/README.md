# Wiring the camcorder into ReSkate

> **Status: runs in the game.** `install.ps1` puts the files in
> `Extension/Camcorder/` into a ReSkate checkout and applies `reskate.patch` (the patch points
> below) against the ReSkate commit in `reskate.commit` (Steam build `25414733`, the `20260929`
> address table). CI builds the result with MSVC on every push (`.github/workflows/build.yml`);
> see `docs/TESTING.md` for how to try it.

## Why this lives inside ReSkate.dll

A mod in `Mods/` can only change game data; ReSkate switches custom Lua off ("nothing in that
folder is read or run"), and has no plugin API for native code. Moving the camera every frame
needs native code, so the camcorder has to be a ReSkate feature, built into `ReSkate.dll`
like the Trainer or Hall of Meat. That means either a ReSkate fork, or (better) a pull request
upstream. ReSkate is GPL-3.0, and so is this repository, so the code can go either way.

## Files

| Copy | To |
|---|---|
| `src/camcorder/*` | `Extension/Camcorder/Model/` (or keep `camcorder/` on the include path) |
| `integration/reskate/Extension/Camcorder/*` | `Extension/Camcorder/` |

## Patch points

1. **Build** (`cmake/Runtime.cmake`): add the `Extension/Camcorder/*.cpp` and model `.cpp`
   files next to the Hall of Meat list. ReSkate builds with `/W4 /WX`; the model already builds
   warning-free with `-Wall -Wextra -Wpedantic -Werror` on GCC and Clang.

2. **Client tick** (`Runtime/client_tick.cpp`): right after
   `multiplayer::tick_native_party_actions(...)`, with the same `ready` and `camera_phase`:

   ```cpp
   {
       DINGO_PROFILE_ZONE("tick/camcorder");
       dingosdk::camcorder_runtime::tick(r.base, client,
           r.spectate_ready && r.spectate_client == client && (state == 13 || state == 21), camera_phase);
   }
   ```

3. **Skater positions** (`Extension/Multiplayer/Session/session.cpp`): wherever
   `note_local_skater(...)` is called (solo and in a session), also call
   `camcorder_runtime::note_local_skater(...)`. In a session, after the remote players are placed,
   call `camcorder_runtime::note_players(...)` with each visible player's id, name and
   `render_pose.root`. This is the one genuinely new piece of plumbing.

4. **One camera lease.** `update_party_camera` keeps a single static lease. Spectate and the
   camcorder must never both drive it: Spectate should refuse (and say why) while
   `camcorder_runtime::owns_camera()`, and `camcorder power on` should refuse while spectating.
   Freecam / noclip already make the lease refuse ("Exit Freecam or Noclip...").

5. **Overlay** (`Extension/UI/Overlay/overlay_render.cpp`): call `draw_camcorder()` next to
   `draw_hall_of_meat()`, and treat `camcorder_pending()` like `hall_of_meat_pending()` so the
   overlay renders with the menu closed.

6. **Console** (`Extension/Console/commands.h` and its registration list): declare and call
   `register_camcorder_commands`.

7. **Bindings** (`Engine/Game/Input/controller_bindings.h`, `action_binds`): add switches so a
   player can put the camcorder on buttons, e.g.
   `{"camcorder_power", "camcorder", "Camcorder on / off", "camcorder power toggle"}`,
   `{"camcorder_rec", "rec", "Camcorder REC / STBY", "camcorder rec"}`,
   `{"camcorder_operate", "operate", "Camcorder: pad to camera", "camcorder operate toggle"}`,
   `{"camcorder_tripod", "tripod", "Camcorder: set up a tripod", "camcorder tripod place"}`,
   `{"camcorder_cut", "cut", "Camcorder: next tripod", "camcorder tripod next"}`.

   (The patch has the full list: steer while filming and take a still too.)

8. **Menu** (`Extension/UI/Overlay/skate_menu.cpp`): a CAMCORDER page that only sends
   `camcorder ...` commands, as the Trainer page does.

9. **Recording** (`overlay_render.cpp`): the back buffer is copied for clips and stills in the
   present path, before the readouts are drawn (`camcorder_capture.cpp`); Media Foundation and
   process-loopback audio are linked in `cmake/UI.cmake`.

10. **Steer while filming** (`Extension/UI/Overlay/input_capture.cpp`, `overlay_input.cpp`): a
    partial hold beside Freecam's full one. `DingoSDKOverlaySetCameraAimCapture` keeps the right
    stick, the triggers and the mouse from the game (XInput, PlayStation HID reports, raw mouse
    records, mouse messages, the cursor) and leaves it the keyboard, the left stick and the
    buttons; `DingoSDKOverlayReadMouseWheel` gives the camera the wheel.

## To confirm in play

- **Look units.** `read_controls` assumes the freecam's look deltas are .0025 rad each, as
  `step_free_flight` uses them. Check a controller and a mouse both feel right.
- **On board or on foot.** `on_board` reads No Bail's physics-state watch (500 to 599 on
  foot); check the shake switches from wheels to footsteps when walking.
- **Ground under a tripod.** `ground_under_tripod` asks `local_ground_height` from 2.5 m above
  the feet down 40 m while it is being set up; check ledges, stairs and rails.
- **FOV range.** The lease writes the FOV into the free camera (`first_person_write_fov`,
  accepted between 1 and 175 degrees). The fisheye asks for about 94 degrees vertical and full
  tele about 3.5; check the game draws both cleanly (LOD and culling at tele, near-plane at wide).
- **A remote player's root is their feet.** The model aims 0.8 m above it; adjust if the
  pose root sits elsewhere.
- **Pausing and loading.** `tick` stops a recording and gives the camera back when `ready`
  drops (loading, menus); check that matches what players expect.

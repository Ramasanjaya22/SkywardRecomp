# Keyboard + mouse support: design

Status: draft, awaiting approval. Nothing is built yet.

## Intent
This is a PC recomp, so it should play fully with keyboard and mouse, start to finish, not just be "technically playable".
The layout is modern-PC style: mouse = camera and sword hand, WASD = movement, one action per key. It is not the Wii
controls forced onto a keyboard.

## State today (verified in the code)
- No mouse input reaches the game. The mouse only drives the ImGui menus and the cursor hide/show
  (`runtime/hle/input.c` `input_cursor_event`). No "Mouse Camera / Mouse Combat" exists in this repo.
- Keyboard goes through `g_bind_key` (`runtime/hle/bindings.c`) into `key_buttons` / `key_r*` and then `fill_state`
  (`runtime/hle/input.c`), which builds the Npad state.
- Free Camera (`input.c` ~836): right-stick motion synthesizes L; a short L tap toggles it; `free_cam_sens` scales it.
- `zr_attack` (`runtime/hle/zr_tap.h`): a ZR tap becomes a right-stick click (the game's own jab / thrust).
- The default keyboard preset collides with the layout below (E = R, Q = L, F = left-stick click, K/J/L/I = A/B/X/Y).

## Goals
- Mouse moves the camera (through the existing free-camera path).
- LMB slashes in the direction the mouse was moving. MMB thrusts. RMB targets and shields. Hold M4 to aim items
  (bow, slingshot, beetle) with the mouse, then LMB fires.
- A new keyboard preset "KB+M": W/A/S/D move, Shift sprint, Space jump / context action, E interact, F item action,
  Q free-cam toggle (existing L tap), R secondary utility, Tab inventory, M map, Esc pause.
- Settings `mouse_camera`, `mouse_combat`, `mouse_sens` in the Controls tab and `skywardrecomp.ini`. Mouse buttons are
  rebindable through the existing "click a binding, then press" UI.
- Menus keep working with a free cursor. A scripted test run ignores the real mouse, as it does for the keyboard today.

## Non-goals
- No gyro or Wii-pointer emulation. Sword direction is the button-only scheme (right stick).
- No chord bindings (no Ctrl+RMB, Shift+E and so on). One action per key, by design.
- No Q reassignment: Q stays free-cam toggle.

## Constraints
- One virtual right stick serves camera and sword, so they must never fire at the same time.
- Changes stay in `runtime/hle/input.c`, `runtime/hle/bindings.c`, `runtime/input_bind.h`, `runtime/settings.*` and
  `runtime/ui/controls_ui.mm`. The game's own state layout is untouched.
- Every new input path is testable headless, in the style of `tools/test_zr_tap.c`.

## Approaches considered
1. **Mouse as a virtual right stick inside `fill_state` (chosen).** Relative motion is accumulated and turned into
   `rx/ry` while the camera is active. On LMB the recent motion direction becomes a short stick flick with L withheld.
   It reuses free-cam, the bindings and the tests, and touches the fewest files.
2. **Hook the game's own motion/pointer HLE.** This would be closest to the Wii feel, but it needs reverse engineering
   of the motion path and is much riskier. Rejected.
3. **A separate mouse layer outside the input table.** It would duplicate the binding and menu-capture logic.
   Rejected.

## Design (chosen)
- **Capture:** relative mouse mode during gameplay and a free cursor in menus. This follows `menu_input_captured()`
  and the cursor hide/show that already exists.
- **Camera:** mouse delta × `mouse_sens` is accumulated into the virtual `rx/ry` each poll. The existing free-cam
  synthesis then adds L.
- **Slash (LMB down):** take the mouse motion of the last ~100 ms and snap it to 8 directions. If there is none, reuse
  the last direction, defaulting to vertical. Hold a full-deflection `rx/ry` for 3–4 polls with L withheld, and pause the
  camera accumulation for the swing.
- **Thrust (MMB):** pulse BTN_RSTICK, the same way `zr_tap` does.
- **RMB:** while held, ZL (target) and R (shield) are both pressed. (Decided with the user.)
- **M4 (aim item):** while held, the mouse drives `rx/ry` as an analog aim, camera synthesis is suppressed, and LMB maps
  to the item-use button.
- **Preset:** `bind_key_preset(KBM)` fills `g_bind_key`. The Controls UI offers "Keyboard + Mouse" next to Reset.
  The existing "Keyboard" default stays so current users are unchanged.

## Left to the builder (verify in game first)
- Which Switch button each action really is (dash, jump/context, interact, item action, secondary utility). The repo
  does not say. Check the in-game controls page and map the slots to match.
- The flick length and speed that reliably triggers a swing in button-only mode. This is the main risk. Tune with
  the `LARECOMP_HOST_INPUT mouse:` harness plus manual play.
- Defaults for `mouse_sens` and the direction-window length.
- Whether M4 aim needs its own deadzone or response curve per item.

## Tests
- A pure-C state machine (`runtime/hle/mouse_sword.h`, header-only like `zr_tap.h`) with `tools/test_mouse_sword.c` and
  a `.py` wrapper wired into CI next to `test_zr_tap`.
- Settings round-trip in `tools/test_settings.py` for the three new keys.
- Manual: slash in 8 directions, thrust, shield and target, bow / slingshot / beetle, menus, rebinding.

## Button map (Task 1) — PROVISIONAL, UNVERIFIED in game
No game files here, so this is a best guess; Task 7 (human in game) must confirm. Only `k_kbm_key` (runtime/kbm_preset.h) and the use-item slot change if it is wrong.

| Key / mouse | Action | Switch slot |
|---|---|---|
| W/A/S/D | move | BIND_LS_UP/LEFT/DOWN/RIGHT |
| Q | free-camera toggle (short L tap, existing) | BIND_L |
| E | interact / context | BIND_A |
| Shift | sprint | BIND_B |
| Space | jump | BIND_X |
| F | item action | BIND_Y |
| R | item wheel (ZR) | BIND_ZR |
| Enter | pause (BIND_PLUS) — Esc is consumed by the overlay menu (menu.mm:176), which opens it instead | BIND_PLUS |
| M | map | BIND_MINUS |
| Tab | inventory | BIND_DOWN |
| RMB | target + shield | ZL + R (not rebindable) |
| LMB while aiming (M4) | use / fire item | BIND_Y (same as F) |
| MMB | thrust | BIND_RSTICK pulse |
All other keyboard slots are unbound (0) in the preset. Aiming (M4) is analog on the virtual right stick; whether ZL must also be held is verified in Task 7.

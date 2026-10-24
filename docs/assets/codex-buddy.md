<p align="right">
  <a href="codex-buddy.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Codex Buddy Passport notes

This note records the first Passport port of [qinyu/codex-buddy](https://github.com/qinyu/codex-buddy). It is fork product text, not an upstream BSP contract.

## What was ported

- Nordic UART service `6e400001-b5a3-f393-e0a9-e50e24dcca9e`, advertised as `Codex-` plus two MAC bytes.
- LE Secure Connections, display-only passkey, and an `unpair` command that clears bonds.
- Usage packet fields the Stick firmware already accepts: `state`, `tokens`, `primary`, `secondary`, `primary_resets_at`, `secondary_resets_at`, `primary_label`, `secondary_label`, `now`, and `agent`.
- Separate `prompt` object (`id`, `tool`, `hint`) and `{"prompt":null}`.
- Device reply `{"cmd":"permission","id":"...","decision":"accept"|"cancel"}`.
- Work poses `sleep`, `idle`, `busy`, `attention`, `completed`, `dizzy`, and `heart`.
- Bar colors and reset urgency from the Stick dashboard.
- A local hook bridge. `PermissionRequest` waits on a Unix socket and fails open.

The host model follows the Stick plugin: Codex hooks start a local process, and that process talks BLE. This port does not use an app-server TUI proxy as the companion path. The Passport has no terminal UI, and the Stick product Yu uses is the hook bridge.

## What was redesigned

The screen is a new 240×320 layout in `main/codex_ui.c`. It does not use the demo menu, `demo_*.c` pages, or the `ui_pixel` shell. The pet is the existing Pi character from the Stick pack, drawn from generated frames in `assets/images/pi_pet.c`. The locked dashboard is the vertical stack: a status and battery bar, one large pose title, a half-screen Pi sprite, then thick 5-hour and 7-day bars that each carry a countdown. Info and settings are single-column rows about 34 px tall so eight settings fit on one screen. Key hints sit in the right gutter, top to bottom, beside the physical Up, Down, and OK keys. Local Pi and Pi through Herdr share that sprite. The about page says which one is current. There is no IMU, so dizzy and heart are poses the host can send; heart also plays for 1.8 seconds after a local allow. Up and Down on the usage page cycle the pose in place of the Stick shake.

Main pages are usage and info. A short OK press toggles them. Up and Down step through Info (About, Buttons, Device, Bluetooth, Credits) and, on usage, change the pose. A long OK press, recognized on release at 800 ms or later, opens settings. The same long press inside settings returns to usage. Two OK shorts inside 400 ms return to usage from anywhere, including settings. A short press is under 400 ms. The power key is not a UI key. Settings stores brightness (four levels), the sound switch, and the BLE radio switch in NVS. Wi-Fi, the LED, and clock rotation are shown as not available. Short OK on Unpair or Reset runs that action and returns to usage. Down moves away without running it. Reset clears only the `codex` preference namespace. While a prompt is open, Up allows, Down refuses, and short OK runs the highlighted choice. Short OK or Down closes the passkey card. The backlight still drops after 45 seconds without a key, and the next key only wakes the screen.

When the sound switch is on, the ES8311 plays distinct cues: a tick, a page glide, a pose glide, a repeating approval call, a low failure pair, a rising success, a falling deny, and a rare low-battery triple.

Fixed Chinese strings use `assets/fonts/codex_font_16.c` and `assets/fonts/codex_font_20.c`. Widgets select those fonts directly. `tests/test_codex_font_coverage.py` checks every printable character in the UI sources, and checks that U+9F98 is absent so the test can fail.

## What is deferred

- Folder push, LittleFS character packs, and pets other than the Pi frames.
- Stick IMU gestures, nap time, level-up, and the multi-agent hub for Hermes and Cursor.
- Tertiary provider meters and the OpenCodex quota client.
- Wi-Fi, the status LED, and clock rotation. The settings list marks them unavailable. Sound cues are implemented.

`main/demo_*.c` remains in the tree so the existing baseline host tests still compile. The firmware CMake file does not link those pages, and `app_main` never opens them.

## Packet example

```json
{"state":"busy","tokens":1800,"primary":42,"secondary":15,"primary_resets_at":1775731234,"secondary_resets_at":1776331234,"primary_label":"5h","secondary_label":"7d","now":1775700000,"agent":"CODEX"}
```

```json
{"prompt":{"id":"h1","tool":"Bash","hint":"echo hi"}}
```

A link with no fresh line for 30 seconds is marked stale. The bridge rolls a reset timestamp forward by the 5-hour or 7-day window when the stored epoch is already in the past.

## Security

RX requires encrypted write. TX notify requires an encrypted subscription. The status ack reports `"sec":true` only after the link is encrypted and authenticated. Tool hints still travel over this link, so an unpaired or failed pairing must not be treated as private. Bonds live in NVS. A merged image flashed at `0x0` can erase that partition.

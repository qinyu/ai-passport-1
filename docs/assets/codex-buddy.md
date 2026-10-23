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

The screen is a new 240×320 layout in `main/codex_ui.c`. It does not use the demo menu, `demo_*.c` pages, or the `ui_pixel` shell. The pet is a small LVGL figure in `main/codex_pet.c`, not a GIF. Spacing, type size, and the dashboard composition are provisional until a layout direction is chosen. There is no IMU, so dizzy and heart are poses the host can send; heart also plays for 1.8 seconds after a local allow.

Main pages are Dashboard, Pet, and Info. OK steps through them. Up and Down step through Info (About, Buttons, Device, Bluetooth, Credits). A long OK press opens or closes Settings. Settings stores brightness (four levels) and the BLE radio switch in NVS. Sound, Wi-Fi, the LED, and clock rotation are shown as not available. Unpair and Reset each ask for a second OK. Reset clears only the `codex` preference namespace. Down cancels a confirmation. While a prompt is open, Up still allows and Down still refuses. The passkey card still covers the page. The backlight still drops after 45 seconds without a key, and the next key only wakes the screen.

Fixed Chinese strings use `assets/fonts/codex_font_16.c` and `assets/fonts/codex_font_20.c`. Widgets select those fonts directly. `tests/test_codex_font_coverage.py` checks every printable character in the UI sources, and checks that U+9F98 is absent so the test can fail.

## What is deferred

- GIF pets, folder push, and LittleFS character packs.
- Stick IMU gestures, nap time, level-up, and the multi-agent hub for Pi, Hermes, and Cursor.
- Tertiary provider meters and the OpenCodex quota client.
- A locked visual layout. Info and Settings are placeholder lists until a 240×320 direction is chosen.
- Audio, Wi-Fi, the status LED, and clock rotation. The settings list marks them unavailable instead of inventing behavior.

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

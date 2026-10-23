<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Codex Buddy for AI Passport

This branch is a first Passport build of the Codex Buddy companion. It runs on the FoloToy AI Passport (ESP32-C3, 8 MB flash, no PSRAM, 240×320, three ADC keys) and replaces the hardware-test menu with its own screen.

The device shows Codex 5-hour and 7-day usage, reset countdowns, and a live work pose. A computer-side Codex plugin starts a local BLE bridge. Permission prompts can be allowed or refused on the device. Nothing in that path is sent to an extra server.

The behavior is ported from [qinyu/codex-buddy](https://github.com/qinyu/codex-buddy). The screen, buttons, and pet are redesigned for Passport. Details, the packet format, and what is still missing are in [docs/assets/codex-buddy.md](docs/assets/codex-buddy.md).

## Screen

Portrait dashboard on the navy screen:

- Link state, work pose, and a drawn pet.
- `5h` and `7d` bars. Fill color is blue through 34%, green through 69%, and orange after that.
- Reset countdown under each bar. `5h` turns orange at 3 hours and red at 1 hour. `7d` turns orange at 4 days and red at 2 days.
- Battery percent in the top-right. An unavailable gauge shows `--`.
- Approval card with the tool name and hint. Passkey card during pairing.

## Keys

| Key | Action |
| --- | --- |
| Up | Previous info or settings row. Allows a pending prompt. |
| Down | Next row, or cancels a confirmation. Refuses a pending prompt. |
| OK | Next main page: dashboard, pet, info. Inside settings, changes the selected item. |
| OK long-press | Open or close settings |

Info covers the name, buttons, battery, address, link security, the six-digit pairing code, and attribution. Settings stores brightness and the BLE switch. Sound, Wi-Fi, the LED, and clock rotation read as not available. The screen arrangement is provisional. The first press after the backlight has dimmed only wakes it. A permission prompt or pairing code keeps the backlight up. With no prompt and no key for 45 seconds, the backlight dims.

## Host path

Install Python `bleak`, then trust the plugin in `plugins/codex-usage-passport`. Hooks `SessionStart`, `UserPromptSubmit`, and `PermissionRequest` start `scripts/hook_entry.py`. That process keeps one bridge alive. The bridge reads recent Codex `token_count` rows from the local rollout log and writes one JSON line to the Nordic UART RX characteristic. If the bridge is down, a permission hook exits without a decision so Codex can use its normal prompt.

Manual packet without a rollout log:

```bash
python3 tools/codex_usage_ble_bridge.py --manual --state busy --primary 40 --secondary 12 --print-packet
```

The device name is `Codex-` plus the last two MAC bytes. The link uses LE Secure Connections with a 6-digit passkey shown on the screen. Bonds are stored in NVS.

## Build

Use ESP-IDF 5.5.3 and `./tools/validate.sh`. Flash the merged image at `0x0`:

```text
build/FoloToy-AI-Passport-full.bin
```

A merged flash can reset NVS, including BLE bonds. Do not flash the application-only image at `0x0`.

## Chinese text

Fixed UI strings use generated 16 px and 20 px subsets of WenQuanYi Micro Hei. Montserrat is not used for those labels. The character list, license, and converter command are in [assets/README.md](assets/README.md). Prompt hints outside that list can show a missing-glyph box; the bridge keeps tool text short and ASCII.

# Keychron protocol findings (Phase 0)

Status legend: **[HW]** verified on the real device · **[SRC]** verified against firmware source only · **[UNTESTED]**

Firmware source used: `Keychron/qmk_firmware`, branch `hall_effect_playground`
(`keyboards/keychron/k10_he`, `keyboards/keychron/common`, `quantum/via.c`), fetched 2026-11-10.

## Devices on this machine

| Device | VID:PID | Notes |
|---|---|---|
| Keychron Link (2.4 GHz receiver) | `3434:d030` | Currently attached. 4 HID interfaces. **[HW]** |
| Keychron K10 HE ANSI (wired) | `3434:0ea0` | VIA interface = 2nd hidraw (usage page `0xFF60`). FW `v1.1.1 2025-06-17`, VIA protocol `0x000C`, Keychron protocol `0x02`. **[HW]** |
| Keychron K10 HE ISO | `3434:0ea1` | Same firmware, ISO layout. **[SRC]** |

### Keychron Link interfaces **[HW]**

| hidraw | Interface | Report descriptor summary |
|---|---|---|
| hidraw2 | input0 | Mouse (report ID 3) |
| hidraw4 | input1 | Vendor page `0x8C`: input `0xB1` (32 B), output `0xB2` (32 B), features `0x51` (20 B), `0x52` (64 B), `0x53` (1000 B), `0x54` (20 B in); plus Joystick (ID 4). Undocumented, probably receiver config/DFU. Not used. |
| hidraw5 | input2 | Keyboard, consumer, system control, NKRO |
| hidraw6 | input3 | **VIA raw HID**: usage page `0xFF60`, usage `0x61`, 32 B in/out, no report ID |

Existing udev rules (packages `keychron-link-udev`, `keychron-k10-he-udev`, plus
`/etc/udev/rules.d/99-keychron-hid.rules`) already grant user access to all `3434:*` hidraw nodes.

## Raw HID over the Link: no response **[HW]**

Read-only VIA queries sent to hidraw6 (both with and without the leading `0x00` report-ID byte):
`0x01` protocol version, `0x02 0x01` uptime, `0x08 0x03 0x0{1..4}` RGB matrix get, and Keychron `0xA0/0xA1/0xA2`.
**All timed out (1 s).**

Explanation from source **[SRC]**: the K10 HE talks to its radio module (LKBT51) over SPI. The module's
command/event set (`lkbt51.c`) contains HID report forwarding (keyboard, NKRO, consumer, mouse, joystick,
xinput), pairing, battery level, and DFU. **It has no event that carries host-to-keyboard raw HID/VIA data.**
So VIA commands sent to the receiver never reach the keyboard MCU. Keychron Launcher's proprietary
firmware or receiver could differ. Nothing here assumes it does.

## VIA lighting commands over USB cable **[HW]**

Packet: 32 bytes, write as `00` + 32 bytes on hidraw.

| Purpose | Bytes | EEPROM? |
|---|---|---|
| Get value | `08 03 <id>` → reply `08 03 <id> <v...>` | no |
| Set value | `07 03 <id> <v...>` | **no**: uses `*_noeeprom` (`quantum/via.c:693-711`) |
| Save | `09 03` | **yes**: `eeconfig_update_rgb_matrix()` |

Value IDs (channel 3 = `id_qmk_rgb_matrix_channel`): `01` brightness (0-255, scaled to max), `02` effect
(0 = off), `03` speed, `04` color `<hue> <sat>`.

Hardware results (2026-11-10, cable connected while keyboard in 2.4 GHz mode):

- Read: `08 03 01` → `08 03 01 ff` (brightness 255), effect `01`, speed `0f`, color `00 ff`. **[HW]**
- Write: `07 03 02 01`, `07 03 04 aa ff`, `07 03 01 c8`, `07 03 03 64`, each echoed back as ack.
  **4 commands applied in 11 ms.** Readback matched (brightness 200 reads as 199: scaling rounding). **[HW]**
- **Brightness reads back one lower than written** for every value 1–255 (sweep: 255→254, 204→203, 1→0).
  The app treats the read-back value as the truth and writes `ceil(R·256/255)` to land on R exactly. 255 itself
  is unreachable via VIA (max read-back 254). The keyboard's own keys and EEPROM can hold 255. **[HW]**
- **Changing the effect reloads color/brightness/speed from the saved settings**, about 10–15 ms after the
  effect command. Values written in that window are acknowledged but lost. Measured: delay 0–10 ms → 0–1/5 kept,
  15/20/30/50 ms → 5/5 kept. Setting the *same* effect again does not trigger the reload. The app sends the
  effect only if it differs, waits 40 ms, writes the rest, then verifies by read-back (one retry). **[HW]**
- Resulting apply times on hardware: 22–31 ms without an effect change, 65–67 ms with one. **[HW]**
- Unknown command (`FE`) → reply starts `ff` (`id_unhandled`). **[HW]**
- `09 03` (save): **not executed on hardware** (deliberately, to avoid flash writes during testing). **[SRC]**
- Keychron `A0` → `a0 02` (protocol 2); `A1` → ASCII `v1.1.1 2025-06-17-10:45:33`; `A2` → feature byte
  `0x8f` at offset 2 (default layer, BT, 2.4G, analog matrix, plus bit 7 which is not in the public source). **[HW]**

### Effect IDs (K10 HE, `via_json/k10_he_ansi.json`, cross-checked with `info.json`) **[SRC]**, `1` verified **[HW]**

0 None · 1 Solid Color · 2 Breathing · 3 Band Spiral Val · 4 Cycle All · 5 Cycle Left Right ·
6 Cycle Up Down · 7 Rainbow Moving Chevron · 8 Cycle Out In · 9 Cycle Out In Dual · 10 Cycle Pinwheel ·
11 Cycle Spiral · 12 Dual Beacon · 13 Rainbow Beacon · 14 Jellybean Raindrops · 15 Pixel Rain ·
16 Typing Heatmap · 17 Digital Rain · 18 Reactive Simple · 19 Reactive Multiwide ·
20 Reactive Multinexus · 21 Splash · 22 Solid Splash

Speed range 0–255. Single HSV color only (no secondary color in VIA).

Per-key colors: not available through VIA itself, but through Keychron's own RGB protocol (next section).

## Keychron RGB protocol: per-key colors **[HW]**

Source: branch `2025q3`, `keyboards/keychron/common/rgb/keychron_rgb.c` (the K10 HE build there sets
`KEYCHRON_RGB_ENABLE`). The older `hall_effect_playground` branch does not have it. The shipped firmware v1.1.1
does: the `A2` feature byte `0x8f` has bit 7 = `FEATURE_KEYCHRON_RGB`.

Request `A8 <sub> args…`, reply `A8 <sub> <status 0 = ok> payload…`.

| Sub | Purpose | Verified on K10 HE |
|---|---|---|
| `01` | protocol version | `01 00` → v1 **[HW]** |
| `02` | save everything below to EEPROM | **never sent** except by an explicit "Save to keyboard" **[SRC]** |
| `05` | LED count | `0x68` = 104 **[HW]** |
| `06 row m0 m1 m2` | LED index per column of `row` (`ff` = none) | rows 0–5, 21 columns **[HW]** |
| `07` / `08` | get/set per-key render type (0 solid, 1 breathing, 2–4 reactive) | type 0 **[HW]** (get) |
| `09 start n` | read `n ≤ 9` LEDs as `h s v` | **[HW]** |
| `0A start n h s v…` | write `n ≤ 9` LEDs (RAM only) | **[HW]** |
| `0B` | mixed-RGB info | 2 layers × 5 effects **[HW]** |

- Firmware quirk: `06` accepts `row == MATRIX_ROWS` (checks `row > MATRIX_ROWS`) and returns garbage for it,
  and unused columns read as `00`, not `ff`. The app stops at the first failing row, drops the last successful one,
  and keeps the first position seen per LED. **[HW]**
- The per-key table is shown by **VIA effect 23 (`PER_KEY_RGB`)**; 24 is `MIXED_RGB`. Verified by setting 23
  and reading it back. **[HW]**
- Solid per-key mode uses each LED's **hue and saturation** and the **global** VIA brightness; the per-LED `v` is
  ignored. So gradients can go to white (saturation 0) but not to black. **[SRC]**, consistent with **[HW]**
- The effect-change reload (see above) does **not** reset the per-key table: colors written before switching
  to effect 23 survive. **[HW]**
- Timing: a full 104-LED frame = 12 packets ≈ 33 ms; 30 frames at 10 fps stayed at 33–35 ms per frame. **[HW]**
- The app writes the table first, then switches to effect 23 (no flash of old colors), captures the original
  table on connect and writes it back on exit. Scrolling sends one packet per event-loop turn, skips unchanged
  packets, and used ~2% of one CPU core while scrolling. **[HW]**
- Other programs that have the hidraw device open receive the same replies. The app drains stale reports before
  each request and matches the full command header (VIA get/set: 3 bytes, `A8`: 2 bytes). Verified by reading
  the keyboard from a second process while the app animates. **[HW]**

## Battery **[SRC]**

- The keyboard pushes its battery level to the radio module (`LKBT51_CMD_UPDATE_BAT_LVL 0x32`,
  `LKBT51_CMD_UPDATE_BAT_STATE 0x33`).
- **Bluetooth:** the module presumably exposes this as the BLE Battery Service, so BlueZ `org.bluez.Battery1`
  and UPower would show it. **[UNTESTED]** (no BT pairing on this host)
- **2.4 GHz Link:** the receiver may receive the level, but no documented host-side query exists.
  Not visible in UPower. **Unavailable.** **[HW]**
- **USB cable:** no raw HID battery command in `keychron_raw_hid.c` (`0xA0–0xAB` only). **Unavailable.** **[SRC]**

## Hardware test log (2026-11-10, K10 HE via USB cable, keyboard otherwise on 2.4 GHz)

| What | Result |
|---|---|
| Discovery, VIA probe, firmware/protocol query | ✓ **[HW]** |
| Apply profile (same effect / effect change), read-back exact | ✓ **[HW]** |
| Restore original lighting on quit: D-Bus `quit` and SIGTERM (exit 0, exact read-back) | ✓ **[HW]** |
| CLI over D-Bus: `set-profile`, `restore-default`, `list-profiles`, `status`, unknown profile → exit 1 | ✓ **[HW]** |
| Auto-switch via fake processes (`Wow.exe` name match, `reaper … AppId=1245620`), newest-wins, revert on exit | ✓ **[HW]** |
| Real non-Steam shortcut (Battle.net launcher, AppId 3259692658) ignored by the fallback | ✓ **[HW]** |
| Static and scrolling gradients (per-key), read-back of the table, restore of original table on quit | ✓ **[HW]** |
| App keeps working while another process uses the same hidraw device | ✓ **[HW]** |
| `09 03` / `A8 02` save to EEPROM | **not executed** (mock only) |
| Battery via UPower/BlueZ | **untested**: no Bluetooth pairing; unavailable over cable/2.4 GHz (shown as such) |
| Low-battery notification, sleep/resume, USB hotplug | mock/unit-tested logic only; not exercised on hardware |
| Real game launches (Proton/Steam) | not exercised; detection logic verified with fake processes and live `/proc` |

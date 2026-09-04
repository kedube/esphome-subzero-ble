# ESPHome Sub-Zero BLE

Connect Sub-Zero, Wolf, and Cove kitchen appliances to Home Assistant over Bluetooth Low Energy, using an ESP32 running [ESPHome](https://esphome.io). Fully local, no cloud.

> **Alpha software.** It works for the appliances listed under [Tested Appliances](#tested-appliances), but coverage is limited to what contributors own. Reports from other models are welcome.

## Contents

- [How it works](#how-it-works)
- [Requirements](#requirements)
- [Getting started](#getting-started)
- [Appliance options](#appliance-options)
- [Entities](#entities)
- [Troubleshooting](#troubleshooting)
- [Tested appliances](#tested-appliances)
- [Further reading](#further-reading)

## How it works

Sub-Zero Group appliances carry a BLE radio that the official *Sub-Zero Group Owner* app uses for setup and monitoring. This project reimplements that protocol as a native ESPHome component: the ESP32 bonds with each appliance using its 6-digit PIN, polls it once a minute, and receives push notifications for changes in between. Each appliance becomes its own sub-device in Home Assistant with sensors, a handful of writable controls, and diagnostic buttons.

The protocol was reverse-engineered from the Android app and live BLE captures; see the [BLE protocol reference](docs/ble-protocol.md). The JSON parser and connection state machine are plain C++ with a host-side test suite, see [Advanced usage](docs/advanced.md).

There is also [eyal0/subzero-ble-homeassistant](https://github.com/eyal0/subzero-ble-homeassistant), a Home Assistant custom integration for the same protocol. It needs a Bluetooth adapter on the Home Assistant host itself; Bluetooth proxies are not supported there.

## Requirements

- An **ESP32-S3** board. The shipped `subzero.yaml` targets `esp32-s3-devkitc-1` with PSRAM, which handles five concurrent appliances comfortably. Other ESP32 variants work with fewer appliances; edit the `esp32:` block accordingly.
- **ESPHome 2026.7.1 or newer.** This is a hard floor: it is the first ESPHome built on ESP-IDF 5.5.5, which fixes a Bluedroid bug that dropped BLE fragments from firmware 8.5 appliances.
- The **6-digit PIN** from each appliance, entered once during pairing.

## Getting started

### 1. Install ESPHome

```bash
python3 -m venv esphome-venv
source esphome-venv/bin/activate
pip install esphome
```

### 2. Configure

Clone this repository. `subzero.yaml` is the complete gateway configuration and never needs editing for a normal setup. It pulls the component from GitHub at compile time and includes two files you create from the examples:

```bash
cp settings-example.yaml settings.yaml
cp secrets-example.yaml secrets.yaml
```

- **`settings.yaml`** is your appliance inventory: one `ble_client` and one `subzero_appliance` entry per appliance, with its MAC address, PIN, name, and options. The example lists three fridges, a range, and a dishwasher; delete what you do not have. Set `type:` to `fridge` (also wine storage, beverage centers, and freezers), `range` (also wall ovens), or `dishwasher`.
- **`secrets.yaml`** holds Wi-Fi, API encryption, OTA, and fallback access point credentials.

Sub-Zero MAC addresses begin with `00:06:80`. A BLE scanner app on Android shows them; iOS hides MAC addresses. On macOS, connect with a scanner and run `system_profiler SPBluetoothDataType`.

When running several appliances on one ESP32, give each a different `poll_offset` (`0s`, `5s`, `10s`, and so on) so their bonding sequences do not collide on the shared radio. The example already does this.

### 3. Flash

```bash
esphome run subzero.yaml --device /dev/cu.usbserial-XXXX   # first flash over USB
esphome run subzero.yaml --device 192.168.x.x              # later, over the air
esphome logs subzero.yaml --device 192.168.x.x             # follow the log
```

### 4. Pair

Each appliance needs its PIN once. Either read it from the official app during its own pairing flow, then delete the phone's Bluetooth pairing so the ESP32 can take the slot, or, after the ESP32 has connected and bonded, press **Start Pairing** in Home Assistant and read the PIN from the appliance display. The display must be awake: if it is inside the compartment, open the door first.

Enter the PIN in the appliance's **PIN** text field in Home Assistant, or put it in `settings.yaml` and reflash. **Status** reads `Connected and polling.` once everything is up.

## Appliance options

All options go on the `subzero_appliance` entry in `settings.yaml`. `name`, `pin`, `type`, `id`, and `ble_client_id` are required.

| Option | Default | Effect |
|---|---|---|
| `poll_interval` | `60s` | How often to poll. Lengthen to `120s` or `300s` on a memory-constrained board. |
| `poll_offset` | `0s` | Staggers this appliance's connection sequence relative to the others. |
| `enable_temp_control` | `false` | Fridge: makes the setpoints writable. See [Fridge write support](#fridge-write-support). |
| `enable_mode_selects` | `false` | Fridge: adds mode selects and a writable Air Filter switch. |

Hidden entities are left out of the firmware entirely, which saves RAM. Defaults match a typical fridge-freezer with ice maker; flip the ones your model needs.

| Option | Default | Hides |
|---|---|---|
| `hide_freezer` | `false` | Freezer Set Temperature, Freezer Door |
| `hide_ice_maker` | `false` | Ice Maker, and the Ice Maker Mode select |
| `hide_sabbath` | `false` | Sabbath Mode |
| `hide_fridge_zone` | `false` | Set Temperature, Door. Set `true` on wine-only and freezer-only units. |
| `hide_wine` | `true` | Wine Door, Wine Zone Upper and Lower Set Temperature, Wine Temperature Alert |
| `hide_ref_drawer` | `true` | Refrigerator Drawer Set Temperature and Door |
| `hide_crisper` | `true` | Crisper Drawer Set Temperature |
| `hide_air_filter` | `true` | Air Filter, Air Filter Remaining |
| `hide_air_filter_extra` | `true` | Air Filter Expires |
| `hide_water_filter` | `true` | Water Filter Remaining (%) |
| `hide_water_filter_extra` | `true` | Water Filter Gallons Remaining, Water Filter Expires |
| `hide_vacation_ice_modes` | `true` | Long and Short Vacation, High Usage, Max Ice, Night Ice modes, and the Appliance Mode and Night Mode selects |
| `hide_extra_diagnostics` | `true` | Smart Grid Mode, Pairing Window Open, Door Ajar Alarm Timeout, the appliance's own Wi-Fi details, Active Faults, and the Humidity Control select |
| `hide_softener` | `true` | Water Softener Low (dishwasher) |
| `hide_oven2` | `true` | Every Oven 2 entity (range) |

## Entities

Every appliance gets a **Status** text sensor, a **PIN** text field, a **Debug Mode** switch, and buttons for Connect, Disconnect, Start Pairing, Submit PIN & Unlock, Poll, Log Debug Info, Reset Pairing, and Clear Cloud Token (BT-Only). The sensors below vary by type.

### Refrigerator, freezer, wine storage

Fridge firmware exposes set points only. Measured compartment temperatures are not available over BLE.

| Entity | Notes |
|---|---|
| Set Temperature, Freezer Set Temperature | °F. Read-only unless `enable_temp_control`. |
| Refrigerator Door, Freezer Door | Binary sensors |
| Ice Maker | On or off |
| Sabbath Mode | On or off |
| Wine Door, Wine Zone Upper and Lower Set Temperature, Wine Temperature Alert | `hide_wine: false` |
| Refrigerator Drawer Set Temperature and Door | `hide_ref_drawer: false`. On models with one door switch, the drawer mirrors the main door. |
| Crisper Drawer Set Temperature | `hide_crisper: false` |
| Air Filter, Air Filter Remaining, Air Filter Expires | This is the appliance's Air Purifier toggle. Writable with `enable_mode_selects`. |
| Water Filter Remaining, Gallons Remaining, Expires | Not every model reports all three. |
| Long and Short Vacation, High Usage, Max Ice, Night Ice | Mode flags, with start and end times where the appliance reports them. |
| Power On, Service Required, Smart Grid Mode, Pairing Window Open, Door Ajar Alarm Timeout, Wi-Fi details, Active Faults | Informational. Smart Grid Mode cannot be written. |
| Appliance Model, Appliance Uptime | Uptime is seconds since power-up (`device_class: duration`). |

### Dishwasher

| Entity | Notes |
|---|---|
| Door, Wash Cycle Active, Light, Remote Ready, Delay Start | Binary sensors. Light is read-only: the appliance acknowledges writes but never changes state. |
| Wash Status, Wash Cycle, Wash Time Remaining | Numeric |
| Wash Cycle End Time | In the appliance's own clock, which is often unset. Republished only when the estimate moves by 5 minutes or more, because it wobbles a minute either way every poll. Wash Time Remaining is the reliable one. |
| Heated Dry, Extended Dry, High Temp Wash, Sanitize Rinse | Cycle options |
| Rinse Aid Low, Softener Low | Problem sensors. Softener needs `hide_softener: false`. |
| Service Required, Appliance Model, Appliance Uptime | |

### Range and wall oven

| Entity | Notes |
|---|---|
| Oven Temperature, Oven Set Temperature, Cook Mode, Gourmet Recipe | Measured temperature is real on ranges. Set Temperature is writable. |
| Probe Temperature, Probe Set Temperature, Probe Inserted, Probe At Temperature, Probe Within 10° | Probe Set Temperature is writable. |
| Door, Oven, Oven At Temperature, Oven Light, Oven Remote Ready, Gourmet Mode | Oven Light is writable. |
| Cook Timer Complete, Cook Timer Within 1 Min | |
| Kitchen Timer 1 and 2: Active, Complete, Within 1 Min, End Time | |
| Oven 2 versions of all of the above | `hide_oven2: false` |
| Service Required, Appliance Model, Appliance Uptime | |

**Oven Set Temperature and Probe Set Temperature only adjust a running cycle.** The appliance silently ignores a set-temperature write when the cavity is off, so start a cook mode at the front panel first. Writing from Home Assistant does not turn the oven on. Accepted ranges are 200–550 °F for the oven and 100–200 °F for the probe.

### Fridge write support

Confirmed on one appliance so far (Sub-Zero CL4850UFDID, firmware 2.27), so both flags default off.

`enable_temp_control: true` turns Set Temperature, Freezer Set Temperature, and Crisper Drawer Set Temperature into writable numbers and adds an **Automatic Crisper Temperature** switch. The crisper setpoint is ignored by the appliance while that switch is on, matching the official app.

`enable_mode_selects: true` adds four selects and makes Air Filter a switch:

| Select | Options |
|---|---|
| Ice Maker Mode | Off, Normal, Max Ice, Night Ice |
| Appliance Mode | Normal, High Usage, Short Vacation, Long Vacation, Sabbath |
| Night Mode | Disabled, Enabled |
| Humidity Control | Normal, Enhanced |

The protocol has no "set mode" command, so the grouped selects write their underlying booleans one at a time. All writes from every entity go through one queue that sends one BLE write about every 750 ms, coalesces repeated writes to the same property, and holds at most 16 pending writes. Writes are dropped, with a warning in the log, when the appliance is disconnected or its pairing window is closed.

Not exposed over BLE at all, and therefore not controllable here: temperature units, language, the internal water dispenser, interior light options, and door alarm sound and delay.

### Clear Cloud Token (BT-Only)

A diagnostic button per appliance that deregisters it from Sub-Zero's cloud, so the official app can no longer reach it remotely. The app falls back to BLE, which fails while the ESP32 holds the connection. Reversible by re-pairing in the official app. Any automations set up in the app stop working.

### Diagnostics

Under the Diagnostic section for every appliance: Appliance Serial, Appliance Type, Diagnostic Status, Firmware, API, BLE App, OS, and RTApp versions, Appliance Board Version, Build Date, and **Last Pairing Error**. The last one holds the decoded reason from the most recent failed bond, for example `0x04 CONFIRM_VALUE_FAILED (wrong PIN)`, until the next successful bond clears it to `None`.

### Status and the logbook

Home Assistant writes a logbook row for every distinct value a text sensor publishes, so **Status** reports only changes that matter. The scheduled 18-minute session refresh is silent unless it stalls for more than 90 seconds, `PIN confirmed` is announced once rather than on every poll, and Appliance Uptime is a number so the logbook ignores it. Full connection detail is still in the ESPHome log at `INFO`.

To hide Status from the logbook entirely, without affecting history:

```yaml
logbook:
  exclude:
    entities:
      - sensor.main_fridge_status
```

## Troubleshooting

| Symptom | What it means |
|---|---|
| Status shows `Connected, discovering...`, then `D5 found! Encrypting...` for 15–20 s on first connect | Normal. Encryption, GATT refresh, and rediscovery run automatically. |
| Status `Pairing failed (0x04 CONFIRM_VALUE_FAILED (wrong PIN))` | The PIN does not match the appliance. Re-read it from the display and enter it again. Other codes are SMP pairing reasons; `0x03 AUTH_REQ_UNMET` means keep `io_capability: keyboard_only` in `subzero.yaml`. |
| Status `Appliance refusing pairing, retrying in N s`, Last Pairing Error `0x09 REPEATED_ATTEMPTS` | The appliance has locked out pairing after too many failed attempts. The lockout only decays while nothing is trying, so the ESP32 backs off for 1, 2, 4, then 5 minutes between attempts and resumes on its own. If it never clears, power-cycle the appliance and press **Connect**. |
| Status `Pairing required`, most sensors unknown | The appliance rejected the unlock with `status 302`: its PIN rotated after a power cycle, reset, or app re-pair. Wake the display, press **Start Pairing**, and enter the new PIN. |
| Status `Enter PIN in HA, then reconnect.` | The appliance asked for a passkey and none is stored. Enter the PIN and press **Submit PIN & Unlock**. |
| Repeated `Disconnected` after a firmware update or overnight | A stale bond. After three failed reconnects the bond is cleared and re-paired automatically with the stored PIN. |
| Status `Encryption not completing, reconnecting...` | The link stayed unencrypted through five subscribe retries. Counts toward the stale-bond clear above. Seen with Wolf ranges, which are slow to encrypt; `fast_connect: true` and `power_save_mode: none` in the Wi-Fi block help. |
| Model, uptime, and version sensors refresh only every 18 minutes | Normal on firmware 8.5. Repeat polls within one BLE session go silent, so those fields refresh on the scheduled session refresh. Push updates keep live state current in between. |
| Dishwasher disconnects after about 10 seconds | Normal for Cove. The fast reconnect path finishes polling inside that window. |
| Crash loop | Out of memory. See [Memory](docs/advanced.md#memory) and reflash over USB. |
| `ACL packet too short` in the log | Fixed in ESP-IDF 5.5.5. Make sure ESPHome is 2026.7.1 or newer and no older `framework: version:` is pinned. |

## Tested appliances

| Brand | Model | Type |
|---|---|---|
| Cove | DW2450, DW2450WS | Dishwasher, with and without water softener |
| Sub-Zero | BI36UFDID, CL44750SID, CL4850SID, IT36CIID, 313, 48SID | Refrigerator-freezer with ice maker |
| Sub-Zero | CL4850UFDID | Refrigerator-freezer, firmware 2.27, including write support |
| Sub-Zero | DEU2450BG, DEU2450R | Under-counter beverage center and refrigerator |
| Sub-Zero | DEU2450WDZ, IW30R | Dual-zone wine storage |
| Wolf | DF36450GSP, DF48850SP | Dual-fuel range |
| Wolf | IR36550ST | Induction range |
| Wolf | SO3050PESP | Wall oven |

Other Sub-Zero, Wolf, and Cove appliances with BLE use the same protocol and should work. If you try one, open an issue with a debug capture (see [Debug mode](docs/advanced.md#debug-mode)) so it can be added here.

## Further reading

- [Advanced usage](docs/advanced.md): ESPHome commands, memory, how reconnects work, running the tests, releasing, debug mode.
- [BLE protocol reference](docs/ble-protocol.md): GATT layout, handshake, commands, example responses.
- [CHANGELOG](CHANGELOG.md)

Releases are cut automatically from the commit messages: after a green CI run on `main`, a `feat:` commit produces a minor release, `fix:` a patch, and `docs:` or `ci:` nothing. Each release stamps `sw_version` in `settings-example.yaml`. For reproducible builds, change `ref: main` in `subzero.yaml` to a release tag such as `ref: v3.8.4`.

## License and acknowledgements

Provided as-is for personal, non-commercial use, with no affiliation with or endorsement by Sub-Zero Group, Inc. Protocol details were reverse-engineered from the Sub-Zero Group Owner Android app and confirmed with live BLE captures using an ESP32 and Python [bleak](https://github.com/hbldh/bleak) scripts. Sub-Zero support declined to provide a local API. AI assistance was used for APK analysis, code, and documentation.

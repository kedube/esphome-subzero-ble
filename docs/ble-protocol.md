# BLE protocol reference

The Sub-Zero BLE protocol as reverse-engineered from the official Android app and live traffic captures. Everything here applies to Sub-Zero, Wolf, and Cove appliances alike.

- [GATT layout](#gatt-layout)
- [Firmware versions](#firmware-versions)
- [Connection and authentication](#connection-and-authentication)
- [Messages](#messages)
- [Commands](#commands)
- [Example responses](#example-responses)

## GATT layout

One custom service with five characteristics, present on every appliance type tested.

| Item | UUID |
|---|---|
| Service | `E20A39F4-73F5-4BC4-A12F-17D1AD07A961` |
| D4 to D8 | `08590F7E-DB05-467E-8757-72F6FAEB13D4` through `...13D8` |

| Channel | Role |
|---|---|
| D4 | Subscribed by the app, carries occasional binary heartbeats. Unused here. |
| D5 | Encrypted **control** channel: `display_pin`, `unlock_channel`, `set`, `scan`. Responses come back as indications. Also mirrors every D6 push; the integration ignores those copies. |
| D6 | Encrypted **data** channel: its own `unlock_channel`, then `get_async` for full state, plus all push notifications. |
| D7 | Open pre-authentication channel. `get_async` works without unlock and returns a small subset (model, doors, uptime, Wi-Fi id). The app uses it to probe before bonding; the integration does not. |
| D8 | Subscribed by the app, never carries data. Unused. |

Handle numbers vary by model and are discovered by UUID at runtime. The integration subscribes with CCCD `0x0002` (indications) on both firmware families.

## Firmware versions

| Firmware | API | Seen on | Indication size |
|---|---|---|---|
| 8.5 | 5.4 | Sub-Zero 313, DEU2450R, Wolf SO3050PESP | about 40 bytes, so a 2 KB poll arrives in 50+ fragments |
| 2.27 | 5.5 | Wolf DF36450GSP, Cove DW2450, Sub-Zero IW30R, CL4850UFDID | about 244 bytes, most pushes fit in one |

Firmware 8.5 has one more quirk: after the first poll in a BLE session, later `get_async` writes go unanswered, and re-sending `unlock_channel` does not help. The official app disconnects and reconnects between polls. The integration does the same on a fixed 18-minute cadence, with push notifications keeping live state current in between. Firmware 2.27 answers every poll.

The 40-byte fragments also made 8.5 appliances the victims of an ESP-IDF Bluedroid bug that rejected short ACL continuation fragments, logged as `ACL packet too short`. Espressif fixed it in ESP-IDF 5.5.5 ([esp-idf#18414](https://github.com/espressif/esp-idf/issues/18414)), which ESPHome adopted in 2026.7.1; that is why the project requires that version. The reassembly buffer stays defensive regardless: a payload that never balances its braces is discarded at 4 KB, and a payload the parser rejects publishes nothing.

## Connection and authentication

```
ESP32                                   Appliance
  | connect, service discovery             |   D5 and D6 hidden until bonded
  | request encryption (MITM)  ----------> |
  | <---------------------- passkey request|   6-digit PIN on the appliance display
  | passkey reply -----------------------> |
  | <------------------------ bond complete|
  | GATT cache refresh, rediscovery        |   D5 and D6 now visible
  | subscribe D5 and D6 (CCCD 0x0002) ---> |
  | unlock_channel on D5 ----------------> |   {"cmd":"unlock_channel","params":{"pin":"XXXXXX"}}
  | unlock_channel on D6 ----------------> |   each channel needs its own unlock
  | get_async on D6 ---------------------> |
  | <------------ full state, fragmented   |
  | ... pushes arrive on D6 ...            |
```

**Security.** Legacy pairing with MITM protection and bonding. The ESP32 uses `keyboard_only` IO capability so the user supplies the passkey. Secure Connections is not supported by the appliance; requesting it silently degrades to Just Works with no passkey. Once bonded, reconnects re-encrypt with the stored key and no passkey is asked for again.

**Discovery timing.** On most appliances D5 and D6 are absent from the GATT table until bonding completes, so the integration requests encryption, refreshes the GATT cache, and re-runs discovery, polling up to three times. Cove dishwashers expose D5 and D6 before bonding but still reject writes until the link is encrypted (GATT status 5).

**Bond failures.** Bluedroid deletes its own bond record whenever an SMP pairing fails, so the next connection is a fresh pairing attempt. An appliance that sees too many of those in a row answers `REPEATED_ATTEMPTS` (SMP 0x09) and keeps doing so until it is left alone. ESP-IDF reports SMP reasons offset by 77 (`HCI_ERR_MAX_ERR + 10`), so `auth fail reason=86` in the log is SMP 0x09.

**MTU** negotiates to 517, which does not change the appliance's fragment size.

## Messages

Every command is a JSON object terminated by `\n`. The appliance silently drops a command without the newline.

Responses and pushes are JSON, fragmented across indications; the receiver reassembles by tracking brace depth. Four shapes arrive on D6:

| Shape | Example | Meaning |
|---|---|---|
| Poll response | `{"status":0,"resp":{...}}` | Answer to `get_async` or `get`. `status` 302 means the unlock was rejected, usually because the PIN rotated. |
| Props push | `{"seq":N,"msg_types":2,"props":{"ref_door_ajar":true}}` | One or more changed fields |
| Diagnostic push | `{"seq":N,"msg_types":1,"diagnostic_status":"0x..."}` | Sent about once a minute when idle |
| Bare full state | `{"appliance_serial":"...", ...}` | The whole property set at the root, seen after a burst of `set` writes. Recognised by the presence of `appliance_serial`. |

Only a poll response counts as proof that polling works; pushes never do. This is what lets the `miss` counter expose the firmware 8.5 silent-poll behaviour instead of masking it.

Fridge firmware publishes set points only. No measured compartment temperature appears in any response, confirmed by capturing full state across a setpoint change on an IW30R. Wolf ranges do publish `cav_temp` and `cav_probe_temp` as real measurements.

## Commands

The official app's entire BLE vocabulary is six verbs, confirmed by decompiling its Dart code.

| Command | Channel | Payload | Response |
|---|---|---|---|
| `unlock_channel` | D5 and D6 | `{"cmd":"unlock_channel","params":{"pin":"XXXXXX"}}` | `{"status":0,"resp":{"pin":"XXXXXX"}}` |
| `get_async` | D6 | `{"cmd":"get_async"}` | Full state |
| `display_pin` | D5 | `{"cmd":"display_pin","params":{"duration":30}}` | Shows the PIN on the appliance for 30 s |
| `set` | D5 | `{"cmd":"set","params":{"<field>":<value>}}` | `{"status":0,"resp":{}}` |
| `scan` | D5 | `{"cmd":"scan"}` | List of Wi-Fi access points |
| `reset_air_filter` | D5 | `{"cmd":"reset_air_filter"}` | Untested here |

There is no keepalive and no subscribe verb; push subscriptions are plain CCCD writes.

**`get` fallback.** Some firmware answers `get_async` with `{"status":1,"resp":{},"status_msg":"An error occurred"}`. The official app then abandons BLE for its cloud path, which is not an option here. The integration instead retries with `{"cmd":"get"}`, which is not in the app's vocabulary but returns full state on the affected Sub-Zero IR36550ST (firmware 2.27), presumably through prefix matching in the firmware. The verb choice latches per appliance.

**`set` values.** Booleans are JSON `true`/`false`, setpoints are integers in °F, and the two fridge selects use integers: Night Mode is `0`/`1`, Humidity Control is `1` Normal / `2` Enhanced. Grouped modes such as Ice Maker Mode have no single field and are written as their component booleans. The appliance acknowledges every `set` with `status:0` even when it ignores the value, so a write is only confirmed by the pushed state that follows.

## Example responses

Cove DW2450 dishwasher, `get_async` on D6, trimmed:

```json
{"status": 0, "resp": {
  "appliance_model": "DW2450", "appliance_serial": "33333333", "uptime": "05:12:30",
  "door_ajar": false, "wash_cycle_on": false, "wash_status": 6, "wash_cycle": 0,
  "heated_dry_on": false, "extended_dry_on": false, "high_temp_wash_on": false,
  "sani_rinse_on": false, "rinse_aid_low": false, "light_on": false,
  "remote_ready": true, "delay_start_timer_active": false, "service_required": false,
  "notifs": []}}
```

Wolf DF36450GSP range, `get_async` on D6, trimmed:

```json
{"status": 0, "resp": {
  "appliance_model": "DF36450GSP", "uptime": "02:10:58", "sabbath_on": false,
  "cav_door_ajar": false, "cav_unit_on": false, "cav_temp": 75, "cav_set_temp": 0,
  "cav_cook_mode": 0, "cav_at_set_temp": false, "cav_light_on": false,
  "cav_remote_ready": false, "cav_probe_on": false, "cav_probe_temp": 1,
  "cav_probe_set_temp": 0, "cav_probe_at_set_temp": false, "cav_probe_within_10deg": false,
  "cav_gourmet_mode_on": false, "cav_cook_timer_complete": false,
  "kitchen_timer_active": false, "kitchen_timer_complete": false,
  "kitchen_timer2_active": false, "kitchen_timer2_complete": false, "notifs": []}}
```

Sub-Zero IW30R wine storage, `get_async` on D6:

```json
{"status": 0, "resp": {
  "accent_light_level": 0, "ref_door_ajar": false, "ref_set_temp": 38, "sabbath_on": false,
  "service_required": false, "unit_on": true, "wine_door_ajar": true, "wine_set_temp": 55,
  "wine_temp_alert_on": false, "wine2_set_temp": 45, "appliance_model": "IW30R",
  "appliance_serial": "XXXXXXXX", "uptime": "02:41:34", "notifs": []}}
```

Pushes on D6:

```json
{"msg_types": 2, "seq": 75, "timestamp": "2025-01-23T09:43:54.901", "props": {"ref_door_ajar": true}}
{"msg_types": 2, "seq": 80, "props": {"ref_set_temp": 38}}
{"diagnostic_status": "0x00000301111", "msg_types": 1, "seq": 113, "timestamp": "2026-04-25T07:31:44.114"}
```

A full fridge poll also carries `frz_set_temp`, `ice_maker_on`, `max_ice_on`, `night_ice_on`, `water_filter_pct_remaining`, `water_filter_gal_remaining`, `air_filter_pct_remaining`, `air_filter_on`, a `version` object with `fw`, `api`, `bleapp`, `os`, and `appliance` board strings, and the appliance's own Wi-Fi details. The complete captures used by the test suite are in `tests/fixtures/`.

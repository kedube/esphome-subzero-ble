# Changelog

All notable changes to this project are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## How this file is used

Add entries to the `## [Unreleased]` section as you merge work. When the
Release workflow runs, it lifts that section verbatim into the GitHub
release notes under a **Highlights** heading, then rewrites it here as a
dated version section. Write entries for someone deciding whether to
upgrade: what changed and why it matters, not which files moved.

Group entries under `### Added`, `### Changed`, `### Fixed`, `### Security`,
`### Performance`, or `### Removed`. Leave `## [Unreleased]` in place (empty)
after a release — the workflow expects it.

## [Unreleased]

### Security

- Sanitize every device-supplied string before it reaches Home Assistant.
  Values parsed from BLE (serial, model, Wi-Fi SSID, uptime, PIN) are now
  filtered through the same ASCII allowlist used for log output. A corrupted
  high-bit byte in any of these fields previously flowed straight to a text
  sensor and could crash Home Assistant's protobuf decoder into a restart loop.
- Stop logging the appliance pairing PIN in plaintext. Five log sites across
  pairing, reconnect, and PIN update now record only the digit count.
- Validate the PIN reported by the appliance (non-empty, digits only, 10 chars
  max). A malformed or empty `pin` field in a push message could previously
  overwrite a valid stored PIN and block all writes until the user re-paired.
- Ship the example configurations with API encryption and an OTA password.
  Copying a quickstart config verbatim previously produced a device that
  accepted unauthenticated firmware uploads from anyone on the network.

### Added

- Report the Bluetooth bonding verdict. The hub now consumes the ESP-IDF
  `AUTH_CMPL` event: a successful bond logs the negotiated auth mode
  (bonding / MITM / Secure Connections bits), and a failed bond publishes the
  decoded SMP reason to the Status entity, e.g. `Pairing failed (0x04
  CONFIRM_VALUE_FAILED (wrong PIN))`. Previously a bond that never completed
  and one that completed but exposed no data channel produced identical logs
  and a silent reconnect loop. Adapted from upstream
  JonGilmore/esphome-subzero-ble branch `diag/fw85-handshake`.
- Add a **Last Pairing Error** diagnostic text sensor per appliance. Status
  is overwritten by the reconnect loop seconds after a bond failure; this
  entity holds the decoded reason until the next successful bond clears it to
  `None`, and only republishes when the reason changes.

### Fixed

- Stop tearing down a connection while it is still encrypting. GATT writes
  rejected for insufficient authentication or encryption mean the link is not
  encrypted *yet*; the stale-handle heuristic counted them and forced a cold
  rediscovery about a second after requesting encryption, which cut the
  security exchange short on a Wolf range (observed as SMP `CONN_TOUT`) and
  turned every bond problem into a permanent 20-second reconnect loop. Those
  rejections now retry the subscribe after 2 s, up to five times, before the
  link is dropped; only handle-class errors feed the stale-handle streak.
- Feed a failed bond into the stale-bond recovery. A bonded appliance that now
  refuses pairing counts as a strike, so three in a row clear the bond and
  re-pair automatically, as designed. Previously the write-failure path
  cleared the cached handles first and the strike counter never moved.
- Back off when an appliance is rate-limiting pairing. An SMP
  `REPEATED_ATTEMPTS` refusal now disables the BLE client for a doubling
  interval from 1 to 5 minutes (Status shows "Appliance refusing pairing,
  retrying in N s"). That lockout only decays while the ESP32 stops redialing;
  auto-connect was retrying every few seconds and keeping it alive
  indefinitely. Pressing Connect or Reset Pairing clears the hold.
- Stop the Status entity from spamming the Home Assistant logbook. HA writes
  a logbook row for every distinct value a text sensor publishes, and a
  healthy appliance produced roughly a thousand a day: "PIN confirmed" was
  re-announced on every poll response, and the scheduled ~18-minute session
  refresh narrated its own disconnect/reconnect in five steps. PIN
  confirmation now announces only the edge, the session refresh is silent
  while it succeeds (a 90 s watchdog surfaces "Reconnecting..." if it stalls,
  and an unexpected drop always reports), and identical consecutive statuses
  are no longer republished. Every suppressed step still logs at INFO.
  Backported from upstream JonGilmore/esphome-subzero-ble PR #117.
- Republish the dishwasher Wash Cycle End Time only when the appliance's
  estimate moves by 5 minutes or more. It re-estimates on every poll and
  wobbles a minute either way around a target that has not moved.
- Recover from a truncated GATT snapshot instead of hanging forever. When
  service discovery returned the control characteristic but not the data
  characteristic, the connection stalled at "Auto-unlocking…" with no polling,
  no session-refresh timer, and no recovery short of a manual reconnect. The
  hub now re-reads the attribute table, forces up to two cold rediscoveries,
  and then parks with its retry timer still armed.
- Detect stale cached handles after an appliance firmware change. GATT writes
  report success synchronously and fail asynchronously, so a reboot that moved
  the handles left the hub polling a dead attribute indefinitely while the
  session refresh reset the failure counter each cycle. Asynchronous write
  failures are now handled, and three consecutive failures trigger a cold
  rediscovery.
- Stop losing messages that share a Bluetooth packet. The data channel is a
  byte stream with no message-aligned boundaries, so one notification can carry
  the end of one message and the start of the next. The buffer discarded that
  trailing fragment, leaving the following message with unbalanced delimiters
  and silently dropping traffic until a 4 KB overflow flush. Trailing bytes are
  now retained, and two complete messages in one packet both dispatch.
- Stop discarding a partially assembled message on the poll tick. The periodic
  poll cleared the assembly buffer unconditionally, orphaning any push that was
  mid-transfer. It now clears only a buffer that made no progress across a full
  poll interval.
- Protect a healthy pairing bond from user-initiated disconnects. Pressing
  Disconnect three times without an intervening successful poll reached the
  stale-bond threshold and wiped the bond, forcing a full re-pair. Deliberate
  disconnects are now excluded from that accounting.
- Preserve the "Pairing fully reset" instruction, which was immediately
  overwritten by the generic "Disconnected" status.
- Locate the notification descriptor in the attribute table instead of assuming
  it sits two handles after its characteristic. The hard-coded offset would
  break silently on a firmware layout change; it remains only as a fallback.
- Drop writes that cannot reach the appliance rather than reporting success.
  After a disconnect with cached handles, queued writes passed the readiness
  checks, failed at the Bluetooth layer, and vanished while the entity had
  already shown the new value. The queue is also flushed on disconnect so stale
  writes cannot fire into the next session.
- Round setpoints instead of truncating them. A Celsius-display frontend
  round-tripping through Fahrenheit (3 °C to 37.4 °F) wrote a setpoint one
  degree off.
- Report unknown remaining time across a month boundary instead of zero. A wash
  cycle finishing after midnight on the last day of a month reported "0 minutes
  remaining" for its entire duration.
- Reject invalid configuration at compile time rather than at runtime. A
  non-numeric `pin` silently produced a failed pairing, and `poll_interval: 0s`
  was accepted and issued a Bluetooth poll on every main-loop iteration.

### Performance

- Publish grouped mode selects only when their value changes. A full poll cycle
  re-published the appliance mode four times and the ice maker mode three times,
  each firing callbacks, a log line, an API message, and a Home Assistant
  history row.

### Removed

- **Breaking:** drop the `patch_acl_reassembly` component and require
  ESPHome 2026.7.1 or newer (`esphome: min_version: 2026.7.1`). The Bluedroid
  ACL continuation-fragment bug it worked around is fixed in ESP-IDF 5.5.5,
  which ESPHome adopted in 2026.7.1. Against that framework the frozen patch
  was a regression: it reverted upstream's stale-partial-packet cleanup and
  would clobber any future change to `packet_fragmenter.c`. Remove
  `patch_acl_reassembly` from your `external_components: components:` list;
  referencing it now fails config validation. Backported from upstream
  JonGilmore/esphome-subzero-ble PRs #111 and #114.

### Changed

- **Breaking:** Appliance Uptime is now a numeric duration in seconds
  (`device_class: duration`, `state_class: total_increasing`) instead of the
  appliance's raw `H:MM:SS` string. The string advanced on every poll and, as
  a text sensor, produced a logbook row each time; a numeric sensor with a
  unit is excluded from the logbook and is graphable. The entity ID is
  unchanged. If Home Assistant complains about the unit change, delete
  `sensor.<device>_uptime` and let it be recreated, and update any template or
  automation that parsed the old string. Firmware-truncated values
  (`627:09:3`, `1000:00:`) are handled; a malformed value publishes nothing.
- Expand the host test suite to 276 tests, including regression coverage for
  every connection-lifecycle and message-framing fix above.
- Restrict continuous integration to read-only repository permissions, fail the
  test job if test discovery ever breaks, and pin all GitHub Actions to
  Node 24 releases.

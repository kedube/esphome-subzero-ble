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

## [3.8.1] - 2026-08-12

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
- Fail the build when the ESP-IDF ACL reassembly patch cannot be verified.
  The pre-build script previously logged one line and continued unpatched,
  silently reintroducing the Bluetooth fragmentation bug the component exists
  to fix. It now hard-fails, checks that the target really is the Bluedroid
  packet fragmenter, keeps a `.orig` backup of the shared framework file, and
  writes atomically so an interrupted build cannot corrupt the toolchain.
- Ship the example configurations with API encryption and an OTA password.
  Copying a quickstart config verbatim previously produced a device that
  accepted unauthenticated firmware uploads from anyone on the network.

### Fixed

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

### Changed

- Expand the host test suite to 237 tests, including regression coverage for
  every connection-lifecycle and message-framing fix above.
- Restrict continuous integration to read-only repository permissions, fail the
  test job if test discovery ever breaks, and pin all GitHub Actions to
  Node 24 releases.


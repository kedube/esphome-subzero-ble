# Advanced usage

- [ESPHome commands](#esphome-commands)
- [Building against local component changes](#building-against-local-component-changes)
- [Memory](#memory)
- [How connections are kept alive](#how-connections-are-kept-alive)
- [Host tests](#host-tests)
- [Releases](#releases)
- [Debug mode](#debug-mode)

## ESPHome commands

With the virtual environment active:

```bash
esphome compile subzero.yaml                                 # build only
esphome run subzero.yaml --device /dev/cu.usbserial-XXXX     # flash over USB, then tail the log
esphome run subzero.yaml --device 192.168.x.x --no-logs      # flash over the air
esphome logs subzero.yaml --device 192.168.x.x               # tail the log over the network
```

`ls /dev/cu.*` lists serial devices on macOS.

## Building against local component changes

`subzero.yaml` pulls the component from GitHub, so edits under `components/` are not used until they are pushed. ESPHome also caches git sources for a day. To build from the working tree, change the source block to:

```yaml
external_components:
  - source:
      type: local
      path: components
```

To force a fresh pull of `main` instead, delete `.esphome/external_components/` before building.

## Memory

An ESP32 without PSRAM has about 320 KB of RAM and the BLE stack takes a large share of it. The component keeps its own footprint down by reserving one 2 KB reassembly buffer per appliance up front, parsing JSON in place with ArduinoJson's zero-copy mode, running the parser on the main loop rather than in the BLE callback, publishing only values that changed, and leaving hidden entities out of the build entirely.

If the device crashes with `__cxa_allocate_exception` and `abort()` in the backtrace: reduce Home Assistant API connections, set `ble_client: WARN` in the logger block, lengthen `poll_interval`, or hide entities you do not use. The shipped configuration targets an ESP32-S3 with PSRAM, which avoids the problem.

Because unchanged values are not republished, `last_updated` on static entities such as model and firmware version only advances when the value changes.

## How connections are kept alive

**Polling.** Every `poll_interval` (60 s) the hub writes `unlock_channel` and `get_async` to the data channel. Push notifications arrive in between for door, light, and setpoint changes.

**Session refresh.** About 18 minutes after each unlock the hub deliberately disconnects and reconnects through a fast path that reuses cached GATT handles. This pre-empts the appliance firmware's own 20–25 minute unlock expiry, which would otherwise drop the connection unpredictably. On firmware 8.5 appliances this refresh is also the only time poll-only fields such as model and uptime update, because repeat polls inside one session go unanswered. The `miss=N` counter in the log tracks those silent polls and is diagnostic only. The refresh does not publish to Status unless it stalls for more than 90 s.

**Encryption retries.** After requesting encryption the hub waits one second, then subscribes. If a write is rejected for insufficient authentication or encryption, the link is simply not encrypted yet, and the subscribe is retried after 2 s, up to five times. Only invalid-handle errors count toward the stale-handle check that forces a full rediscovery. Bluedroid raises no application event when a bonded link re-encrypts with its stored key, which is why this is timer-driven.

**Stale bonds.** Each failed reconnect, failed bond, or exhausted encryption retry is a strike. Three in a row clear the ESP32's bond, wipe the cached handles, and re-pair from scratch with the stored PIN. Disconnects the hub initiates itself, such as the session refresh or the Disconnect button, never count. A failed bond also publishes its decoded reason to the Last Pairing Error entity.

**Pairing back-off.** When an appliance answers a pairing attempt with `REPEATED_ATTEMPTS`, it has locked pairing out and the lockout only decays while nothing retries. The hub disables the BLE client for 1 minute, doubling to a 5-minute cap on consecutive refusals, and re-enables it automatically. Connect and Reset Pairing clear the hold.

**Poll verb fallback.** Some firmware answers `get_async` with `{"status":1,"resp":{}}`. The hub then switches that appliance to `{"cmd":"get"}` for the rest of its life; the choice persists across reconnects and resets only with Reset Pairing.

**`poll_offset`.** Applied at the top of every connection sequence and poll, so appliances on the same ESP32 bond one after another instead of racing each other.

Log lines to know:

```
[I][szg]: [Kitchen Range] Periodic poll D6 (verb=get_async, miss=0, retries=0)
[E][szg]: [Kitchen Range] Pairing rejected (status 302). The PIN has likely changed - press 'Start Pairing' on the appliance and enter the new PIN in HA.
[E][ble]: [Refrigerator] SMP bond FAILED reason=86 (SMP 0x09 REPEATED_ATTEMPTS)
```

`retries` is the stale-bond strike count.

## Host tests

The parser, message buffer, command builders, write queue, and the whole hub state machine compile on a desktop with a fake BLE transport and scheduler. CMake fetches ArduinoJson, GoogleTest, and nlohmann/json on the first run.

```bash
cmake -S tests/cpp -B tests/cpp/build
cmake --build tests/cpp/build -j
ctest --test-dir tests/cpp/build --output-on-failure
```

CI runs the same commands on every push, then validates and compiles `subzero.yaml` with a dummy `settings.yaml` and `secrets.yaml`.

Parser fixtures live in `tests/fixtures/`, one `.json` capture plus one `.expected.json` per payload, and are discovered automatically. To add one for a new appliance, capture a full poll in [debug mode](#debug-mode), save it as the `.json`, and record the parsed output as the `.expected.json`.

## Releases

Releases are driven by commit messages. After every green CI run on `main`, the Release workflow classifies the commits since the last tag:

| Commits since the last tag | Release |
|---|---|
| `type!:` or a `BREAKING CHANGE` footer | major |
| `feat:` | minor |
| `fix:`, `perf:`, `revert:` | patch |
| only `docs:`, `ci:`, `chore:`, `test:`, `style:`, `build:`, `refactor:` | none |
| no recognised prefix | patch |

The highest level wins. Unlabelled commits count as a patch on purpose: this is firmware, and a code change should ship rather than sit unreleased over a commit-message slip. The workflow refuses to tag a commit whose CI did not pass, and it skips with a warning when a release is due but the `## [Unreleased]` section of `CHANGELOG.md` is empty. A release with no notes is almost always a mistake, so write the entry with the change.

Each release tags `vX.Y.Z`, publishes GitHub release notes built from the Unreleased entries plus GitHub's generated commit list, and commits a follow-up to `main` that stamps `sw_version` in `settings-example.yaml` and rolls the changelog section. That follow-up is a `chore:` commit and does not trigger another release.

To force a specific bump, a prerelease, or a dry run that only previews the notes, run the workflow by hand from the Actions tab. Leave the bump on `auto` to get the same classification as the automatic path.

`scripts/changelog.py extract` prints the Unreleased body and `scripts/release_notes.py` assembles the final notes, so the exact release body can be previewed locally.

## Debug mode

Each appliance has a **Debug Mode** switch and a **Log Debug Info** button. With debug mode on, every full response is written to the ESPHome log at `INFO` in `Response[N/M]:` chunks of 400 bytes, and every top-level key is listed under the `szg-debug` tag. Bytes outside printable ASCII are replaced with `?` before logging, because a non-UTF-8 byte in a log line crashes Home Assistant's log decoder.

**Log Debug Info** turns debug mode on and forces a disconnect; the automatic reconnect lands a fresh full-state poll within 10 to 15 seconds.

To capture an appliance for a bug report or a new fixture:

1. Press **Log Debug Info** and wait for the reconnect.
2. Grep the log for `Response\[` and concatenate the chunks in order.
3. Optionally open a door or change a setpoint to capture push notifications, which carry only the changed field.
4. Turn **Debug Mode** off.

Normal operation never logs raw BLE payloads.

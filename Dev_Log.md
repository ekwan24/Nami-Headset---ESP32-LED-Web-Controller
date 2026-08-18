# Nami Headset — Build Log

Reconstructed from project chat history. Each entry = one rebuild session. Newest at bottom.

---

### 2026-08-03 — Baseline established
**Files:** both (baseline load, not a rebuild)
- Loaded `Nami_Headset_LEDs.ino` + `index.html` as the working baseline (228 WS2812B: RingA 54 / Strip 120 / RingB 54; QA system: Solid, Breathe, Waves).
- Standing rule set: **no file edits without explicit go-ahead**.
- Fixed Breathe speed (was hardcoded, ignored speed field) — added time-based (`dt`) phase advance.
- Added Waves master brightness (`waveBrightScale` / `WAVEBRIGHT`), removed redundant `trailBright`/`TRAILBR`.
- Renamed "Brightness" → "Peak Brightness"; added two-way peak/base clamp.
- Tried whole-buffer gamma correction (`napplyGamma_video`) for low-brightness color shift — **later reverted** (see next entry).
- Ended unresolved: LEDs shifting greenish-yellow after brightness changes.

### 2026-08-03 — Simplification + state sync + UI queue
**Files:** both
- Removed unused Preset system (presets[5], EDITP/SEG/SAVE/RUN, save/load flash) — kept BLE, hardware defs, 3 patterns, QA, RSSI.
- Fixed Arduino compile error: `pctTo255(pct, floor255=0)` default arg not auto-prototyped reliably — reordered function defs.
- Added centralized **DEFAULT TUNING VALUES** config block (plain % constants, `pctTo255()` auto-applied in `makeQA()`).
- Added `renderSegment()` helper to collapse duplicate per-segment render logic.
- Gamma investigated again: per-channel gamma was crushing hue (green instead of teal) — fixed by applying gamma to the brightness **scalar** in `segColor()`, then **fully removed** at user's request → linear `nscale8()` only. (Root cause suspected: low battery voltage under-driving blue channel, not software.)
- Saved permanent QA defaults: `QA1=50%`; `QA2 25/50/-3`; `QA3 50/25/0/20 trail`.
- Implemented **BLE state sync**: `StateCallback` on READ char (`...def3`) builds `QA:N,ON:..,COLOR:..,BRIGHT:..,BASE:..,MINBR:..,MAXBR:..,SPD:..,TRAIL:..,WBRIGHT:..`; webpage reads on connect via `syncStateFromDevice()` → `applyState()`, no round-trip commands sent.
- Built 4 queued webpage changes: signal strength label (High/Med/Low), dim/disable cards 2+ when disconnected (no shimmer), "Quick Access" → "Patterns" (centered), card 4 header (`tuningWho`) restyled to match other card headers (white).

### 2026-08-04 — Connection state + Breathe smoothing
**Files:** both
- Removed heartbeat/RSSI-cadence disconnect watchdog (was causing false disconnects) — reverted to native `gattserverdisconnected` only. *(Later superseded — see 08-06 reconnect work.)*
- Fixed stale UI-on-connect bug: sliders/toggle showed hardcoded JS defaults instead of live firmware state — fixed via cached `stateCharacteristic` + `syncStateFromDevice()`, called on connect and after pattern switch, with one retry.
- Fixed `pctTo255` compile error properly: removed default arg, explicit `0` at all 5 call sites.
- Ring A/B were counter-rotating; since both are rings on one headset, made them move identically — removed `reverse`/mirror logic, shared `ringPos`. *(Later reversed — see next entry, physical mount is actually mirrored.)*

### 2026-08-05 — Reset button + Ring B mirror
**Files:** both
- Added **Reset to default** button (card 4): `QA_ORIGINAL[3]` snapshot array in `setup()`, `RESET` BLE command restores `QA[runQA]`, JS `resetPattern()` sends command + spin/flash animation + re-syncs UI.
- Re-added Ring B mirroring for Waves: `RINGB_MIRROR_ROTATION` toggle + `renderWavesRing(..., reverse)` — physical mount is actually mirrored (like meshed gears), confirming 08-04's "same direction" change was wrong for this hardware.
- Added `RINGB_PHASE_OFFSET` (0–53) to fine-tune alignment between the two rings' bright heads without further code changes — flash, observe, nudge.

### 2026-08-05 — Discussion only: BLE disconnect / power-off state (no changes made)
- Analyzed persistence: only `runQA` and `ledsOn` survive reboot (flash `Preferences`); all tuning (color, brightness, speed, trail) resets to firmware `#define` defaults on reboot.
- Identified usability gap: no way to command the headset while BLE is disconnected (cosplay use case — worn for hours, phone not always in reach).
- Proposed 3 options (physical reset button / auto-revert timeout / power-switch-as-reset) — none implemented, tabled.

### 2026-08-05 — Rainbow pattern (4th QA effect)
**Files:** both
- New pattern: `PAT_RAINBOW`, QA4 config block (`QA4_BRIGHTNESS_PCT`, `QA4_SPEED_TICK`, `QA4_RAINBOW_SPEED_TICK`, `RAINBOW_RINGB_HUE_OFFSET`).
- `SegSettings` gained `hueThr`/`hueStep`; `applyHueTick()`/`getHueTick()` mirror the existing speed-tick helpers.
- `renderRainbowRing()` — smooth full-spectrum wheel, uniform brightness. `renderRainbowStrip()` — mirrored gradient flowing end→center, continuous loop, no trail/peak/base.
- Ring B mirrors Ring A the same way Waves does (`RINGB_MIRROR_ROTATION` + hue offset).
- New `HUESPD` BLE command (independent "Rainbow Speed" control); state string gained `RSPD:`.
- Webpage: 4th "Rainbow" button, reused Solid/Speed sliders via `data-qa`, added Rainbow Speed slider, color picker auto-hides for Rainbow.

### 2026-08-06 — Battery life estimate (no file changes)
- Estimated runtime on a 10Ah USB power bank spliced into VIN: ~2.1 hrs at max/medium brightness (hits the 3A `FastLED.setMaxPowerInVoltsAndMilliamps` cap), ~4.3–7 hrs at current QA-default low brightness.
- Flagged: cap is a software estimate with no real current sensing, no fuse/overcurrent protection; animated patterns cause current swings that can trip power bank protection even under the average limit.
- Recommended (not applied): lower the firmware cap to 70–80% of actual cable rating for headroom.

### 2026-08-06 — Auto-reconnect + faster disconnect detection
**Files:** `index.html` only
- Added `manualDisconnect` flag to distinguish user-initiated disconnects from unexpected drops.
- Added `attemptReconnect()`: retries using the cached `device` object (no device picker), `Promise.race()` timeout around `gatt.connect()` so a dead device can't hang the loop, "Reconnecting…" UI with a live Cancel button (`reconnectAbort`).
- Added ping/heartbeat: periodic `stateCharacteristic.readValue()` with short timeout to catch power-off faster than the OS's native BLE timeout.
- Bug fix: ping was too aggressive (short interval, 1-fail trigger) causing connect/disconnect/reconnect flapping — fixed with 2-consecutive-failure threshold, 5s interval / 2.5s timeout, skip ping while a slider write is in flight.
- Bug fix: race condition where a mid-reconnect cleanup `disconnect()` re-triggered `onDisconnect()` and spawned a second overlapping reconnect loop — fixed by checking `isReconnecting` at the top of `onDisconnect()`.

### 2026-08-06 — Custom color picker (replaces native color input)
**Files:** `index.html` only
- Replaced the native `<input type="color">` swatch with a fully custom picker matching the app's dark theme: hue+saturation square, hue slider, and a row of 4 preset slots.
- Slot 0 is permanently locked to the firmware's default teal (`80E2E5`) — square/slider disable themselves whenever it's selected. Slots 1–3 start **empty** (rendered as a corner-to-corner X) and "fill in" on first tap, adopting whatever color the square is currently showing rather than jumping to an arbitrary default.
- Selecting a slot or dragging the square/slider sends `QACOLOR` live over BLE (debounced during drag, immediate on release/tap), matching the app's existing slider pattern.
- Collapsed the picker behind a compact **color bar** (swatch + hex + chevron) that expands/collapses the full picker on tap, to save space in card 4.
- On device state sync, the running color is matched against the 4 saved slots (highlighting the match) or, if no match, just positions the square/slider without a slot selected.
- **Bug + fix — selection ring clipped:** the popover needs `overflow:hidden` for its expand/collapse animation, which was clipping the swatches' outward selection-ring `box-shadow` at the container edges. Fixed by adding padding inside the clipped container so the ring has room to render before hitting the boundary. *Lesson: any element with a glow/ring effect that extends beyond its own box needs padding room in the nearest `overflow:hidden` ancestor, not just in its own margin.*
- **Bug + fix — hue slider showed no gradient (two attempts):**
  1. First cause found: an existing generic rule (`input[type=range]::-webkit-slider-runnable-track { background:var(--border) }`) was painting a solid track over the hue slider's rainbow gradient, since WebKit renders the track *pseudo-element*, not the input's own background.
  2. First fix attempt (making the track pseudo-element transparent so the input's own gradient background would show through) did not work — WebKit doesn't reliably paint an input's own `background` behind its track pseudo-element once `-webkit-appearance:none` is set. Confirmed broken via screenshot (only the thumb was visible, no track at all).
  3. Working fix: paint the gradient **directly on the track pseudo-element** (`::-webkit-slider-runnable-track` / `::-moz-range-track`) instead of the `<input>` itself, with `!important` to beat the generic rule's specificity.
  - *Lesson: when styling a range input's track (gradient, image, custom color), always set the background directly on `::-webkit-slider-runnable-track` / `::-moz-range-track` — never on the `<input>` element and hope it shows through.*

### 2026-08-08 — Firmware robustness/efficiency pass + first hardware flash
**Files:** `Nami_Headset_LEDs.ino` (now at `Nami_Headset_LEDs/Nami_Headset_LEDs.ino`)
- Replaced Arduino `String` concatenation in `buildStateString()` and `CmdCallback::onWrite()` with fixed stack buffers (`snprintf`/`memcpy`/`strcmp`) — removes per-read/per-write heap churn that could fragment memory over long uptime. BLE `getValue()` still hands back one `String` (that's the library's own API, unavoidable); only the downstream trim/substring/concat chain was replaced.
- Added an input-length guard on incoming BLE commands (`MAX_CMD_LEN 32`) — oversized/malformed writes are dropped before parsing instead of costing an allocation.
- Reworked `loop()` frame pacing: was a flat `delay(10)` after `FastLED.show()`, so the real frame period was `10ms + whatever show() cost that frame` (undefined/variable). Now targets a fixed 20ms period (`FRAME_INTERVAL_MS`), trimmed by however long that frame's rendering actually took — animation speed no longer drifts with render cost.
- Added a boot-time `Serial.printf("FastLED version: %d")` plus a comment documenting the ESP32 RMT-vs-bit-bang BLE-coexistence concern (bit-banged WS2812 output can disable interrupts long enough to stall the BLE stack) — now a checked fact at boot, not an assumption.
- **Bug caught mid-build:** `std::string raw = c->getValue();` didn't compile — this ESP32 core's BLE library returns Arduino `String`, not `std::string`. Fixed to accept the one unavoidable `String` from the API; everything downstream still copies into the fixed buffer.
- **First flash from a local toolchain:** installed `arduino-cli`, pointed it at the existing Arduino IDE data dir (`esp32:esp32` core 3.3.11 was already installed), compiled for FQBN `esp32:esp32:esp32` (ESP32 Dev Module), flashed over COM7 (Silicon Labs CP210x bridge). Verified via serial monitor after reset: RMT driver active (`Added driver 'RMT'`, `Channel created on GPIO 5`), `BLE advertising as ESP32-LED`. Sketch uses 95% of program storage (1,245,303 / 1,310,720 bytes) — little headroom left before a partition scheme change would be needed.
- Sketch folder reorganized to `Nami_Headset_LEDs/Nami_Headset_LEDs.ino` (Arduino toolchain requires the containing folder name to match the `.ino` filename to compile/upload). `README.md` deleted (was empty, unused).

### 2026-08-08 — Battery efficiency: off-state refresh skip + CPU underclock
**Files:** `Nami_Headset_LEDs/Nami_Headset_LEDs.ino`
- `loop()` was re-sending an unchanged all-black frame every 20ms forever whenever `ledsOn == false` — WS2812 holds its last latched value on its own, so this bought nothing but kept the CPU fully active during standby. Now pushes black once per off-transition (`offFrameSent` flag) and idles at 100ms polling instead; BLE writes (including the `ON` command) still land immediately since they run in the BLE stack's own task, not gated by `loop()`'s cadence.
- Added `setCpuFrequencyMhz(80)` as the first line of `setup()` — drops from the default 240MHz to 80MHz, the lowest clock Espressif supports with BLE active. Flagged going in that RMT (LED timing) and the `millis()`/`delay()` timer both run off clocks independent of CPU frequency, but that per-frame render cost (Rainbow's `hsv2rgb_rainbow` especially) would take ~3x longer in wall-clock terms and needed on-device confirmation it still fits the 20ms frame budget.
- **Verified on hardware:** boot log after flashing shows identical RMT timing config (`resolution=40000000Hz, ns_per_tick=25`) as before the clock change, confirming RMT is unaffected. BLE advertised normally. User visually checked all 4 patterns over the webpage — no stutter/lag on any, including Rainbow.
- **Flash hiccup during testing (unrelated to the code):** first upload attempt lost the USB connection mid-write (~75% through the app image) — COM7 disappeared from Windows entirely. Reseating the cable brought it back; re-flash completed and verified cleanly on retry. Worth remembering if COM7 vanishes again during a future upload — check the physical connection before assuming a firmware/tooling problem.

### 2026-08-08 — GitHub Pages hosting confirmed working end-to-end
**Files:** none (infra/deployment only)
- Repo confirmed public (`private: false` via GitHub API) with no secrets found anywhere in tracked files or full commit history (searched for password/SSID/API-key/token/private-key patterns — clean).
- Discussed the actual convention use case: phone won't be near a laptop, so `index.html` needs to be reachable from the phone without a local server. GitHub Pages is a free, permanent HTTPS URL for exactly this — and since Web Bluetooth talks phone-to-headset directly, GitHub is only involved in the one-time page load, never in live LED control.
- User enabled GitHub Pages and **verified it live**: opened the hosted URL on a phone and controlled the LEDs over Bluetooth successfully, away from the local dev server. Confirms the whole chain (GitHub Pages → phone browser → Web Bluetooth → ESP32) works for real, not just in theory.
- Flagged for later: iOS Safari does not support Web Bluetooth at all (Apple platform limitation, not fixable in our code). Workaround discussed: **Bluefy** (paid) or **WebBLE** (free) — third-party iOS browsers that implement Web Bluetooth, no code changes needed. A "real" fix (ESP32 self-hosting its own page over WiFi/HTTP, universal browser support) was scoped but not started — bigger project, and current sketch is already at 95% flash usage so it would likely need a partition scheme change.

### 2026-08-08 — Switched partition scheme to min_spiffs, freed ~35% flash headroom
**Files:** `CLAUDE.md`, `.claude/skills/rebuild-and-flash/SKILL.md` (docs/tooling only — firmware `.ino` itself unchanged)
- Confirmed the board's flash chip directly via `esptool.exe --port COM7 flash_id`: **4MB** (Manufacturer 5e, Device 4016).
- Discovered the partition table already in use ("Default") already had OTA-style dual app slots (`app0`/`app1`, ~1.25MB each) — the 95%-full number tracked since 2026-08-08 was how full *one* of those slots was, not evidence OTA support was missing at the partition level.
- Real constraint was that an unused `spiffs` partition (1.375MB, nothing in this project touches SPIFFS/LittleFS) was eating space that could go to the app slots instead. Switched FQBN to `esp32:esp32:esp32:PartitionScheme=min_spiffs`, which shrinks `spiffs` to 128KB and grows `app0`/`app1` to ~1.9MB each.
- Recompiled (same firmware, no `.ino` changes) and reflashed: **storage usage dropped from 95% to 63%** of the app slot. Verified via serial boot log after reset — identical clean boot (FastLED version, RMT driver active, BLE advertising), confirming the new partition table works correctly on hardware, not just in the compile output.
- Updated `CLAUDE.md` and the `rebuild-and-flash` skill to require `PartitionScheme=min_spiffs` on every future compile/upload — omitting it would silently revert to the old, smaller partition layout, which would no longer match what's actually on the board and risks a boot/partition mismatch on the next flash.
- This also means WiFi/OTA (discussed earlier today) has meaningfully more room to work with now — not confirmed it fits, but the ~630KB of extra headroom this freed is exactly the kind of space that discussion flagged as needed.

### 2026-08-14 — ESP32-C6 port (Seeed XIAO ESP32-C6), verified working on hardware
**Files:** new `Nami_Headset_LEDs_C6/Nami_Headset_LEDs_C6.ino` (original `Nami_Headset_LEDs/Nami_Headset_LEDs.ino` untouched — both firmwares now maintained in parallel). `index.html` unchanged — BLE name/UUIDs/command protocol aren't chip-dependent.
- Researched risk areas before porting: FastLED has open 2026 GitHub issues on ESP32-C6 (RMT5 "No Engine" errors, DMA-not-supported crashes, reported WS2812 timing bugs) — flagged as the biggest unknown going in. In practice, compiled and ran clean with the installed FastLED 3.10.5 / esp32 core 3.3.11 — no workaround needed.
- **LED_PIN changed 5 → 21**: GPIO5 is a JTAG pin on the XIAO ESP32-C6 (unusable as general GPIO on this board). GPIO21 (silkscreened `D3`) is the cleanest unshared GPIO in the board's 11-pin breakout (D0-D2 are ADC-capable, D4-D10 are tied to I2C/UART/SPI).
- **BLE stack is NimBLE on this core/target, not Bluedroid** (the classic ESP32 build's stack) — confirmed the C6 build doesn't ship `esp_gap_ble_api.h` at all. Ported the RSSI-read path: removed the Bluedroid async `gapCallback`/`esp_ble_gap_read_rssi()` pattern, replaced with NimBLE's synchronous `ble_gap_conn_rssi(connHandle, &lastRssi)`, keyed off a `connHandle` (`uint16_t`) captured in `ServerCallback::onConnect(BLEServer*, ble_gap_conn_desc*)` instead of Bluedroid's `esp_bd_addr_t`. Same RSSI characteristic/format on the wire, so `index.html` needed no changes.
- `setCpuFrequencyMhz(80)` kept as-is — confirmed 80MHz is a valid frequency step for this board (Arduino menu lists 160/80/40/20/10; C6 maxes at 160MHz vs. 240MHz on the classic ESP32). Comment updated to reflect the correct max-clock figure.
- Board specifics used for the FQBN: `esp32:esp32:XIAO_ESP32C6`, 4MB flash, no `min_spiffs` option available on this board — used `PartitionScheme=no_ota` (2MB APP/2MB unused SPIFFS) instead, since this board's flash layout doesn't offer 6-digit granularity like the classic ESP32's did and SPIFFS isn't used by this project anyway. Compiled: 887,402 bytes (42% of 2MB app partition), 27,832 bytes RAM (8%) — comfortable headroom.
- USB is native CDC (no CP210x bridge) — enumerated as `USB Serial Device` (VID_303A, Espressif's own vendor ID) on COM8, `CDCOnBoot=cdc` already the board default.
- **First flash succeeded**: compiled + uploaded via `arduino-cli` over COM8, hash-verified on every partition (bootloader/partitions/boot_app0/app), clean reset. **User confirmed working on real hardware** — LEDs and BLE control both functioning from the webpage.

### 2026-08-17 — Strip shortened to match physical cut, C6 LED pin moved to D10, new default look
**Files:** `Nami_Headset_LEDs/Nami_Headset_LEDs.ino`, `Nami_Headset_LEDs_C6/Nami_Headset_LEDs_C6.ino`, `index.html`
- Physical LED strip was cut shorter (from 120 LEDs). `STRIP_LEDS` updated in both firmwares to match the actual cut — classic ESP32 to 68, C6 to 69 (the two ended up cut to slightly different lengths, so tracked separately rather than forced to match). `NUM_LEDS` comment updated 228 → 176 in both.
- **C6's `LED_PIN` moved GPIO21 (D3) → GPIO18 (D10)**: checked the installed board's `pins_arduino.h` — D10 is nominally labeled MOSI, but since this sketch never initializes SPI, the pin is electrically free. Same reasoning already applied to D3 (nominally SPI `SS`) when it was originally chosen. Confirmed GPIO18 isn't a JTAG or boot-strapping pin either. Physical data wire moved to match.
- **Bug hit after the first shorter-strip flash:** Strip + RingB stayed dark, RingA still worked. Root cause was a wiring break at the cut point — the original 5V/GND/data harness that ran from the strip's far end over to RingB had been left attached to the discarded offcut piece, so RingB was getting neither power nor data anymore. Fix: resolder that 3-wire harness onto the new cut end (matters that data goes **DO → DI**, i.e. the correct direction — reversing it would leave RingB dark even with the harness reattached). Worth remembering for next time the strip gets trimmed further.
- C6's default Quick Access look changed: color teal `#80E2E5` → blue `#006EFF`; Solid brightness 10% → 100%; Waves peak 25% → 100%, base 5% → 25%, speed tick 0 → 1 (trail unchanged at 20).
- `index.html`'s color picker (default swatch, `colorBarHex`, initial hue/sat/val) and the `QA_DEFAULTS` fallback object were synced to the new C6 values, so the pre-connect/fallback UI state matches what the firmware actually does on boot.
- **Classic ESP32 firmware's QA defaults were intentionally left unchanged** (still teal/lower brightness) — only the C6 board is physically in the headset right now, so that file only got the `STRIP_LEDS` fix, not the look change. Worth reconciling if the classic board ever goes back in.
- Committed as `20cda36`.

### 2026-08-17 — Waves/Rainbow strip fixes for the odd-length cut, new tuning defaults
**Files:** `Nami_Headset_LEDs/Nami_Headset_LEDs.ino`, `Nami_Headset_LEDs_C6/Nami_Headset_LEDs_C6.ino`, `index.html`
- **Waves strip reset looked like a pop, not a fade** (`renderWavesStrip`): the two mirrored fronts travel from each edge toward the middle, then snap back to the edge once they meet (`half = STRIP_LEDS/2`, wrapped via `while (head >= half) head -= half`) — confirmed with the user this hard-reset design is intentional (like the game Snake wrapping off one edge of the screen and reappearing on the other), not a bug to remove. The problem was that `QA3_TRAIL_LENGTH` (20) was long relative to the travel distance to the middle (`half`=34 for the 69-LED strip) — the trail never fully faded to background brightness before the reset, so a visible ~20-LED chunk of still-lit trail vanished each cycle instead of a clean loop. Fixed with a runtime safeguard: cap the effective trail to at most `half / 2`, guaranteeing it's fully faded (invisible) before every reset, regardless of the configured trail length.
- **Odd LED count left the exact center pixel dark**: with `STRIP_LEDS` now 69 on the C6 (odd), `half = STRIP_LEDS / 2` truncates to 34, so the two fronts only ever reach indices 33 and 35 — index 34 (dead center) is never covered by either one. Same root cause existed in both `renderWavesStrip` and `renderRainbowStrip` (worse in the Rainbow case, since that function never fills a background color first, so the skipped pixel just showed stale leftover color from whatever pattern ran before it). Fixed in both functions, both files, by rounding up instead of down (`half = (STRIP_LEDS + 1) / 2` / `half = (count + 1) / 2`) — makes both fronts converge exactly onto the shared center pixel at the peak before wrapping. No effect on even LED counts, so the classic ESP32's 68-LED strip is unaffected.
- **Base Brightness slider had a 25% floor** (`index.html`): hardcoded in the range input's `min="25"` attribute — nothing on the firmware side enforced this (the `BASE` command already accepts 0-100), it was purely a UI restriction. Lowered to `min="5"`.
- **New C6 tuning defaults** (user-edited directly in the `.ino`): Breathe min/max brightness 5/35% → 15/100%, speed tick 0 → -1; Waves speed tick settled at 0 (was briefly 1 in the previous entry); Rainbow brightness 25% → 100%, speed tick 0 → 2, rainbow-speed tick 0 → 2. `index.html`'s `QA_DEFAULTS` fallback object re-synced to match. As with the 08-17 color/brightness change, the classic ESP32 firmware did **not** get these tuning updates (only the trail-cap and center-pixel fixes, which are harmless no-ops for its even 68-LED strip) — still only the C6 board is physically in use.
- **Verified on hardware**: compiled + uploaded to the C6 over COM8, hash-verified, clean boot log (FastLED init, PARLIO driver, BLE advertising) — no crash/panic.

### 2026-08-18 — BLE OTA implemented, debugged, and reverted; stale "Last updated" stamp fixed
**Files:** `Nami_Headset_LEDs/Nami_Headset_LEDs.ino`, `Nami_Headset_LEDs_C6/Nami_Headset_LEDs_C6.ino`, `index.html` (OTA changes all **stashed, not committed** — see below; only the stamp fix landed on `main`)
- Built out the BLE OTA design scoped 2026-08-17: `OTA_CONTROL`/`OTA_DATA`/`OTA_STATUS` characteristics on both firmwares, `Update.h` (`begin`/`write`/`end`/`abort`), C6 switched to `PartitionScheme=default` (from `no_ota`) so it has the second app partition OTA needs to write into. Webpage got a "Firmware Update" card — file picker, chunked transfer, progress bar, START/END/ABORT handshake.
- **Bug 1 (JS race condition):** `waitForOtaStatus()` attached its `characteristicvaluechanged` listener *after* the triggering write (`START`/`END`) had already resolved. When the firmware replied fast, the notification could arrive and fire before the listener existed — Web Bluetooth doesn't replay missed events, so the wait timed out even though the device answered correctly. Fixed by registering one persistent listener up front, before any writes go out, using a pending-resolver pattern.
- **Bug 2 (the real root cause — a genuine firmware/library gotcha worth remembering):** calling `BLECharacteristic::notify()` **synchronously from inside a different characteristic's `onWrite` callback** silently fails to transmit on this board's BLE library/NimBLE combo. Proved this with live serial logging: firmware logged `Update.begin()` succeeding in 0ms and `otaNotify("READY")` being called — but the browser never received it and timed out regardless of bug 1's fix. Root-caused by testing "did the firmware even try" via `Serial.printf` before assuming a wireless/timing issue. Fix: defer the actual `notify()` call (and the post-`END` `ESP.restart()`) to `loop()` — a normal task context — via `otaNotifyPending`/`otaNotifyMsg`/`otaRestartPending` flags set from the callback and drained at the top of `loop()`. After this fix, the log confirmed the notify actually arrived browser-side and data started flowing correctly.
- **Why it was reverted, not merged:** once notifications worked, the real transfer speed measured out at ~900 bytes/sec (180-byte chunks, ~200ms per BLE write-with-response round trip) — confirmed **latency-bound, not bandwidth-bound** (the round-trip cost is roughly independent of small payload size, so chunk size mostly just amortizes a fixed per-write cost). At that rate the ~900KB firmware image would take an estimated 15-20 minutes, judged impractical. A larger chunk size could plausibly get ~2-3x faster (untested — the actual negotiated MTU ceiling on this connection was never measured), and write-without-response with custom flow control could plausibly get 5-10x faster but is a bigger change that wasn't attempted. Decided to stop rather than keep iterating.
- **Current repo state:** all OTA code was preserved via `git stash` (message: "BLE OTA implementation (in-progress, shelved) - 2026-08-18") rather than committed — nothing OTA-related is on `main`. Both boards reflashed back to their exact pre-OTA firmware (C6 back to `PartitionScheme=no_ota`, 42% storage, matching commit `d703ca4`). Confirmed via `git diff` that none of the 2026-08-17 QA default/tuning values were touched by the stash. If OTA is revisited, `git stash pop` restores this exact starting point — no need to re-derive the two bugs above.
- Separately: the webpage's "Last updated" stamp (`index.html`) had been hardcoded to 2026-08-08 and never updated across three real releases since (08-14, two on 08-17) — user noticed it was stale. Updated to 2026-08-17 22:45 UTC, matching the last commit that actually changed `index.html` content. Worth keeping in mind going forward: this stamp is a plain hardcoded string, not automated, so it'll silently go stale again unless it's part of the routine when `index.html` content changes.

---

## Current state (as of latest files on hand)
- **Patterns:** Solid, Breathe, Waves, Rainbow — all 4 implemented in both files. Waves/Rainbow strip rendering is now safe for odd LED counts (rounds the midpoint up, not down) and Waves' trail auto-caps to fit the travel distance so its edge-to-middle reset never shows a visible pop.
- **Connection:** auto-reconnect + ping-based fast disconnect detection + cancelable reconnect UI, all in place in the `index.html` you last shared.
- **Color picker:** custom hue+sat square / hue slider / 4-slot picker (slot 0 locked to the firmware's default color, slots 1-3 start empty) behind a collapsible color bar — replaces the native color input. Default is currently blue (`#006EFF`), matching the C6 firmware.
- **Firmware (classic ESP32):** state-sync and command parsing use fixed buffers (no `String` churn in hot paths), frame pacing is time-based (20ms target), CPU runs at 80MHz (down from 240MHz default), off-state loop skips redundant LED refreshes. Sketch lives at `Nami_Headset_LEDs/Nami_Headset_LEDs.ino`; local `arduino-cli` toolchain in place for compiling/flashing over COM7 — **must always use FQBN `esp32:esp32:esp32:PartitionScheme=min_spiffs`**, storage usage was 63% of the 1.9MB app slot as of 2026-08-08. `STRIP_LEDS` is 68; teal/dimmer QA defaults still in place (not the board currently in the headset, so not yet updated to match C6's new look).
- **Firmware (C6, currently the board in the headset):** `Nami_Headset_LEDs_C6/Nami_Headset_LEDs_C6.ino`, compiles/flashes via `arduino-cli` over COM8 with FQBN `esp32:esp32:XIAO_ESP32C6:PartitionScheme=no_ota` (this board has no `min_spiffs` option; SPIFFS isn't used by this project anyway). Storage usage was 42% of the 2MB app partition as of 2026-08-18. `LED_PIN` is GPIO18 (D10), `STRIP_LEDS` is 69. QA defaults — see 2026-08-17 entries above (both of them).
- **Hosting:** GitHub Pages is live and confirmed working — phone can load the webpage from anywhere (no local server needed) and control the headset over Bluetooth directly. Android Chrome confirmed working; iOS needs Bluefy/WebBLE since Safari has no Web Bluetooth support.
- **Not yet done:** flash-persisted tuning params (color/brightness/speed/trail still reset to firmware defaults on reboot — analyzed 2026-08-05, not implemented); battery monitoring (fuel gauge / board swap / ADC partial read) — tabled, no decision made; BLE connection-interval tuning (discussed 2026-08-08, low confidence in payoff, not implemented); WiFi/HTTP self-hosted rewrite for universal (incl. iOS) browser support without a workaround app — scoped, not started; **BLE OTA firmware updates — built, debugged (see 2026-08-18 entry for the two bugs found), and reverted after measuring the transfer as too slow (~15-20 min) to be practical as implemented; the working implementation is preserved in a git stash if it's worth revisiting with a speed fix (larger chunk size and/or write-without-response).**

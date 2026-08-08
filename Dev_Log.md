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

---

## Current state (as of latest files on hand)
- **Patterns:** Solid, Breathe, Waves, Rainbow — all 4 implemented in both files.
- **Connection:** auto-reconnect + ping-based fast disconnect detection + cancelable reconnect UI, all in place in the `index.html` you last shared.
- **Color picker:** custom hue+sat square / hue slider / 4-slot picker (slot 0 locked to default teal, slots 1-3 start empty) behind a collapsible color bar — replaces the native color input.
- **Firmware:** state-sync and command parsing use fixed buffers (no `String` churn in hot paths), frame pacing is time-based (20ms target), CPU runs at 80MHz (down from 240MHz default), off-state loop skips redundant LED refreshes. Boot log confirms RMT driver + FastLED version + unaffected RMT timing at 80MHz. Sketch lives at `Nami_Headset_LEDs/Nami_Headset_LEDs.ino`; local `arduino-cli` toolchain in place for compiling/flashing over COM7 — **must always use FQBN `esp32:esp32:esp32:PartitionScheme=min_spiffs`**, storage usage is 63% of the 1.9MB app slot as of 2026-08-08 (was 95% of a 1.25MB slot before the partition switch).
- **Hosting:** GitHub Pages is live and confirmed working — phone can load the webpage from anywhere (no local server needed) and control the headset over Bluetooth directly. Android Chrome confirmed working; iOS needs Bluefy/WebBLE since Safari has no Web Bluetooth support.
- **Not yet done:** flash-persisted tuning params (color/brightness/speed/trail still reset to firmware defaults on reboot — analyzed 2026-08-05, not implemented); battery monitoring (fuel gauge / board swap / ADC partial read) — tabled, no decision made; BLE connection-interval tuning (discussed 2026-08-08, low confidence in payoff, not implemented); WiFi/HTTP self-hosted rewrite for universal (incl. iOS) browser support without a workaround app — scoped, not started.

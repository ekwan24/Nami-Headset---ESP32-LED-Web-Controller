# Nami Headset — Project Instructions

## What this is
An ESP32-based LED headset (cosplay use). 176 WS2812B LEDs (RingA 54 / Strip 69 / RingB 54 — the strip was physically shortened from its original 120) controlled over BLE from a self-contained webpage (Web Bluetooth) hosted on GitHub Pages so it's reachable from a phone away from any local server.

## Key files
- `Nami_Headset_LEDs_C6/Nami_Headset_LEDs_C6.ino` — **active firmware**, for the Seeed XIAO ESP32-C6 board currently in the headset. Must live in a folder whose name matches the `.ino` filename exactly — the Arduino toolchain requires this to compile/upload.
- `Nami_Headset_LEDs/Nami_Headset_LEDs.ino` — **archived** classic-ESP32 firmware (2026-08-18). That board has been retired; this file is kept for reference only and is no longer actively maintained/synced with the C6 build. Don't spend effort keeping it in parity with new features or tuning changes unless specifically asked.
- `index.html` — the control webpage. Fully self-contained (no external CSS/JS dependencies), talks to the headset directly over Web Bluetooth. Board-agnostic — BLE UUIDs/protocol are the same for both firmwares.
- `Dev_Log.md` — technical log for us: bugs hit, lessons learned, tooling notes, compile errors. Never shown in-app. As of 2026-08-18 it's a living document (`## Lessons & gotchas` / `## Current state` / `## Not yet done`), not an append-only chronicle — edit those sections in place rather than appending dated entries.
- `User_Log.md` — plain-language changelog shown in-app (the "Last updated" stamp at the bottom of the page reads live off this file's newest entry — see `updateStamp()` in `index.html`). No technical details — just what changed from a user's perspective, newest entry first.

**Where does a given fact belong — here or in `Dev_Log.md`?** Two different jobs, so don't duplicate the same fact in both:
- **This file (`CLAUDE.md`)** is loaded every session regardless of topic — it only holds what's relevant *every* time: current operational facts (active board, FQBN, COM port) and standing rules (workflow, tone, communication preferences). Test: "would getting this wrong misdirect any session, regardless of what we're working on?"
- **`Dev_Log.md`** is read on demand — it holds what's only useful when specifically debugging or deciding something similar later: the *why* behind a fact, history of what was tried and reverted, reusable techniques. Test: "is this only useful in the specific context of understanding a past decision or diagnosing a similar future bug?"

If a fact is already stated in full here, it doesn't need restating in `Dev_Log.md` — only add the reasoning/context this file doesn't carry.

## Standing workflow rule (updated 2026-08-08)
1. **Batch work before rebuilding.** Multiple features/mockups can be worked on locally without rebuilding after each one — don't rebuild reflexively after every small change.
2. **Always ask before rebuilding.** Before compiling/flashing firmware or pushing webpage changes live, ask first — don't assume the user wants to rebuild yet.
3. **Once approved, finish the rebuild without further asks.** After a yes: compile + flash the firmware (if it changed) and `git push` the webpage (if it changed) automatically — no additional permission needed for those specific actions.
4. **Logs are a separate, later ask.** After the rebuild is done, ask whether to update `Dev_Log.md` / `User_Log.md` — don't update/commit/push the logs automatically.

(This supersedes an earlier same-day rule that said to auto-update logs and push on every change — refined after more thought on workflow.)

## Working with this user
- Coding + hobby electronics background: **beginner to intermediate**. They're building toward more complex projects, so prioritize clear, concrete explanations over jargon-heavy technical depth — define terms in passing rather than assuming familiarity.
- Keep explanations **concise** — enough to understand and decide, not exhaustive.
- **Whenever giving an assessment, judgment call, or recommendation, state a confidence level from 1 (low) to 5 (high).**

## Firmware toolchain
- `arduino-cli` at `C:\Program Files\Arduino CLI\arduino-cli.exe`, config at `%USERPROFILE%\.arduino15cli\arduino-cli.yaml` (points at the existing Arduino IDE data dir so it shares already-installed cores/libraries — don't reinstall ESP32 core or FastLED).
- Reconfirm the COM port each session rather than assuming — it can shift: `Get-CimInstance -ClassName Win32_PnPEntity | Where-Object { $_.Name -match "COM\d+" }`
- **Known gotcha:** if the COM port disappears mid-upload, it's a physical USB connection drop (reseat the cable), not a firmware/tooling bug — confirmed this happened once and a reseat fixed it.

**Active board — ESP32-C6** (Seeed XIAO ESP32-C6, `Nami_Headset_LEDs_C6/Nami_Headset_LEDs_C6.ino`)
- Board: **XIAO_ESP32C6**, 4MB flash, native USB CDC (no CP210x bridge) — enumerates as `USB Serial Device` (VID_303A), typically **COM8**.
- **FQBN: `esp32:esp32:XIAO_ESP32C6:PartitionScheme=no_ota`**
- Compile: `arduino-cli --config-file <cfg> compile --fqbn "esp32:esp32:XIAO_ESP32C6:PartitionScheme=no_ota" Nami_Headset_LEDs_C6`
- Upload: same, `upload -p COM8 --fqbn "esp32:esp32:XIAO_ESP32C6:PartitionScheme=no_ota" Nami_Headset_LEDs_C6`
- Storage usage as of 2026-08-18: **42%** of the 2MB app partition.

**Archived board — classic ESP32** (`Nami_Headset_LEDs/Nami_Headset_LEDs.ino`) — retired 2026-08-18, kept for reference only
- Board: **ESP32 Dev Module**, flash chip confirmed **4MB** (2026-08-08, via `esptool.exe flash_id`), Silicon Labs CP210x bridge, typically **COM7**.
- FQBN: `esp32:esp32:esp32:PartitionScheme=min_spiffs` — must include `PartitionScheme=min_spiffs` if this board is ever brought back; omitting it silently reverts to a smaller, mismatched partition table.
- Storage usage as of 2026-08-08: 63% of the 1.9MB app slot.

## Local webpage preview
`.claude/launch.json` defines a `nami-webpage` server (`py -m http.server 5500`) for previewing the page in the Browser pane. Note this only lets you check UI/layout — Web Bluetooth won't actually pair with the real headset from a sandboxed/remote browser context, only from a real local Chrome (or Bluefy/WebBLE on iOS).

## Architecture notes
- Pure BLE, no WiFi — the phone talks directly to the headset over Bluetooth; GitHub Pages only serves the static page files, it's never in the runtime control path.
- **iOS Safari has no Web Bluetooth support** (Apple platform limitation, not fixable here). iPhone users need a workaround browser: Bluefy (paid) or WebBLE (free).
- Repo is public on GitHub — confirmed (as of 2026-08-08) no secrets in any tracked file or full commit history.
- The BLE command characteristic has no pairing/authentication — anyone within Bluetooth range can send commands. Known, accepted tradeoff for now; would need bonding/pairing to close.

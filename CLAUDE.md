# Nami Headset — Project Instructions

## What this is
An ESP32-based LED headset (cosplay use). 228 WS2812B LEDs (RingA 54 / Strip 120 / RingB 54) controlled over BLE from a self-contained webpage (Web Bluetooth) hosted on GitHub Pages so it's reachable from a phone away from any local server.

## Key files
- `Nami_Headset_LEDs/Nami_Headset_LEDs.ino` — firmware. Must live in a folder whose name matches the `.ino` filename exactly — the Arduino toolchain requires this to compile/upload.
- `index.html` — the control webpage. Fully self-contained (no external CSS/JS dependencies), talks to the headset directly over Web Bluetooth.
- `Dev_Log.md` — technical log for us: bugs hit, lessons learned, tooling notes, compile errors. Never shown in-app.
- `User_Log.md` — plain-language changelog shown in-app (the "Last updated" stamp at the bottom of the page opens it in a modal, fetched live). No technical details — just what changed from a user's perspective, newest entry first.

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
- Board: **ESP32 Dev Module**, FQBN `esp32:esp32:esp32`.
- Flash over USB via the Silicon Labs CP210x bridge, typically **COM7** — reconfirm with:
  `Get-CimInstance -ClassName Win32_PnPEntity | Where-Object { $_.Name -match "COM\d+" }`
- Compile: `arduino-cli --config-file <cfg> compile --fqbn esp32:esp32:esp32 Nami_Headset_LEDs`
- Upload: same, `upload -p COM7 --fqbn esp32:esp32:esp32 Nami_Headset_LEDs`
- **Known gotcha:** if the COM port disappears mid-upload, it's a physical USB connection drop (reseat the cable), not a firmware/tooling bug — confirmed this happened once and a reseat fixed it.
- Sketch is at **~95% of program storage** as of 2026-08-08 — check the compile output's storage percentage after any addition; a partition scheme change may eventually be needed.

## Local webpage preview
`.claude/launch.json` defines a `nami-webpage` server (`py -m http.server 5500`) for previewing the page in the Browser pane. Note this only lets you check UI/layout — Web Bluetooth won't actually pair with the real headset from a sandboxed/remote browser context, only from a real local Chrome (or Bluefy/WebBLE on iOS).

## Architecture notes
- Pure BLE, no WiFi — the phone talks directly to the headset over Bluetooth; GitHub Pages only serves the static page files, it's never in the runtime control path.
- **iOS Safari has no Web Bluetooth support** (Apple platform limitation, not fixable here). iPhone users need a workaround browser: Bluefy (paid) or WebBLE (free).
- Repo is public on GitHub — confirmed (as of 2026-08-08) no secrets in any tracked file or full commit history.
- The BLE command characteristic has no pairing/authentication — anyone within Bluetooth range can send commands. Known, accepted tradeoff for now; would need bonding/pairing to close.

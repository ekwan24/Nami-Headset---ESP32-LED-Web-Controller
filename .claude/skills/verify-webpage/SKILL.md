---
name: verify-webpage
description: Runs a baseline sanity check on index.html in the Browser pane before it gets pushed live — page loads clean, no console/network errors, the "Last updated" stamp resolves, the update-log modal opens and shows real content, and the color picker renders. Use this whenever the user wants to verify, test, sanity-check, or QA the webpage before pushing, or after making index.html changes and before asking "want to push this live?" per this project's CLAUDE.md workflow. Does not and cannot verify real Web Bluetooth pairing — that only works from a real local Chrome (or Bluefy/WebBLE on iOS), not this sandboxed preview.
---

# Verify the webpage before pushing it live

`index.html` is the only thing standing between "compiles/renders in the preview" and "actually usable on someone's phone at a convention" — this is a fast, repeatable pass to catch anything broken before it goes live on GitHub Pages, without requiring a real headset or a real phone.

## What this can and can't prove

Per `CLAUDE.md`'s "Local webpage preview" note: the Browser pane can confirm the page loads, renders, and runs its own JS correctly — but Web Bluetooth itself won't actually pair with real hardware from this sandboxed context. So this skill verifies everything *except* live BLE pairing. Don't claim BLE control was tested — say explicitly that pairing itself is unverified and needs a real phone/Chrome.

## Steps

**1. Start (or reuse) the local preview.**
Use `preview_start` with `{name: "nami-webpage"}` (defined in `.claude/launch.json`, serves via `py -m http.server 5500`). If it's already running from earlier in the session, reload the existing tab instead of starting a second server.

**2. Check for load-time errors.**
- `read_console_messages` with `onlyErrors: true` — should be empty. Any error here (a JS exception, a failed script) is a blocker.
- `read_network_requests` — confirm `index.html` and `User_Log.md` both returned 200, not 404. A missing/misnamed file here is exactly the kind of thing that only shows up once it's actually being fetched over HTTP, not when just reading the source.

**3. Confirm the "Last updated" stamp resolved.**
`get_page_text` and look for `Last updated: YYYY-MM-DD` with a real date matching `User_Log.md`'s newest entry — not stuck on "loading…" or "unavailable" (see `updateStamp()` in `index.html`). If it's wrong, that's a fetch/parsing bug worth chasing before pushing, not a cosmetic issue — it's the whole point of that feature.

**4. Open the update-log modal.**
Use `read_page` with `filter: "interactive"` (not `"all"` — the full tree pulls in every list item from every dated log entry, which is a lot of tokens for finding one button) to get a `ref` for "View update log" (`openLog()`), click it, then `get_page_text` (cheaper than another `read_page` — plain text is enough to confirm real content rendered, no `ref` needed for a text check) to confirm the modal shows `User_Log.md`'s content, not the "Could not load the update log" error state. Close it via `javascript_tool: closeLog()` rather than hunting for the close button's `ref`.

**5. Expand the color picker.**
The card holding it (`#tuningCard`) is `pointer-events:none` while disconnected by design (the "dim/disable cards when disconnected" feature) — a normal click won't reach it and will silently do nothing, which reads as a false failure if you don't know this going in. Call `togglePicker()` via `javascript_tool` (inspection only — this isn't implementing anything, just driving the same function a click would) and check `document.querySelector('.color-popover').classList.contains('open')` — that alone confirms the toggle works and is enough for a routine run.
- **Only take a `computer` screenshot if the change being verified actually touched CSS/layout** (color picker, slider, or general styling code) — a screenshot embeds a full image and costs meaningfully more than the DOM check above, so it's worth it when there's real visual risk (this area has broken silently before — see `Dev_Log.md`'s Lessons & gotchas, the slider-gradient and glow-clipping entries) but not as a default reflex on every run.
- If you need real clicks (e.g. testing slot-selection or drag behavior), first run `setConnState('connected')` via `javascript_tool` to lift the `pointer-events:none` restriction for inspection purposes.
Reload the page afterward (or re-run `setConnState('disconnected')`) so the tab is left in its normal starting state, not artificially connected.

**6. Re-check console after interaction.**
`read_console_messages` once more after steps 4-5 — clicking through UI can surface errors that a static page load wouldn't (event handler bugs, null-ref on an element that doesn't exist yet).

**7. Skip Web Bluetooth interaction entirely.**
Don't click "Connect" — it calls `navigator.bluetooth.requestDevice()`, which either throws immediately (sandboxed context, no real adapter) or opens a native device chooser that automation can't dismiss. Neither outcome tells you anything useful; it's not what this skill is for.

## Reporting back

State plainly what passed and what didn't — "console clean, network 200s, stamp shows 2026-08-18, modal renders, color picker intact" reads very differently from a bare "looks good." If anything failed, read the relevant source before proposing a fix, then re-run from step 2 after fixing. Always end with a one-line reminder that BLE pairing itself still needs a real phone/Chrome before calling the change fully verified.

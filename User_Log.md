# Nami Headset — Update Log

## 2026-08-18
- Fixed the "Last updated" date at the bottom of the page, which had been stuck showing an old date.

## 2026-08-17
- Shortened the LED strip to match the trimmed headset — patterns now correctly cover just the LEDs that are actually there.
- Fixed a wiring issue that left the strip and second ring dark after the strip was trimmed.
- New default look: patterns now start in blue instead of teal, and brighter by default.
- Fixed the Waves pattern on the strip so it fades away smoothly when the two sides meet in the middle, instead of visibly popping and restarting.
- Fixed a dark LED right in the center of the strip that Waves and Rainbow weren't lighting up.
- Base Brightness can now be turned down further (as low as 5%, was capped at 25%).
- Breathe and Rainbow patterns are brighter and a bit faster by default.

## 2026-08-14
- The headset firmware now also works on a new, smaller controller board (ESP32-C6) — no change for anyone using the existing setup.

## 2026-08-08
- The control page is now hosted online — no computer needed. Open it from your phone from anywhere and it connects straight to the headset over Bluetooth, confirmed working at a real test.
- Note for iPhone users: Safari doesn't support the Bluetooth connection this app needs. Install a free/cheap browser app like Bluefy or WebBLE and open the page there instead — everything else works the same.
- Improved battery life: the headset now uses less power whenever the lights are turned off, and runs more efficiently overall.
- General behind-the-scenes reliability improvements for long wear sessions — no visible changes, just smoother, sturdier performance.
- Added this update log, viewable right from the app by tapping the "Last updated" text at the bottom of the page.

## 2026-08-06
- New custom color picker — pick any color from a color square and hue slider, and save up to 3 favorite colors for quick access alongside the default teal.
- The app now automatically reconnects if the headset disconnects unexpectedly, with a Cancel option if you'd rather connect manually instead.
- Estimated battery life: roughly 2 hours at higher brightness, up to 4–7 hours at everyday brightness levels.

## 2026-08-05
- Added a 4th lighting pattern: Rainbow — a smooth, full-color cycling effect.
- Added a Reset button so any pattern can be instantly restored to its default look.
- Fixed the second ring so both rings move together the way they're meant to.

## 2026-08-04
- Fixed sliders sometimes showing the wrong values right after connecting — they now always match what the headset is actually doing.
- Smoothed out the Breathe pattern's pulsing.

## 2026-08-03
- First version: Solid, Breathe, and Waves lighting patterns, with brightness and speed controls.
- Added a status card showing connection state and signal strength right on the page.

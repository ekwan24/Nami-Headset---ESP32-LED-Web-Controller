---
name: rebuild-and-flash
description: Compiles the Nami Headset ESP32-C6 firmware (Nami_Headset_LEDs_C6/Nami_Headset_LEDs_C6.ino) via arduino-cli, flashes it to the physical board, and verifies the flash actually worked by reading the serial boot log. Use this whenever the user wants to rebuild, flash, upload, reflash, or test firmware changes on the real headset — phrases like "let's flash this", "push it to the board", "try it on the device", "rebuild the firmware", or a plain "yes" after being asked "want to rebuild?" per this project's CLAUDE.md workflow rule. Always confirm with the user before running this unless they've already explicitly said yes to rebuilding — see the workflow note below.
---

# Rebuild and flash the Nami Headset firmware

This project's `CLAUDE.md` has a standing rule: always ask the user before rebuilding or flashing, and only proceed once they say yes. If the current conversation hasn't already gotten that yes, ask first — don't treat "the code changed" as permission on its own. Once you have the go-ahead, everything below runs without further asks.

## Which board

The ESP32-C6 (Seeed XIAO) is the active board — it's what's physically in the headset. The classic ESP32 build (`Nami_Headset_LEDs/Nami_Headset_LEDs.ino`) was archived 2026-08-18 and shouldn't be flashed or kept in sync unless the user specifically asks for it. Default to C6 below without asking which board — only use the classic board's FQBN/sketch if the user explicitly says so.

## Why each step exists

The ESP32-C6 shows up as a USB-serial device (native USB CDC, no CP210x bridge — enumerates as "USB Serial Device", VID_303A) whose COM port number can shift between sessions, so it's confirmed fresh each time rather than assumed. The FQBN must always include `PartitionScheme=no_ota` — omitting it would compile against a different partition layout than what's on the board. Still worth watching the flash-storage percentage after any addition — if it ever crosses 100%, compilation fails with a clear error, but it's worth flagging early if it's climbing. And the boot-log check exists because a "successful" upload (exit code 0, hash verified) only proves the *bytes* made it onto the flash chip — it doesn't prove the firmware actually boots and runs correctly. Reading the serial output after reset is the only way to confirm that.

## Steps

**1. Confirm the COM port.**
```powershell
Get-CimInstance -ClassName Win32_PnPEntity | Where-Object { $_.Name -match "COM\d+" } | Select-Object Name, DeviceID
```
Look for **"USB Serial Device"** (VID_303A) — note its COM number (it's been COM8 in past sessions, but confirm rather than hardcode). If it's missing entirely, the board may not be plugged in — tell the user and stop here.

**2. Compile.**
```powershell
$cli = "C:\Program Files\Arduino CLI\arduino-cli.exe"
$cfg = "$env:USERPROFILE\.arduino15cli\arduino-cli.yaml"
& $cli --config-file $cfg compile --fqbn "esp32:esp32:XIAO_ESP32C6:PartitionScheme=no_ota" "<repo>\Nami_Headset_LEDs_C6"
```
Replace `<repo>` with the actual repo path. Check for exit code 0. In the output, find the line like `Sketch uses X bytes (Y%) of program storage space` — if Y is climbing toward 100, mention it to the user even if the compile succeeded; that's useful information before it becomes a blocker.

**3. Upload.**
Same command, swap `compile` for `upload -p COM<N>` (using the port from step 1):
```powershell
& $cli --config-file $cfg upload -p COM<N> --fqbn "esp32:esp32:XIAO_ESP32C6:PartitionScheme=no_ota" "<repo>\Nami_Headset_LEDs_C6"
```

**Known gotcha:** if the upload fails partway with the COM port *completely disappearing* from Windows (not just "port busy" — actually gone), that's a physical USB connection drop, not a firmware or tooling problem. Re-run the step 1 port scan:
- If the port reappears, retry the upload — this has resolved the issue before.
- If it doesn't reappear, tell the user to check the physical USB cable/connection before trying again. Don't keep retrying blindly; a dead connection won't fix itself.

**4. Verify by reading the boot log.**
A successful upload only proves the bytes were written — it doesn't prove the firmware runs. Reset the board and read its serial output:
```powershell
$port = new-Object System.IO.Ports.SerialPort COM<N>,115200,None,8,one
$port.Open()
$port.DtrEnable = $false
$port.RtsEnable = $true
Start-Sleep -Milliseconds 200
$port.RtsEnable = $false
Start-Sleep -Milliseconds 200
$sw = [System.Diagnostics.Stopwatch]::StartNew()
$buf = ""
while ($sw.Elapsed.TotalSeconds -lt 6) {
  try { $chunk = $port.ReadExisting(); if ($chunk) { $buf += $chunk } } catch {}
  Start-Sleep -Milliseconds 100
}
$port.Close()
Write-Output $buf
```
Confirm the captured output includes all three of:
- `FastLED version: <number>`
- a line mentioning the LED driver — `Added driver 'PARLIO'` on this board (FastLED's pick for the C6; not an error)
- `BLE advertising as ESP32-LED`

...and no crash/panic/error text. Report a clear pass/fail to the user — don't just say "it flashed," say what the boot log actually confirmed.

## Reporting back

Keep the summary short and concrete: did it compile, what's the storage %, did it flash, did the boot log check out. This user is building foundational knowledge, so it's fine to note *what* each check proved (e.g. "boot log confirms it's actually running, not just that the upload succeeded") rather than just stating results with no context.

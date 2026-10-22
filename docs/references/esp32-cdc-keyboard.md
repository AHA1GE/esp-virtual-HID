# esp32-cdc-keyboard

- **Repository:** <https://github.com/cnfatal/esp32-cdc-keyboard>
- **License:** none declared (all rights reserved)

## What it is

An ESP32 USB composite device that exposes both HID and CDC interfaces on a
single USB port. It receives messages over the CDC interface and translates
them into HID reports, simulating a keyboard and mouse in hardware.

## Why it was referenced

Studied as an example of a composite HID + CDC device on ESP32, which informed
the CDC serial channel in this project.

Reference only — no code from this project is included. Note that the upstream
repository declares no license, which is why the copy that was previously kept
under `reference/` has been removed from this repository's history.

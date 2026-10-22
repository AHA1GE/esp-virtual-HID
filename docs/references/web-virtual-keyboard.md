# Web Virtual Keyboard

- **Repository:** <https://github.com/DotaPie/Web-Virtual-Keyboard>
- **License:** EUPL-1.2 (European Union Public Licence)

## What it is

Turns a LILYGO ESP32 T-Dongle S3 (or any ESP32-S3 with native USB) into a
Wi-Fi-controlled USB keyboard. The device enumerates as a normal USB HID
keyboard and is driven from a web browser on the same network. It supports
free-form text entry and saved presets with special-key combos, protected by
HTTP Basic authentication.

## Why it was referenced

Studied for its approach to a mobile-friendly web UI driving a USB HID
keyboard, and as a comparison point for text input handling.

Reference only — no code from this project is included in ESP Virtual HID.
The web UI and firmware here were written independently: the UI uses a HID
usage-code map rather than a JSON action table, and the firmware targets
ESP-IDF rather than PlatformIO/Arduino.

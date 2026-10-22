# ESP32-S3 USB HID Macro Keyboard + Web Typing Interface

- **Repository:** <https://github.com/konkop/ESP32-S3-USB-HID-Macro-Keyboard-Web-Typing-Interface>
- **License:** MIT

## What it is

An ESP32-S3 project that presents itself as a USB HID keyboard (via TinyUSB)
and exposes a web interface hosted on the board. Text entered in the browser
is typed into the host in a human-like way, and physical Cherry MX keys can
trigger macro commands.

## Why it was referenced

An early reference for the idea of a browser-driven HID keyboard with the web
UI served from the device itself.

Reference only — no code from this project is included in ESP Virtual HID.
The implementation here is written from scratch against ESP-IDF, NimBLE and
TinyUSB.

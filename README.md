# ESP Virtual HID

A wireless keyboard and mouse built on an ESP32 board. Join the board's Wi-Fi
hotspot from a phone or laptop, open the web page, and everything you type or
tap is sent to the computer the board is plugged into — over USB and/or
Bluetooth at the same time.

## What you need

- An ESP32 board with native USB for USB HID — we recommend **ESP32-S3** or
  **ESP32-S31**. A chip without USB-OTG can still compile if it has BLE; it
  then works over Bluetooth only.
- The board's USB connector plugged into the host computer.
- A phone or laptop with Wi-Fi.

## How to use it

1. Power the board. The LED breathes while it waits for a device.
2. Join the Wi-Fi network **ESP-HID-XXXXXX** (open by default).
3. Open <http://192.168.4.1> in a browser.
4. **Keyboard tab**: type in the text box, or tap the special-key buttons
   (Function keys, modifiers, editing/navigation, locks, arrows). The music
   note button opens the media keys; the gear button opens sticky/toggle
   settings.
5. **Mouse tab**: tap = left click, double-tap = double-click, long-press or
   two-finger tap = right click, swipe = move the pointer, pinch = scroll.
   There are also explicit click/wheel buttons and a speed slider.
6. **Bluetooth**: pair **ESP32 Keyboard** on the host as well if you want
   Bluetooth input. USB and Bluetooth can be used at the same time.

## LED

| LED | Meaning |
|---|---|
| Solid | Starting up |
| Breathing | Hotspot ready, waiting for a device |
| Blinking | A device is connected to the hotspot |
| Fast red flashing | Hard error (the device is not usable) |

The status LED is white; on boards with an RGB WS2812 a hard error flashes
red, while a plain LED just flashes fast.

## Change the hotspot

Tap the menu button (☰) on the web page to change the network name and
password, and to adjust the status LED brightness. The hotspot restarts, so
reconnect to the new name. Leave the password empty for an open network.

## Reset

Hold the BOOT button for 5 seconds to erase all settings and reboot.

## Notes

- Typing assumes a **US keyboard layout** on the host.
- On phones and tablets each character is sent as a key tap, so holding a key
  does not repeat. Desktop browsers keep normal press/release and key repeat.
- The host sees the board as **ESP32 Virtual Keyboard** (keyboard, mouse,
  media keys, and a serial port).

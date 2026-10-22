# ESP Virtual HID — project brief

Target: any ESP32 board with native USB. Boards without USB-OTG still build
and work over BLE. Bring-up target: ESP32-C3, no PSRAM, 4 MB flash.

## UI and features

- SoftAP limited to one client.
- SSID and password configurable from the web page.
- Default SSID: `ESP-HID-XXXXXX` (last three bytes of the WiFi MAC), open by
  default.
- Status LED:
  - solid: starting up
  - breathing: waiting for a client
  - blinking: client connected
  - fast red flash (10 Hz): hard error
  - The GPIO is configurable — default 48 (onboard WS2812) on ESP32-S3 and
    8 on ESP32-C3.
- Web page:
  - text box for client-keyboard input, mirrored live to the host
  - special-key buttons: F1–F12, ESC, TAB, CTRL, ALT, SHIFT, WIN, DEL,
    BACKSPACE, ENTER, INS, HOME, END, PAGE UP, PAGE DOWN, arrows, PRINT
    SCREEN, SCROLL LOCK, PAUSE, CAPS LOCK, NUM LOCK
  - default-hidden modal for media keys: MUTE, VOL UP, VOL DOWN, PLAY/PAUSE,
    STOP, NEXT, PREV
  - default-hidden modal for feature switches: sticky keys, toggle keys
  - two tabs: Keyboard and Mouse
  - mouse tab gestures: tap = left click, double-tap = double click,
    long-press or two-finger tap = right click, swipe = pointer move,
    pinch = wheel

## Build

ESP-IDF v6.1.

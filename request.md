# esp32 cirtual keyboard

target: all esp32 board has native usb cdc support
bring up target: esp32c3, no psram, 4M flash

## UI and Features
- soft AP, limited to 1 client
- user can consifure SSID and password via web page
- default SSID: "ESP32KBD-[MAC]", defualt open (no password)
- led indicate status: 
  - solid: starting up
  - breathing: waiting for client
  - blinking: client connected
  - GPIO8, active low
- web page:
  - input box for client-keyboard input
  - 3 rows of buttons for special keys: F1-F12, ESC, TAB, CTRL, ALT, SHIFT, WIN, DEL, BACKSPACE, ENTER, INS, HOME, END, PAGE UP, PAGE DOWN, ARROW UP, ARROW DOWN, ARROW LEFT, ARROW RIGHT, PRINT SCREEN, SCROLL LOCK, PAUSE, CAPS LOCK, NUM LOCK
  - default-hide modal for media keys: MUTE, VOL UP, VOL DOWN, PLAY/PAUSE, STOP, NEXT, PREV
  - default-hide modal for features switch: sticky keys, toggle keys
  - two tabs on the web page: "Keyboard" and "Mouse"
  - mouse tab:
    - input box for client-mouse input
    - support touch-gestures to mouse-key: tap=left click, double-tap=double-click, long-press=right-click, two-finger-tap=right-click, swipe=mouse-move, pinch=mouse-wheel

## BUILD
- use ESP-IDF v6.1
idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES settings status_led hid_core hid_usb hid_ble wifi_ap webserver nvs_flash)

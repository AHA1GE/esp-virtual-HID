# esp_bt.h and the NimBLE host exist in-tree on every target; the sources are
# preprocessor-guarded so this compiles to an empty object on non-BLE chips.
idf_component_register(SRCS "hid_ble.c" "ble_gap.c"
                       INCLUDE_DIRS "include"
                       REQUIRES bt esp_hid hid_core)

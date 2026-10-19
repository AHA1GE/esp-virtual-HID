# esp_tinyusb comes from idf_component.yml, where a target rule keeps the
# component manager from fetching it on non-OTG chips (C3/C6/H2...). The
# source file is therefore only added on OTG targets; hid_usb.h degrades to
# a stub elsewhere.
set(srcs "")
if(CONFIG_SOC_USB_OTG_SUPPORTED)
    list(APPEND srcs "hid_usb.c")
endif()
idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS "include"
                       REQUIRES hid_core)

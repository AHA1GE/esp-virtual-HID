idf_component_register(SRCS "webserver.c" "kbd_state.c"
                       INCLUDE_DIRS "include"
                       EMBED_FILES "web/index.html"
                       REQUIRES esp_http_server cjson hid_core settings wifi_ap)

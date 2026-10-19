cmake_minimum_required(VERSION 3.16)

# This project has no display. ESP-IDF compiles every component by default and
# xtensa-esp-elf 15.2.0 crashes (ICE in the IRA pass) on esp_lcd's RGB panel
# driver at -Og, so keep that unused component out of the build.
set(EXCLUDE_COMPONENTS esp_lcd)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32_virtual_keyboard)

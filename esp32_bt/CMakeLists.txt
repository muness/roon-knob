# ESP32 Bluetooth Firmware for Roon Knob
# This firmware runs on the ESP32 chip (not S3) and handles:
# - Classic Bluetooth (AVRCP Controller)
# - UART communication with ESP32-S3

cmake_minimum_required(VERSION 3.16)

# Include ESP-IDF build system
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(esp32_bt)

cmake_minimum_required(VERSION 3.5)

add_library(ArduinoX64 STATIC
    "Arduino.h"
    "Arduino.cpp"
    "Print.h"
    "Print.cpp"
    "Stream.h"
    "Stream.cpp"
    "WString.h"
    "WString.cpp"
    "dtostrf.cpp"
    "SPI.h"
    "SPI.cpp"
    "wiring_constants.h"
)

#target_link_libraries(ArduinoX64 PUBLIC ./)

target_include_directories(ArduinoX64 INTERFACE
  ${PROJECT_SOURCE_DIR}/
)

cmake_minimum_required(VERSION 3.5)

add_library(TFT_eSPI STATIC
    User_Setup_Select.h
    TFT_eSPI.h TFT_eSPI.cpp
    Extensions/Button.h
    Extensions/Sprite.h
)

target_link_libraries(TFT_eSPI PUBLIC SDL2::SDL2 ArduinoX64)

target_include_directories(TFT_eSPI INTERFACE
  ${PROJECT_SOURCE_DIR}/
)

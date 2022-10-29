cmake_minimum_required(VERSION 3.5)

project(aoe_tft LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(SDL2 REQUIRED)

add_compile_definitions(-D_CRT_SECURE_NO_WARNINGS)

include_directories("arduino")
include_directories("TFT_eSPI")

add_subdirectory(arduino)
add_subdirectory(TFT_eSPI)

add_executable(aoe_tft main.cpp)

target_link_libraries(
    ${PROJECT_NAME}
    PUBLIC
        ArduinoX64
        TFT_eSPI
    PRIVATE
        SDL2::SDL2
        SDL2::SDL2main
)

install(TARGETS aoe_tft
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})

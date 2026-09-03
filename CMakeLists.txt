cmake_minimum_required(VERSION 3.16)
project(MCXA153_Touch_Audio_Workstation)

set(CMAKE_C_STANDARD 99)

# Include directories
include_directories(source)
include_directories(board)

# Source files
set(SOURCES
    source/main.c
    source/ui_display.c
    source/audio_dsp.c
    source/sdcard_wav.c
    source/wifi_esp.c
)

# Placeholder executable (requires MCUXpresso SDK to actually build for ARM)
add_executable(${PROJECT_NAME} ${SOURCES})

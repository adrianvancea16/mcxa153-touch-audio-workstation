# Copyright 2026 NXP
#
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required(VERSION 3.22.0)

cmake_path(APPEND conf_file_path ${CMAKE_CURRENT_LIST_DIR} ${board} prj.conf)
set(CONF_FILE ${conf_file_path} ${CONF_FILE})
include(${SdkRootDirPath}/cmake/extension/mcux.cmake)

project(MCXA153_Touch_Audio_Workstation LANGUAGES C CXX ASM PROJECT_BOARD_PORT_PATH ${board_root}/${board}/demo_apps/led_blinky)

include(${SdkRootDirPath}/CMakeLists.txt)

include(${board}/board_files.cmake)

mcux_add_source(
    SOURCES source/main.c
            source/ui_display.c
            source/audio_dsp.c
            source/sdcard_wav.c
            source/wifi_esp.c
)

mcux_add_source(
    SOURCES board/peripherals.c
            board/peripherals.h
)

mcux_convert_binary(BINARY ${APPLICATION_BINARY_DIR}/${MCUX_SDK_PROJECT_NAME}.bin)
mcux_add_config_mex_path( PATH ${board} )
mcux_add_include(INCLUDES . source board frdmmcxa153/board frdmmcxa153/frdmmcxa153)

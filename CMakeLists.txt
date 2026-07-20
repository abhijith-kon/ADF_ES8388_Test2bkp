cmake_minimum_required(VERSION 3.5)

# Set ADF_PATH explicitly for the build system
set(ENV{ADF_PATH} "C:/esp/esp-adf")

# Global definitions to fix ADF component build errors
add_definitions(-DBOARD_PA_GAIN=10)
add_definitions(-DESP_SD_PIN_CLK=38)
add_definitions(-DESP_SD_PIN_CMD=39)
add_definitions(-DESP_SD_PIN_D0=40)
add_definitions(-DESP_SD_PIN_D1=41)
add_definitions(-DESP_SD_PIN_D2=42)
add_definitions(-DESP_SD_PIN_D3=21)
add_definitions(-DESP_SD_PIN_D4=-1)
add_definitions(-DESP_SD_PIN_D5=-1)
add_definitions(-DESP_SD_PIN_D6=-1)
add_definitions(-DESP_SD_PIN_D7=-1)
add_definitions(-DESP_SD_PIN_CD=-1)
add_definitions(-DESP_SD_PIN_WP=-1)

# Include the ADF project CMake logic
include("$ENV{ADF_PATH}/CMakeLists.txt")

# Set EXTRA_COMPONENT_DIRS to ensure both ADF and local components are found
set(EXTRA_COMPONENT_DIRS 
    "$ENV{ADF_PATH}/components"
    "${CMAKE_CURRENT_LIST_DIR}/components"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(play_mp3_control)

# Fix ESP-ADF compatibility with ESP-IDF v5.x FreeRTOS changes globally across all components
idf_build_set_property(COMPILE_OPTIONS "-DxSemaphoreHandle=SemaphoreHandle_t" "-DportTICK_RATE_MS=portTICK_PERIOD_MS" "-DxQueueHandle=QueueHandle_t" "-Wno-error=return-type" APPEND)

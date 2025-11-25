cmake_minimum_required(VERSION 3.20.0)

project(radar_eletronico)

find_package(Zephyr REQUIRED)

set(EXTRA_MODULES "${CMAKE_CURRENT_SOURCE_DIR}/modules/camera_service")

add_subdirectory(src)
add_subdirectory(tests)

target_include_directories(app PRIVATE include)


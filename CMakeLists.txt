cmake_minimum_required(VERSION 3.16)
project(qt-caster VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Multimedia DBus)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED IMPORTED_TARGET
    gstreamer-1.0
    gstreamer-app-1.0
    gstreamer-video-1.0
    gstreamer-audio-1.0
)

add_executable(qt-caster main.cpp)
add_executable(qt-caster-worker stream_worker.cpp)

target_link_libraries(qt-caster
    Qt6::Core Qt6::Widgets Qt6::Multimedia
)

target_link_libraries(qt-caster-worker
    Qt6::Core Qt6::Widgets Qt6::DBus
    PkgConfig::GST
)
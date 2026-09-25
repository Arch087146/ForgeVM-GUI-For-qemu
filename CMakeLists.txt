cmake_minimum_required(VERSION 3.20)
project(qemu-forgevm VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Core Gui Network)
find_package(OpenSSL REQUIRED)

set(SOURCES
    src/main.cpp
    src/mainwindow.cpp
    src/vmconfig.cpp
    src/vmmanager.cpp
    src/vmtask.cpp
    src/consolewidget.cpp
    src/vmcreatordialog.cpp
    src/aichatwidget.cpp
    src/vncviewer.cpp
    src/vncdisplaywindow.cpp
    src/appsettingsdialog.cpp
)

set(HEADERS
    src/mainwindow.h
    src/vmconfig.h
    src/vmmanager.h
    src/vmtask.h
    src/consolewidget.h
    src/vmcreatordialog.h
    src/aichatwidget.h
    src/vncviewer.h
    src/vncdisplaywindow.h
    src/appsettingsdialog.h
)

qt_add_executable(qemu-forgevm ${SOURCES} ${HEADERS} icons.qrc)

target_link_libraries(qemu-forgevm PRIVATE
    Qt6::Widgets
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    OpenSSL::SSL
    OpenSSL::Crypto
)

target_include_directories(qemu-forgevm PRIVATE src)

# Cross-compilation support for Windows
if(CMAKE_CROSSCOMPILING AND CMAKE_SYSTEM_NAME STREQUAL "Windows")
    message(STATUS "Cross-compiling for Windows")
    # Static linking for easier deployment
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++")
endif()

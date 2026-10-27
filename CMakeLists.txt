cmake_minimum_required(VERSION 3.22)
project(LANShare VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Try Qt6 first, fall back to Qt5
find_package(Qt6 QUIET COMPONENTS Core Widgets Network)
if(Qt6_FOUND)
    message(STATUS "Using Qt6")
    set(QT_VERSION_MAJOR 6)
    set(QT_LIBS Qt6::Core Qt6::Widgets Qt6::Network)
else()
    find_package(Qt5 REQUIRED COMPONENTS Core Widgets Network)
    message(STATUS "Using Qt5")
    set(QT_VERSION_MAJOR 5)
    set(QT_LIBS Qt5::Core Qt5::Widgets Qt5::Network)
endif()

set(SOURCES
    src/main.cpp
    src/mainwindow.cpp
    src/mainwindow.h
    src/devicediscovery.cpp
    src/devicediscovery.h
    src/transferserver.cpp
    src/transferserver.h
    src/transferclient.cpp
    src/transferclient.h
    src/transfersession.cpp
    src/transfersession.h
    src/filebrowserdialog.cpp
    src/filebrowserdialog.h
    src/transferwidget.cpp
    src/transferwidget.h
    src/devicelistitem.cpp
    src/devicelistitem.h
    src/protocol.h
)

add_executable(LANShare ${SOURCES})

target_link_libraries(LANShare PRIVATE ${QT_LIBS})

target_include_directories(LANShare PRIVATE src)

if(UNIX AND NOT APPLE)
    target_link_libraries(LANShare PRIVATE pthread)
endif()

# Install
install(TARGETS LANShare DESTINATION bin)

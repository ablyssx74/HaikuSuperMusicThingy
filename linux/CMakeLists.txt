cmake_minimum_required(VERSION 3.16)
project(HaikuSuperMusicThingy LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network Svg)

find_package(PkgConfig REQUIRED)
pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv)

set(APP_TARGET haikusupermusicthingy)

add_executable(${APP_TARGET}
    src/main.cpp
    src/MainWindow.h
    src/MainWindow.cpp
    src/MpvPlayer.h
    src/MpvPlayer.cpp
    src/StationManager.h
    src/StationManager.cpp
    src/ConfigManager.h
    src/ConfigManager.cpp
    src/FavoritesManager.h
    src/FavoritesManager.cpp
    src/EqualizerWidget.h
    src/EqualizerWidget.cpp
    src/Channel.h
    resources/icons.qrc
)

target_link_libraries(${APP_TARGET} PRIVATE
    Qt6::Widgets
    Qt6::Network
    Qt6::Svg
    PkgConfig::MPV
)

include(GNUInstallDirs)

install(TARGETS ${APP_TARGET} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES resources/haikusupermusicthingy.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES resources/icon.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
        RENAME haikusupermusicthingy.svg)

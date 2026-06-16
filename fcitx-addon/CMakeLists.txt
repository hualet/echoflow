# SPDX-FileCopyrightText: 2026 HarryLoong
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.16)
project(echoflow-fcitx-addon LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(Fcitx5Core REQUIRED)
find_package(Fcitx5Utils REQUIRED)

add_library(echoflow MODULE echoflow.cpp)
target_link_libraries(echoflow PRIVATE Fcitx5::Core Fcitx5::Utils)
set_target_properties(echoflow PROPERTIES PREFIX "lib")

configure_file(echoflow.conf.in echoflow.conf @ONLY)

include(GNUInstallDirs)
install(TARGETS echoflow LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/fcitx5")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/echoflow.conf" DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx5/addon")

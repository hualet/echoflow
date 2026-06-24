# SPDX-FileCopyrightText: 2026 Hualet Wang
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.16)
project(EchoFlow LANGUAGES C CXX)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

option(ECHOFLOW_BUILD_TESTS "Build QTest unit tests + spec tests" ON)

add_subdirectory(service)
add_subdirectory(fcitx-addon)
add_subdirectory(ui-host)
if(ECHOFLOW_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/systemd/user")
configure_file(
    "${CMAKE_SOURCE_DIR}/systemd/user/echoflow.service.in"
    "${CMAKE_BINARY_DIR}/systemd/user/echoflow.service"
    @ONLY)
configure_file(
    "${CMAKE_SOURCE_DIR}/systemd/user/echoflow-ui.service.in"
    "${CMAKE_BINARY_DIR}/systemd/user/echoflow-ui.service"
    @ONLY)
install(FILES
    "${CMAKE_BINARY_DIR}/systemd/user/echoflow.service"
    "${CMAKE_BINARY_DIR}/systemd/user/echoflow-ui.service"
    DESTINATION "lib/systemd/user")

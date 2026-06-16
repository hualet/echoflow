# SPDX-FileCopyrightText: 2026 Hualet Wang
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.16)
project(echoflow-ui-host LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick Widgets)
include(GNUInstallDirs)

add_executable(echoflow-ui
    main.cpp
)

target_link_libraries(echoflow-ui
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::Widgets
)

target_compile_definitions(echoflow-ui PRIVATE
    ECHOFLOW_QML_DIR="${CMAKE_INSTALL_FULL_DATADIR}/echoflow/qml"
)

install(TARGETS echoflow-ui RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../qml/EchoFlowTooltip.qml"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/echoflow/qml")

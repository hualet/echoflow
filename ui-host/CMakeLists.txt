# SPDX-FileCopyrightText: 2026 Hualet Wang
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.16)
project(echoflow-ui-host LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick Widgets)
find_package(Dtk6 COMPONENTS Widget Core Gui REQUIRED)
include(GNUInstallDirs)

add_executable(echoflow-ui
    main.cpp
    EchoFlowSettings.cpp
    SettingsDialog.cpp
    settings.qrc
)

target_link_libraries(echoflow-ui
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::Widgets
        Dtk6::Widget
        Dtk6::Core
        Dtk6::Gui
)

target_compile_definitions(echoflow-ui PRIVATE
    ECHOFLOW_QML_DIR="${CMAKE_INSTALL_FULL_DATADIR}/echoflow/qml"
    ECHOFLOW_QML_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../qml"
)

install(TARGETS echoflow-ui RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../qml/EchoFlowTooltip.qml"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/echoflow/qml")

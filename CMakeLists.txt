cmake_minimum_required(VERSION 3.14)
project(ReceiverUiPlugin LANGUAGES CXX)

if(DEFINED ENV{LOGOS_MODULE_BUILDER_ROOT})
    include($ENV{LOGOS_MODULE_BUILDER_ROOT}/cmake/LogosModule.cmake)
else()
    message(FATAL_ERROR "LogosModule.cmake not found. Set LOGOS_MODULE_BUILDER_ROOT.")
endif()

logos_module(
    NAME receiver_ui
    REP_FILE src/receiver_ui.rep
    SOURCES
        src/receiver_ui_backend.h
        src/receiver_ui_backend.cpp
    INCLUDE_DIRS
        src
)

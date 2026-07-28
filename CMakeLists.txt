cmake_minimum_required(VERSION 3.14)
project(ReceiverUiPlugin LANGUAGES CXX)

if(DEFINED ENV{LOGOS_MODULE_BUILDER_ROOT})
    include($ENV{LOGOS_MODULE_BUILDER_ROOT}/cmake/LogosModule.cmake)
else()
    message(FATAL_ERROR "LogosModule.cmake not found. Set LOGOS_MODULE_BUILDER_ROOT.")
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(secp256k1 REQUIRED IMPORTED_TARGET libsecp256k1)   # #13 verify station-identity sigs
pkg_check_modules(sodium    REQUIRED IMPORTED_TARGET libsodium)      # #69 private-stream KDF + AEAD

logos_module(
    NAME receiver_ui
    REP_FILE src/receiver_ui.rep
    SOURCES
        src/receiver_ui_backend.h
        src/receiver_ui_backend.cpp
        src/station_identity.h
        src/station_identity.cpp
        src/station_crypto.h
        src/station_crypto.cpp
    INCLUDE_DIRS
        src
    LINK_LIBRARIES
        PkgConfig::secp256k1
        PkgConfig::sodium
)

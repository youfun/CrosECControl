cmake_minimum_required(VERSION 3.21)

# Displayed in the UI, PE VERSIONINFO, and CI artifacts.
# Override from the command line: cmake -DCROSEC_VERSION=1.2.3+abc1234
if(NOT CROSEC_VERSION)
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" CROSEC_VERSION LIMIT_COUNT 1)
    string(STRIP "${CROSEC_VERSION}" CROSEC_VERSION)
endif()
string(REGEX REPLACE "^[vV]" "" CROSEC_VERSION "${CROSEC_VERSION}")
if(CROSEC_VERSION MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)")
    set(CROSEC_VERSION_MAJOR "${CMAKE_MATCH_1}")
    set(CROSEC_VERSION_MINOR "${CMAKE_MATCH_2}")
    set(CROSEC_VERSION_PATCH "${CMAKE_MATCH_3}")
else()
    message(FATAL_ERROR "CROSEC_VERSION must start with major.minor.patch, got: ${CROSEC_VERSION}")
endif()

project(CrosECControl
    VERSION ${CROSEC_VERSION_MAJOR}.${CROSEC_VERSION_MINOR}.${CROSEC_VERSION_PATCH}
    LANGUAGES CXX
)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/version.h"
    @ONLY
)

add_executable(CrosECControl WIN32
    src/main.cpp
    src/resources.rc
    src/resource.h
    src/app.manifest
)
target_include_directories(CrosECControl PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_compile_features(CrosECControl PRIVATE cxx_std_20)
target_compile_definitions(CrosECControl PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(CrosECControl PRIVATE comctl32 dwmapi uxtheme shlwapi shell32 ole32 uuid)
if(MSVC)
    target_compile_options(CrosECControl PRIVATE /W4 /utf-8 /permissive-)
endif()
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT CrosECControl)

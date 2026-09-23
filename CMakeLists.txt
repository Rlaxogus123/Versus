cmake_minimum_required(VERSION 3.21 FATAL_ERROR)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

project(Versus VERSION 0.4.14 LANGUAGES C CXX)

# A successful local Windows build is installed into the active Geode profile.
if(WIN32)
    set(GEODE_DONT_INSTALL_MODS OFF CACHE BOOL
        "Install Versus after a local Windows build" FORCE
    )
endif()

# This avoids precompiled-header code generation crashes with the installed
# Visual Studio toolchain.
if(MSVC)
    set(GEODE_DISABLE_PRECOMPILED_HEADERS ON CACHE BOOL "" FORCE)
endif()

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)
add_library(${PROJECT_NAME} SHARED ${SOURCES})

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /Z7 /FS)
endif()

set(GEODE_SDK_PATH "$ENV{GEODE_SDK}")
if(NOT GEODE_SDK_PATH)
    message(FATAL_ERROR
        "Unable to find the Geode SDK. Set GEODE_SDK to the SDK directory."
    )
endif()
file(TO_CMAKE_PATH "${GEODE_SDK_PATH}" GEODE_SDK_PATH)
if(NOT EXISTS "${GEODE_SDK_PATH}/CMakeLists.txt")
    message(FATAL_ERROR
        "GEODE_SDK does not point to a valid Geode SDK: ${GEODE_SDK_PATH}"
    )
endif()
message(STATUS "Found Geode: ${GEODE_SDK_PATH}")

add_subdirectory("${GEODE_SDK_PATH}" "${CMAKE_CURRENT_BINARY_DIR}/geode")
setup_geode_mod(${PROJECT_NAME})

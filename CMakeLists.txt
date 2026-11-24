cmake_minimum_required(VERSION 3.15)

# 32-bit only: every address in src/game.h is a 32-bit virtual address from
# gta_sa.exe 1.0 US. Configure with:
#     cmake -A Win32 -B build
project(minimap_asi CXX)

if(NOT WIN32)
    message(FATAL_ERROR "This plugin only builds for Windows.")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR
        "Configure for x86 (32-bit). With the Visual Studio generator use "
        "\"cmake -A Win32 -B build\"; with Ninja use an x86 developer prompt.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(minimap SHARED
    src/main.cpp
    src/radar.cpp
    src/config.cpp
    src/hook.cpp
    src/thunk.cpp
    src/log.cpp
)

target_include_directories(minimap PRIVATE src)

# Static CRT so the .asi does not depend on a specific VC++ redistributable
# being installed on the player's machine.
set_property(TARGET minimap PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

target_link_libraries(minimap PRIVATE user32)

target_compile_options(minimap PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive- /EHsc /Gy /GS->
)

# No manifest, no import lib clutter.
set_target_properties(minimap PROPERTIES
    OUTPUT_NAME "minimap"
    PREFIX ""
    SUFFIX ".asi"
    LINK_FLAGS "/SUBSYSTEM:WINDOWS /DYNAMICBASE:NO /NXCOMPAT:NO"
)

if(MSVC)
    # Match the game's own settings as closely as is useful for a DLL.
    target_compile_definitions(minimap PRIVATE _CRT_SECURE_NO_WARNINGS WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

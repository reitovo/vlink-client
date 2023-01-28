project(graphics-hook)

find_path(DETOURS_INCLUDE_DIRS "detours/detours.h")
find_library(DETOURS_LIBRARY detours REQUIRED)

add_library(graphics-hook SHARED)

target_include_directories(graphics-hook PRIVATE "${DETOURS_INCLUDE_DIRS}/detours")
target_link_libraries(graphics-hook PRIVATE ${DETOURS_LIBRARY})

target_sources(
    graphics-hook
    PRIVATE
    graphics-hook.c
    graphics-hook.h
    d3d8-capture.cpp
    d3d9-capture.cpp
    d3d9-patches.hpp
    dxgi-capture.cpp
    d3d10-capture.cpp
    d3d11-capture.cpp
    d3d12-capture.cpp
    ../util/windows/obfuscate.c
    ../util/windows/obfuscate.h
    ../graphics-hook-ver.h
    ../graphics-hook-info.h
    ../hook-helpers.h)

target_include_directories(graphics-hook PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)

target_link_libraries(graphics-hook PRIVATE ipc-util dxguid)
target_link_options(graphics-hook PRIVATE "LINKER:/IGNORE:4099")

target_compile_definitions(graphics-hook PRIVATE COMPILE_D3D12_HOOK)

install (TARGETS graphics-hook
    RUNTIME DESTINATION bin)
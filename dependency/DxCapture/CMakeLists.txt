project(DxCapture)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_library(DxCapture STATIC dx-capture-d3d11.cpp)
include_directories(.)

target_sources(
    DxCapture
    PRIVATE
    app-helpers.c
    app-helpers.h
    dx-capture.c
    dx-capture.h
    game-capture.c
    graphics-hook-info.h
    graphics-hook-ver.h
    hook-helpers.h
    inject-library.c
    inject-library.h
    nt-stuff.c
    nt-stuff.h

    util/bmem.h
    util/bmem.c
    util/dstr.h
    util/dstr.c
    util/base.h
    util/base.c
    util/utf8.h
    util/utf8.c
    util/platform.h
    util/platform.c
    util/platform-windows.c
    util/windows/window-helpers.h
    util/windows/window-helpers.c
    util/windows/obfuscate.c
    util/windows/obfuscate.h
)

if (MSVC)
    find_package(PThreads4W REQUIRED)
    target_link_libraries(DxCapture PRIVATE PThreads4W::PThreads4W)
endif ()

target_compile_definitions(
    DxCapture PRIVATE UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS
    _CRT_NONSTDC_NO_WARNINGS)

add_subdirectory(ipc-util)
target_link_libraries(DxCapture PRIVATE ipc-util)

add_subdirectory(get-graphics-offsets)
target_link_libraries(DxCapture PRIVATE get-graphics-offsets)

add_subdirectory(graphics-hook)
add_subdirectory(inject-helper)

target_include_directories(DxCapture PUBLIC
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(DxCapture PRIVATE Dwmapi.lib Winmm.lib)
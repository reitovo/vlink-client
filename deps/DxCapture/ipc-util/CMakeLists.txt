project(ipc-util)

add_library(ipc-util STATIC)

target_sources(ipc-util PRIVATE pipe.h)

set_target_properties(ipc-util PROPERTIES FOLDER "deps"
    POSITION_INDEPENDENT_CODE ON)

target_include_directories(ipc-util PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if (MSVC)
    target_compile_options(ipc-util PRIVATE /Zl)
endif ()

target_sources(ipc-util PRIVATE
    pipe-windows.c
    pipe-windows.h)

project(inject-helper)

add_executable(inject-helper)

target_sources(
    inject-helper
    PRIVATE inject-helper.c ../inject-library.c ../inject-library.h
    ../util/windows/obfuscate.c
    ../util/windows/obfuscate.h)

target_include_directories(inject-helper PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)

install(TARGETS inject-helper
    RUNTIME DESTINATION bin)

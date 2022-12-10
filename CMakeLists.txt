cmake_minimum_required(VERSION 3.5)

project(VTSLink VERSION 0.9.0 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets LinguistTools)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets LinguistTools)
qt_standard_project_setup()

set(TS_FILES
    translations/VTSLink_zh_CN.ts
    translations/VTSLink_en_US.ts)

set(PROJECT_SOURCES
        main.cpp
        mainwindow.cpp
        mainwindow.h
        mainwindow.ui
        resources.qrc
        vtslink.h util.h
        peer.h peer.cpp ndi2av.h ndi2av.cpp
        collabroom.h collabroom.cpp collabroom.ui
        blockinghttprequest.h blockinghttprequest.cpp blockinghttprequest.ui
        peeritemwidget.h peeritemwidget.cpp peeritemwidget.ui
        ${TS_FILES}
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(VTSLink
        vtslink.rc
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )
# Define target properties for Android with Qt 6 as:
#    set_property(TARGET VTSLink APPEND PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
#                 ${CMAKE_CURRENT_SOURCE_DIR}/android)
# For more information, see https://doc.qt.io/qt-6/qt-add-executable.html#target-creation

    qt_add_translations(VTSLink TS_FILES ${TS_FILES})
else()
    if(ANDROID)
        add_library(VTSLink SHARED
            ${PROJECT_SOURCES}
        )
# Define properties for Android with Qt 5 after find_package() calls as:
#    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
    else()
        add_executable(VTSLink
            ${PROJECT_SOURCES}
        )
    endif()

    qt5_create_translation(QM_FILES ${CMAKE_SOURCE_DIR} ${TS_FILES})
endif()

set_target_properties(VTSLink
    PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

target_compile_definitions(VTSLink PUBLIC VTSLINK_VERSION="${CMAKE_PROJECT_VERSION}")

target_link_libraries(VTSLink PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)

find_package(Qt6 REQUIRED COMPONENTS Network)
target_link_libraries(VTSLink PRIVATE Qt6::Network)

add_subdirectory(NatTypeProbe)
target_link_libraries(VTSLink PRIVATE NatTypeProb)

#libdatachannel
find_package(LibDataChannel CONFIG REQUIRED)
target_link_libraries(VTSLink PRIVATE LibDataChannel::LibDataChannel)

#winsparkle
#find_path(WINSPARKLE_INCLUDE_DIR winsparkle/winsparkle.h)
#find_library(WINSPARKLE_LIBRARY winsparkle)
#target_include_directories(VTSLink PRIVATE ${WINSPARKLE_INCLUDE_DIR})
#target_link_libraries(VTSLink PRIVATE ${WINSPARKLE_LIBRARY})

# kcp
find_package(kcp CONFIG REQUIRED)
target_link_libraries(VTSLink PRIVATE kcp::kcp)

# ffmpeg
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBAV REQUIRED IMPORTED_TARGET
    libavdevice
    libavfilter
    libavformat
    libavcodec
    libswresample
    libswscale
    libavutil
)
target_link_libraries(VTSLink PUBLIC PkgConfig::LIBAV)

# NDI SDK
string(REPLACE "\\" "/" NDI_SDK_DIR $ENV{NDI_SDK_DIR})
target_include_directories(VTSLink PRIVATE "${NDI_SDK_DIR}/Include")
target_link_libraries(VTSLink PRIVATE "${NDI_SDK_DIR}/Lib/x64/Processing.NDI.Lib.x64.lib")
add_custom_command(TARGET VTSLink POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${NDI_SDK_DIR}/Bin/x64/Processing.NDI.Lib.x64.dll"
        $<TARGET_FILE_DIR:VTSLink>)

# yuv
find_package(libyuv CONFIG REQUIRED)
target_link_libraries(VTSLink PUBLIC yuv)

# brotli
find_package(unofficial-brotli CONFIG REQUIRED)
# Brotli decoder library
target_link_libraries(VTSLink PRIVATE unofficial::brotli::brotlidec)
# Brotli encoder library
target_link_libraries(VTSLink PRIVATE unofficial::brotli::brotlienc)

set_target_properties(VTSLink PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER my.example.com
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

if (${CMAKE_BUILD_TYPE} MATCHES "Debug")
    install(FILES "C:/Windows/System32/ucrtbased.dll" DESTINATION bin)
endif()

install(DIRECTORY "${CMAKE_BINARY_DIR}/bin/" DESTINATION ${CMAKE_INSTALL_BINDIR}
    FILES_MATCHING PATTERN "*.dll")

install(TARGETS VTSLink
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

qt_generate_deploy_app_script(
    TARGET VTSLink
    FILENAME_VARIABLE DEPLOY_SCRIPT
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${DEPLOY_SCRIPT})

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(VTSLink)
endif()

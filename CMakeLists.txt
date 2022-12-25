cmake_minimum_required(VERSION 3.12)

set(ENV{VCPKG_MAX_CONCURRENCY} 16)

message(STATUS "VTSLink - Reito")

project(VTSLink VERSION 0.9.0)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 20) 
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets LinguistTools)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets LinguistTools)
qt_standard_project_setup()

find_package(Protobuf CONFIG REQUIRED)
include_directories(${Protobuf_INCLUDE_DIRS})
include_directories(${CMAKE_CURRENT_BINARY_DIR})
set(PROTO_FILES
    proto/avframe.proto
    proto/vts.proto
    proto/smartbuf.proto
)
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_FILES})

set(TS_FILES
    translations/VTSLink_zh_CN.ts
    translations/VTSLink_en_US.ts)

set(CODEC_FILES
        codec/ndi_to_av.h
        codec/ndi_to_av.cpp
        codec/bgra_to_nv12.h
        codec/bgra_to_nv12.cpp
        codec/av_to_d3d.h
        codec/av_to_d3d.cpp
        codec/d3d_to_ndi.h
        codec/d3d_to_ndi.cpp
        codec/nv12_to_bgra.h
        codec/nv12_to_bgra.cpp
        codec/ndi_to_d3d.h
        codec/ndi_to_d3d.cpp
        codec/i_d3d_src.h
        codec/i_d3d_src.cpp)

set(SHADER_FILES
        shader/blend_pixel.hlsl
        shader/blend_vertex.hlsl
        shader/nv12bgra_pixel.hlsl
        shader/nv12bgra_vertex.hlsl
        shader/bgranv12_pixel.hlsl
        shader/bgranv12_vertex.hlsl
        shader/bgranv12_downsample_vertex.hlsl
        shader/bgranv12_downsample_pixel.hlsl
    )

include_directories(.)
include_directories(codec)

set(PROJECT_SOURCES
        main.cpp
        resources.qrc
        core/vtslink.h
        core/util.h core/util.cpp
        core/peer.h core/peer.cpp
        core/smartbuf.h core/smartbuf.cpp
        core/debugcenter.h core/debugcenter.cpp
        ui/windows/mainwindow.cpp
        ui/windows/mainwindow.h
        ui/windows/mainwindow.ui
        ui/windows/collabroom.h ui/windows/collabroom.cpp ui/windows/collabroom.ui
        ui/windows/blockinghttprequest.h ui/windows/blockinghttprequest.cpp ui/windows/blockinghttprequest.ui
        ui/widgets/peeritemwidget.h ui/widgets/peeritemwidget.cpp ui/widgets/peeritemwidget.ui
        ui/windows/settingwindow.h ui/windows/settingwindow.cpp ui/windows/settingwindow.ui
        ${SHADER_FILES}
        ${CODEC_FILES}
        ${TS_FILES}
        ${PROTO_FILES}
        ${PROTO_SRCS} ${PROTO_HDRS}
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

#natprobe
add_subdirectory(dependency/NatTypeProbe)
target_link_libraries(VTSLink PRIVATE NatTypeProb)

#libdatachannel
find_package(LibDataChannel CONFIG REQUIRED)
target_link_libraries(VTSLink PRIVATE LibDataChannel::LibDataChannel)

#winsparkle
#find_path(WINSPARKLE_INCLUDE_DIR winsparkle/winsparkle.h)
#find_library(WINSPARKLE_LIBRARY winsparkle)
#target_include_directories(VTSLink PRIVATE ${WINSPARKLE_INCLUDE_DIR})
#target_link_libraries(VTSLink PRIVATE ${WINSPARKLE_LIBRARY})

#crashpad
find_package(crashpad CONFIG REQUIRED)
target_link_libraries(VTSLink PRIVATE crashpad crashpad::crashpad)
 
add_custom_command(TARGET VTSLink POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/zlib1.dll"
        $<TARGET_FILE_DIR:VTSLink>)
add_custom_command(TARGET VTSLink POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/crashpad_handler.exe"
        $<TARGET_FILE_DIR:VTSLink>)
install(FILES "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/crashpad_handler.exe" DESTINATION bin)
install(FILES "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/zlib1.dll" DESTINATION bin)

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

# libyuv
find_package(libyuv CONFIG REQUIRED)
target_link_libraries(VTSLink PRIVATE yuv)

# NDI SDK
string(REPLACE "\\" "/" NDI_SDK_DIR $ENV{NDI_SDK_DIR})
target_include_directories(VTSLink PRIVATE "${NDI_SDK_DIR}/Include")
target_link_libraries(VTSLink PRIVATE "${NDI_SDK_DIR}/Lib/x64/Processing.NDI.Lib.x64.lib")
add_custom_command(TARGET VTSLink POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${NDI_SDK_DIR}/Bin/x64/Processing.NDI.Lib.x64.dll"
        $<TARGET_FILE_DIR:VTSLink>)

# brotli
find_package(unofficial-brotli CONFIG REQUIRED)
# Brotli decoder library
target_link_libraries(VTSLink PRIVATE unofficial::brotli::brotlidec)
# Brotli encoder library
target_link_libraries(VTSLink PRIVATE unofficial::brotli::brotlienc)

# protobuf
target_link_libraries(VTSLink PRIVATE protobuf::libprotoc protobuf::libprotobuf)

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

#If use IDE other than Qt Creator, we need to copy required plugins and qt.conf correctly
file(WRITE "${CMAKE_BINARY_DIR}/bin/qt.conf" "[Paths]
Prefix = ..") 
if (${CMAKE_BUILD_TYPE} MATCHES "Debug")
    file(COPY "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/Qt6/plugins/" DESTINATION "${CMAKE_BINARY_DIR}/plugins/")
else()
    file(COPY "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/Qt6/plugins/" DESTINATION "${CMAKE_BINARY_DIR}/plugins/")
endif() 
cmake_minimum_required(VERSION 3.19)

set(ENV{VCPKG_MAX_CONCURRENCY} 16)  

message(STATUS "VTSLink - Reito")

project(VTSLink)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 20) 
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Linguist tools for Qt
find_package(QT NAMES Qt6 REQUIRED)

if(${QT_VERSION_MAJOR} LESS 6)
    message(FATAL_ERROR "Needs Qt6")
endif()

find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets LinguistTools Network)
qt_standard_project_setup()

# Translation files
set(TS_FILES
    translations/VTSLink_zh_CN.ts
    translations/VTSLink_en_US.ts
    translations/VTSLink_ja_JP.ts
)

# Codes
set(CODEC_FILES
    codec/spout_capture.h
    codec/spout_capture.cpp
    codec/d3d_capture.h
    codec/d3d_capture.cpp
    codec/frame_to_av.h
    codec/frame_to_av.cpp
    codec/bgra_to_nv12.h
    codec/bgra_to_nv12.cpp
    codec/av_to_d3d.h
    codec/av_to_d3d.cpp
    codec/d3d_to_frame.h
    codec/d3d_to_frame.cpp
    codec/nv12_to_bgra.h
    codec/nv12_to_bgra.cpp
    codec/i_d3d_src.h
    codec/i_d3d_src.cpp
)

# Shader
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

set(DIAGNOSTIC_SOURCES
    diag/diag.cpp
    diag/amd/amf_cap.cpp
    diag/amd/amf_enc.cpp
    diag/amd/AMFFactory.cpp
    diag/amd/CmdLogger.cpp
    diag/amd/DeviceDX11.cpp
    diag/amd/Thread.cpp
    diag/amd/ThreadWindows.cpp
)

# UI
set(PROJECT_SOURCES
    main.cpp
    resources.qrc
    core/vtslink.h
    core/util.h core/util.cpp
    core/peer.h core/peer.cpp
    core/smart_buf.h core/smart_buf.cpp
    core/debug_center.h core/debug_center.cpp
    core/speed.h core/speed.cpp
    core/usage.h core/usage.cpp
    core/room_server.h core/room_server.cpp
    core/grpc_cert.cpp core/grpc_cert.h
    ui/windows/mainwindow.cpp
    ui/windows/mainwindow.h
    ui/windows/mainwindow.ui
    ui/windows/collabroom.h ui/windows/collabroom.cpp ui/windows/collabroom.ui
    ui/widgets/peeritemwidget.h ui/widgets/peeritemwidget.cpp ui/widgets/peeritemwidget.ui
    ui/windows/settingwindow.h ui/windows/settingwindow.cpp ui/windows/settingwindow.ui
    ui/windows/buyrelay.cpp ui/windows/buyrelay.h ui/windows/buyrelay.ui
    ui/windows/dxgioutput.cpp ui/windows/dxgioutput.h ui/windows/dxgioutput.ui
    ui/windows/framequality.cpp ui/windows/framequality.h ui/windows/framequality.ui
    ${SHADER_FILES}
    ${CODEC_FILES}
    ${TS_FILES}
    ${PROTO_FILES}
    ${PROTO_SRCS}
    ${PROTO_HDRS}
)

qt_add_executable(VTSLink
    ui/vtslink.rc
    MANUAL_FINALIZATION
    ${PROJECT_SOURCES}
    ${DIAGNOSTIC_SOURCES}
)

# Tokens pass from env
target_compile_definitions(VTSLink PUBLIC -DVTSLINK_BACKTRACE_SUBMIT_TOKEN="$ENV{BACKTRACE_SUBMIT_TOKEN}")

qt_add_translations(VTSLink TS_FILES ${TS_FILES})

include_directories(.)
include_directories(codec)

set(USE_ASAN OFF)
set(USE_CRASHPAD ON)
set(USE_DIRECTXTK_OPENCV OFF)

set_target_properties(VTSLink
    PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

target_link_libraries(VTSLink PRIVATE Qt6::Widgets Qt6::Network)

# Protobuf/gRPC
find_package(Protobuf CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)

set(PROTO_FILES
    proto/vts.proto
    proto/vts_server.proto
    proto/relay.proto
    proto/common.proto
    )
add_library(VTSLinkProto ${PROTO_FILES})
target_link_libraries(VTSLinkProto PUBLIC protobuf::libprotobuf gRPC::grpc gRPC::grpc++)
target_include_directories(VTSLinkProto PUBLIC ${CMAKE_CURRENT_BINARY_DIR})

get_target_property(grpc_cpp_plugin_location gRPC::grpc_cpp_plugin LOCATION)
protobuf_generate(TARGET VTSLinkProto
    LANGUAGE cpp
    PROTOC_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/proto
    IMPORT_DIRS proto)
protobuf_generate(TARGET VTSLinkProto
    LANGUAGE grpc
    GENERATE_EXTENSIONS .grpc.pb.h .grpc.pb.cc
    IMPORT_DIRS proto
    PROTOC_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/proto
    PLUGIN "protoc-gen-grpc=${grpc_cpp_plugin_location}")

target_link_libraries(VTSLink PUBLIC VTSLinkProto)

find_path(AMD_AMF_INCLUDE_DIRS "AMF/components/Ambisonic2SRenderer.h")
target_include_directories(VTSLink PRIVATE ${AMD_AMF_INCLUDE_DIRS})

# DxCapture
add_subdirectory(deps/DxCapture)
target_link_libraries(VTSLink PRIVATE DxCapture)

# NatProbe
add_subdirectory(deps/NatTypeProbe)
target_link_libraries(VTSLink PRIVATE NatTypeProb)

# libdatachannel
find_package(LibDataChannel CONFIG REQUIRED)
target_link_libraries(VTSLink PRIVATE LibDataChannel::LibDataChannel)

# Asan, not useful really...
if (USE_ASAN)
    message(STATUS "Use Asan")
    target_compile_options(VTSLink PUBLIC -fsanitize=address)
endif()

# CrashPad
if (USE_CRASHPAD AND NOT USE_ASAN)
    message(STATUS "Use Crashpad")
    target_compile_definitions(VTSLink PUBLIC HAS_CRASHPAD)
    find_package(crashpad CONFIG REQUIRED)
    target_link_libraries(VTSLink PRIVATE crashpad crashpad::crashpad)
endif()

# Copy CrashPad Handler
if (${CMAKE_BUILD_TYPE} MATCHES "Debug")
    add_custom_command(TARGET VTSLink POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/zlib1.dll"
            $<TARGET_FILE_DIR:VTSLink>)
    install(FILES "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/zlib1.dll" DESTINATION bin)
endif()

add_custom_command(TARGET VTSLink POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/crashpad_handler.exe"
        $<TARGET_FILE_DIR:VTSLink>)
install(FILES "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/crashpad_handler.exe" DESTINATION bin)

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

# for debug purpose
# directxtk
if (USE_DIRECTXTK_OPENCV)
    message(STATUS "Use DirectXTK")
    target_compile_definitions(VTSLink PUBLIC HAS_DIRECTXTK)
    find_package(directxtk CONFIG REQUIRED)
    target_link_libraries(VTSLink PRIVATE Microsoft::DirectXTK)
endif()

# for debug purpose
# opencv
if (USE_DIRECTXTK_OPENCV)
    message(STATUS "Use OpenCV")
    target_compile_definitions(VTSLink PUBLIC HAS_OPENCV)
    set(OpenCV_DIR "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/opencv2")
    find_package(OpenCV REQUIRED)
    target_link_libraries(VTSLink PRIVATE opencv_core opencv_imgproc opencv_contrib)
endif()

# qrencode
find_path(QRENCODE_INCLUDE_DIR NAMES qrencode.h)
find_library(QRENCODE_LIBRARY_RELEASE qrencode)
find_library(QRENCODE_LIBRARY_DEBUG qrencoded)
set(QRENCODE_LIBRARIES optimized ${QRENCODE_LIBRARY_RELEASE} debug ${QRENCODE_LIBRARY_DEBUG})

target_include_directories(VTSLink PRIVATE ${QRENCODE_INCLUDE_DIR})
target_link_libraries(VTSLink PRIVATE ${QRENCODE_LIBRARIES})

# spout
find_package(Spout2 CONFIG REQUIRED)
target_link_libraries(VTSLink PRIVATE Spout2::Spout Spout2::SpoutLibrary Spout2::SpoutDX)

# brotli
find_package(unofficial-brotli CONFIG REQUIRED)
# Brotli decoder library
target_link_libraries(VTSLink PRIVATE unofficial::brotli::brotlidec)
# Brotli encoder library
target_link_libraries(VTSLink PRIVATE unofficial::brotli::brotlienc)

set_target_properties(VTSLink PROPERTIES
    WIN32_EXECUTABLE TRUE
)

# copy ucrtbased if debug
if (${CMAKE_BUILD_TYPE} MATCHES "Debug")
    install(FILES "C:/Windows/System32/ucrtbased.dll" DESTINATION bin)
endif()

install(DIRECTORY "${CMAKE_BINARY_DIR}/bin/" DESTINATION ${CMAKE_INSTALL_BINDIR} FILES_MATCHING PATTERN "*.dll")

install(TARGETS VTSLink
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

qt_generate_deploy_app_script(
    TARGET VTSLink
    FILENAME_VARIABLE DEPLOY_SCRIPT
    NO_UNSUPPORTED_PLATFORM_ERROR
) 
install(SCRIPT ${DEPLOY_SCRIPT})

qt_finalize_executable(VTSLink)

#If use IDE other than Qt Creator,  we need to copy required plugins and qt.conf correctly
file(WRITE "${CMAKE_BINARY_DIR}/bin/qt.conf" "[Paths]
Prefix = ..") 
if (${CMAKE_BUILD_TYPE} MATCHES "Debug")
    file(COPY "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/Qt6/plugins/" DESTINATION "${CMAKE_BINARY_DIR}/plugins/")
else()
    file(COPY "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/Qt6/plugins/" DESTINATION "${CMAKE_BINARY_DIR}/plugins/")
endif()

# install licenses
file(GLOB LICENSE_DIRS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/*")
foreach(LICENSE_DIR ${LICENSE_DIRS})
    if (EXISTS ${LICENSE_DIR}/copyright)
        get_filename_component(LICENSE_LIB_NAME ${LICENSE_DIR} NAME)
        install(FILES ${LICENSE_DIR}/copyright DESTINATION "license" RENAME ${LICENSE_LIB_NAME})
    endif()
endforeach()

# install pdb
install(DIRECTORY "${CMAKE_BINARY_DIR}/bin/" DESTINATION "pdb" FILES_MATCHING PATTERN "*.pdb")
if (${CMAKE_BUILD_TYPE} MATCHES "Debug")
    install(DIRECTORY "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/bin/" DESTINATION "pdb" FILES_MATCHING PATTERN "*.pdb")
else()
    install(DIRECTORY "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/" DESTINATION "pdb" FILES_MATCHING PATTERN "*.pdb")
endif()

# Git
find_package(Git QUIET)
execute_process(
    COMMAND ${GIT_EXECUTABLE} rev-parse HEAD
    OUTPUT_VARIABLE GIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
execute_process(
    COMMAND ${GIT_EXECUTABLE} symbolic-ref --short -q HEAD
    OUTPUT_VARIABLE GIT_BRANCH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
message(STATUS "Git version is ${GIT_BRANCH}:${GIT_HASH}")
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/git.h.in
    ${CMAKE_BINARY_DIR}/git.h
    @ONLY
)
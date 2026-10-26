# input: 依赖 CMake、Qt、平台工具链与打包选项。
# output: 提供 Qt 6 构建图；Linux 使用 D-Bus 桥接，构建时不改写翻译源文件。
# pos: 根目录中的主构建入口。
# update: 一旦我被更新，务必更新我的开头注释，以及所属的文件夹的 README.md。
cmake_minimum_required(VERSION 3.16)
set(_VERSION_MAJOR 0)
set(_VERSION_MINOR 8)
set(_VERSION_PATCH 2)
set(VERSION "${_VERSION_MAJOR}.${_VERSION_MINOR}.${_VERSION_PATCH}")
project(MPaste)

set(MPASTE_QT_ROOT "" CACHE PATH "Optional Qt installation prefix (prepended to CMAKE_PREFIX_PATH)")
if(MPASTE_QT_ROOT)
    list(PREPEND CMAKE_PREFIX_PATH "${MPASTE_QT_ROOT}")
elseif(NOT Qt6_DIR AND EXISTS "C:/Qt/6.8.0/mingw_64/lib/cmake/Qt6")
    list(PREPEND CMAKE_PREFIX_PATH "C:/Qt/6.8.0/mingw_64")
endif()

option(MPASTE_ENABLE_WINDOWS_DEPLOY "Enable post-build Windows deployment packaging" ON)
set(MPASTE_MINGW_BIN_DIR "" CACHE PATH "Optional MinGW runtime bin directory used by Windows deployment")

#set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON) 
# Appended, so flags given on the command line or in CXXFLAGS still apply.
string(APPEND CMAKE_CXX_FLAGS " -g -Wall")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DQT_DEBUG")

set(BIN_NAME MPaste)

file(GLOB SRCS CONFIGURE_DEPENDS
        "*.h" "*.cpp"
        "widget/*.ui" "widget/*.h" "widget/*.cpp"
        "widget/cardrenderer/*.h" "widget/cardrenderer/*.cpp"
        "resources.qrc"
        "data/*.h" "data/*.cpp"
        "utils/*.h" "utils/*.cpp"
)

# Build dependencies come from the system. A missing one stops the configure
# step with the package to install (Debian/Ubuntu names) rather than a missing
# header later in the build.
find_package(Qt6 COMPONENTS
        Widgets
        Multimedia
        Network
        LinguistTools
        Xml
)
if(NOT Qt6_FOUND)
    message(FATAL_ERROR "Qt 6 development files are missing. On Debian/Ubuntu: "
            "sudo apt install qt6-base-dev qt6-multimedia-dev qt6-tools-dev qt6-tools-dev-tools")
endif()

set(TS_FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/translations/MPaste_zh_CN.ts
)
set_source_files_properties(${TS_FILES} PROPERTIES
        OUTPUT_LOCATION ${CMAKE_BINARY_DIR}/translations)
qt6_add_translation(QM_FILES ${TS_FILES})

if(UNIX)
    find_package(PkgConfig REQUIRED)
    find_package(X11 REQUIRED)
    pkg_check_modules(XCB xcb xcb-keysyms)
    if(NOT XCB_FOUND)
        message(FATAL_ERROR "xcb-keysyms is missing. On Debian/Ubuntu: sudo apt install libxcb-keysyms1-dev")
    endif()
    find_path(XDO_INCLUDE_DIR xdo.h)
    find_library(XDO_LIBRARY xdo)
    if(NOT XDO_INCLUDE_DIR OR NOT XDO_LIBRARY)
        message(FATAL_ERROR "libxdo is missing. On Debian/Ubuntu: sudo apt install libxdo-dev")
    endif()
    find_package(Qt6 REQUIRED COMPONENTS DBus)

    set(PLATFORM_SPECIFIC_LIBS
            ${XDO_LIBRARY}
            ${X11_LIBRARIES}
            ${XCB_LIBRARIES}
            Qt6::DBus
    )

    set(PLATFORM_SPECIFIC_INCLUDES
            ${X11_INCLUDE_DIR}
            ${XCB_INCLUDE_DIRS}
            ${XDO_INCLUDE_DIR}
    )

    set(PLATFORM_SPECIFIC_SOURCES
            utils/PlatformRelated.cpp
            utils/PlatformRelated.h
    )
else()
    set(PLATFORM_SPECIFIC_LIBS "")
    set(PLATFORM_SPECIFIC_INCLUDES "")
    set(PLATFORM_SPECIFIC_SOURCES "")
endif()

if(WIN32)
    set(WIN_RC_FILE "${CMAKE_CURRENT_SOURCE_DIR}/resources/mpaste.rc")
endif()

if(WIN32 AND NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_executable(${BIN_NAME}
            WIN32
            ${SRCS}
            ${INTERFACES}
            ${PLATFORM_SPECIFIC_SOURCES}
            ${QM_FILES}
            ${WIN_RC_FILE}
    )
else()
    add_executable(${BIN_NAME}
            ${SRCS}
            ${INTERFACES}
            ${PLATFORM_SPECIFIC_SOURCES}
            ${QM_FILES}
            $<$<PLATFORM_ID:Windows>:${WIN_RC_FILE}>
    )
endif()

target_include_directories(${BIN_NAME} PUBLIC
        ${Qt6Gui_PRIVATE_INCLUDE_DIRS}
        ${PROJECT_BINARY_DIR}
        ${PLATFORM_SPECIFIC_INCLUDES}
)

# Native background blur for Wayland compositors advertising ext-background-effect-v1.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(Qt6 QUIET COMPONENTS GuiPrivate)
    pkg_check_modules(WAYLAND_CLIENT QUIET IMPORTED_TARGET wayland-client)
    pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)
    find_program(WAYLAND_SCANNER wayland-scanner)
    set(BLUR_PROTOCOL "${WAYLAND_PROTOCOLS_DIR}/staging/ext-background-effect/ext-background-effect-v1.xml")
    if(Qt6_VERSION VERSION_GREATER_EQUAL "6.11" AND WAYLAND_CLIENT_FOUND AND WAYLAND_SCANNER AND TARGET Qt6::GuiPrivate AND EXISTS "${BLUR_PROTOCOL}")
        add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/background-effect-client.h"
            COMMAND "${WAYLAND_SCANNER}" client-header "${BLUR_PROTOCOL}" "${CMAKE_CURRENT_BINARY_DIR}/background-effect-client.h"
            DEPENDS "${BLUR_PROTOCOL}")
        add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/background-effect-protocol.c"
            COMMAND "${WAYLAND_SCANNER}" private-code "${BLUR_PROTOCOL}" "${CMAKE_CURRENT_BINARY_DIR}/background-effect-protocol.c"
            DEPENDS "${BLUR_PROTOCOL}")
        target_sources(${BIN_NAME} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/background-effect-client.h"
            "${CMAKE_CURRENT_BINARY_DIR}/background-effect-protocol.c")
        target_link_libraries(${BIN_NAME} PRIVATE PkgConfig::WAYLAND_CLIENT Qt6::GuiPrivate)
        target_compile_definitions(${BIN_NAME} PRIVATE MPASTE_WAYLAND_BLUR)
    endif()
endif()

if(WIN32)
    target_link_libraries(${BIN_NAME} PRIVATE
            Qt6::Widgets
            Qt6::Multimedia
            Qt6::Network
            Qt6::Xml
            ${PLATFORM_SPECIFIC_LIBS}
            ${CUSTOM_LIBRARIES}
            dwmapi
            shell32
            ole32
            psapi
            version
    )
else()
    target_link_libraries(${BIN_NAME} PRIVATE
            Qt6::Widgets
            Qt6::Multimedia
            Qt6::Network
            Qt6::Xml
            ${PLATFORM_SPECIFIC_LIBS}
            ${CUSTOM_LIBRARIES}
    )
endif()

target_compile_definitions(${BIN_NAME} PRIVATE MPASTE_VERSION="${VERSION}")

if(UNIX)
    include(deb.cmake)
    include(CPack)
endif()

if(WIN32)
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(CMAKE_WIN32_EXECUTABLE TRUE)
    endif()

    if(NOT MPASTE_MINGW_BIN_DIR)
        get_filename_component(MPASTE_MINGW_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    endif()

    get_target_property(_qmake_executable Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin_dir "${_qmake_executable}" DIRECTORY)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${_qt_bin_dir}")

    set(MPASTE_MINGW_RUNTIME_DLLS "")
    foreach(runtime_dll libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll)
        set(runtime_path "${MPASTE_MINGW_BIN_DIR}/${runtime_dll}")
        if(EXISTS "${runtime_path}")
            list(APPEND MPASTE_MINGW_RUNTIME_DLLS "${runtime_path}")
        endif()
    endforeach()

    if(MPASTE_ENABLE_WINDOWS_DEPLOY)
        if(WINDEPLOYQT_EXECUTABLE)
            set(DEPLOY_DIR "${CMAKE_BINARY_DIR}")
            set(PACKAGE_DIR "${CMAKE_BINARY_DIR}/package/${PROJECT_NAME}")

            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E make_directory "${DEPLOY_DIR}"
                    COMMAND ${CMAKE_COMMAND} -E make_directory "${PACKAGE_DIR}"
                    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:${PROJECT_NAME}>" "${DEPLOY_DIR}"
                    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:${PROJECT_NAME}>" "${PACKAGE_DIR}"
                    COMMAND ${CMAKE_COMMAND} -E make_directory "${DEPLOY_DIR}/translations"
                    COMMAND ${CMAKE_COMMAND} -E make_directory "${PACKAGE_DIR}/translations"
                    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_BINARY_DIR}/translations" "${DEPLOY_DIR}/translations"
                    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_BINARY_DIR}/translations" "${PACKAGE_DIR}/translations"
                    COMMENT "Preparing Windows deployment directories..."
            )

            if(MPASTE_MINGW_RUNTIME_DLLS)
                add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${MPASTE_MINGW_RUNTIME_DLLS} "${DEPLOY_DIR}/"
                        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${MPASTE_MINGW_RUNTIME_DLLS} "${PACKAGE_DIR}/"
                        COMMAND_EXPAND_LISTS
                        COMMENT "Copying MinGW runtime DLLs..."
                )
            endif()

            # Find strip for removing debug symbols.
            find_program(STRIP_EXECUTABLE strip HINTS "${MPASTE_MINGW_BIN_DIR}" "${_qt_bin_dir}/../../../Tools/mingw*/bin")

            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                    COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                    --no-translations
                    --no-system-d3d-compiler
                    --no-compiler-runtime
                    --no-opengl-sw
                    --dir "${DEPLOY_DIR}"
                    "${DEPLOY_DIR}/${PROJECT_NAME}.exe"
                    COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                    --no-translations
                    --no-system-d3d-compiler
                    --no-compiler-runtime
                    --no-opengl-sw
                    --dir "${PACKAGE_DIR}"
                    "${PACKAGE_DIR}/${PROJECT_NAME}.exe"
                    COMMENT "Running windeployqt..."
            )

            # Strip debug symbols from the exe in the package directory
            # and remove FFmpeg DLLs pulled in by Qt6Multimedia (only
            # audio playback is used, which does not need FFmpeg).
            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E echo "Stripping and cleaning package..."
                    COMMAND ${CMAKE_COMMAND} -E rm -f
                        "${PACKAGE_DIR}/avcodec-61.dll"
                        "${PACKAGE_DIR}/avformat-61.dll"
                        "${PACKAGE_DIR}/avutil-59.dll"
                        "${PACKAGE_DIR}/swscale-8.dll"
                        "${PACKAGE_DIR}/swresample-5.dll"
                        "${DEPLOY_DIR}/avcodec-61.dll"
                        "${DEPLOY_DIR}/avformat-61.dll"
                        "${DEPLOY_DIR}/avutil-59.dll"
                        "${DEPLOY_DIR}/swscale-8.dll"
                        "${DEPLOY_DIR}/swresample-5.dll"
                    COMMAND ${CMAKE_COMMAND} -E rm -rf
                        "${PACKAGE_DIR}/multimedia/ffmpegmediaplugin.dll"
                        "${PACKAGE_DIR}/generic"
                        "${PACKAGE_DIR}/networkinformation"
                        "${DEPLOY_DIR}/multimedia/ffmpegmediaplugin.dll"
                        "${DEPLOY_DIR}/generic"
                        "${DEPLOY_DIR}/networkinformation"
                    COMMENT "Removing unused DLLs and plugins..."
            )
            if(STRIP_EXECUTABLE)
                add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                        COMMAND "${STRIP_EXECUTABLE}" --strip-all "${PACKAGE_DIR}/${PROJECT_NAME}.exe"
                        COMMAND "${STRIP_EXECUTABLE}" --strip-unneeded
                            "${PACKAGE_DIR}/libstdc++-6.dll"
                            "${PACKAGE_DIR}/libgcc_s_seh-1.dll"
                            "${PACKAGE_DIR}/libwinpthread-1.dll"
                        COMMENT "Stripping debug symbols..."
                )
            endif()

            add_custom_target(package
                    COMMAND ${CMAKE_COMMAND} -E tar "cfv" "${CMAKE_BINARY_DIR}/${PROJECT_NAME}-${VERSION}-win64.zip" --format=zip "${PROJECT_NAME}"
                    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/package
                    COMMENT "Creating zip package..."
                    DEPENDS ${PROJECT_NAME}
            )
        else()
            message(WARNING "windeployqt was not found; Windows deploy packaging is disabled. Set MPASTE_QT_ROOT or disable MPASTE_ENABLE_WINDOWS_DEPLOY.")
        endif()
    endif()
endif()

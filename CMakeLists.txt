cmake_minimum_required(VERSION 3.7)
set(VERSION 0.4.0)
project(MPaste)

# Set Qt installation path
set(CMAKE_PREFIX_PATH "C:/Qt/6.8.0/mingw_64")

#set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_CXX_STANDARD 17)  # Updated to C++17 for Qt6
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_CXX_FLAGS "-g -Wall")

# generate a compile commands file
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DQT_DEBUG")

set(BIN_NAME MPaste)

# Sources files
file(GLOB SRCS
        "*.h" "*.cpp"
        "widget/*.ui" "widget/*.h" "widget/*.cpp"
        "resources.qrc"
        "data/*.h" "data/*.cpp"
        "utils/*.h" "utils/*.cpp"
)

# Find Qt packages (these are platform-independent)
find_package(Qt6 COMPONENTS
        Widgets
        Multimedia
        Network
        LinguistTools
        Xml
        REQUIRED
)

# Translation files setup
set(TS_FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/translations/MPaste_zh_CN.ts
)
set_source_files_properties(${TS_FILES} PROPERTIES
        OUTPUT_LOCATION ${CMAKE_BINARY_DIR}/translations)
qt6_create_translation(QM_FILES ${CMAKE_SOURCE_DIR} utils ${TS_FILES})
qt6_add_translation(QM_FILES ${TS_FILES})

# Platform specific configuration
IF (UNIX)
    # Linux specific packages
    find_package(PkgConfig REQUIRED)
    find_package(Qt6X11Extras REQUIRED)
    find_package(KF5WindowSystem REQUIRED)
    pkg_check_modules(QGSettings REQUIRED gsettings-qt)
    set(PLATFORM_SPECIFIC_LIBS
            KF5::WindowSystem
            xdo
            ${Qt6X11Extras_LIBRARIES}
            ${QGSettings_LIBRARIES}
    )
    set(PLATFORM_SPECIFIC_INCLUDES
            ${QGSettings_INCLUDE_DIRS}
    )
    set(PLATFORM_SPECIFIC_SOURCES
            utils/PlatformRelated.cpp
            utils/PlatformRelated.h
    )
ELSE()
    # Windows specific setup
    set(PLATFORM_SPECIFIC_LIBS "")
    set(PLATFORM_SPECIFIC_INCLUDES "")
    set(PLATFORM_SPECIFIC_SOURCES "")
ENDIF()

# Add KDSingleApplication subdirectory if it exists
#if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/KDSingleApplication")
#    add_subdirectory(KDSingleApplication)
#    set(CUSTOM_LIBRARIES kdsingleapplication)
#endif()

# Main executable
# Main executable
IF(WIN32)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_executable(${BIN_NAME}
                ${SRCS}
                ${INTERFACES}
                ${PLATFORM_SPECIFIC_SOURCES}
                ${QM_FILES}
        )
    else()
        add_executable(${BIN_NAME}
                WIN32    # Only add WIN32 flag for Release builds
                ${SRCS}
                ${INTERFACES}
                ${PLATFORM_SPECIFIC_SOURCES}
                ${QM_FILES}
        )
    endif()
ENDIF()

target_include_directories(${BIN_NAME} PUBLIC
        ${Qt6Gui_PRIVATE_INCLUDE_DIRS}
        ${PROJECT_BINARY_DIR}
        ${PLATFORM_SPECIFIC_INCLUDES}
)

IF(WIN32)
    target_link_libraries(${BIN_NAME} PRIVATE
            Qt6::Widgets
            Qt6::Multimedia
            Qt6::Network
            Qt6::Xml
            ${PLATFORM_SPECIFIC_LIBS}
            ${CUSTOM_LIBRARIES}
            dwmapi
    )
ELSE ()
    target_link_libraries(${BIN_NAME} PRIVATE
            Qt6::Widgets
            Qt6::Multimedia
            Qt6::Network
            Qt6::Xml
            ${PLATFORM_SPECIFIC_LIBS}
            ${CUSTOM_LIBRARIES}
    )
ENDIF ()

# Only include packaging on Unix systems
IF(UNIX)
    include(deb.cmake)
    include(CPack)
ENDIF()

if(WIN32)
    # 设置为 Windows GUI 应用程序
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(CMAKE_WIN32_EXECUTABLE TRUE)
    endif()

    # 找到 windeployqt.exe
    get_target_property(_qmake_executable Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin_dir "${_qmake_executable}" DIRECTORY)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${_qt_bin_dir}")

    # 使用 CLion 的 MinGW 目录
    set(MINGW_DIR "C:/Users/tianj/AppData/Local/Programs/CLion/bin/mingw/bin")


    # 两个部署目录
    set(DEPLOY_DIR "${CMAKE_BINARY_DIR}")
    set(PACKAGE_DIR "${CMAKE_BINARY_DIR}/package/${PROJECT_NAME}")

    # 同时部署到两个目录
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            # 创建目录
            COMMAND ${CMAKE_COMMAND} -E make_directory "${DEPLOY_DIR}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${PACKAGE_DIR}"

            # 复制主程序到两个目录
            COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:${PROJECT_NAME}>" "${DEPLOY_DIR}"
            COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:${PROJECT_NAME}>" "${PACKAGE_DIR}"

            # 创建并复制翻译文件到两个目录
            COMMAND ${CMAKE_COMMAND} -E make_directory "${DEPLOY_DIR}/translations"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${PACKAGE_DIR}/translations"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_BINARY_DIR}/translations" "${DEPLOY_DIR}/translations"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_BINARY_DIR}/translations" "${PACKAGE_DIR}/translations"

            # 复制 MinGW 运行时到两个目录
            COMMAND ${CMAKE_COMMAND} -E copy
            "${MINGW_DIR}/libgcc_s_seh-1.dll"
            "${MINGW_DIR}/libstdc++-6.dll"
            "${MINGW_DIR}/libwinpthread-1.dll"
            "${DEPLOY_DIR}/"

            COMMAND ${CMAKE_COMMAND} -E copy
            "${MINGW_DIR}/libgcc_s_seh-1.dll"
            "${MINGW_DIR}/libstdc++-6.dll"
            "${MINGW_DIR}/libwinpthread-1.dll"
            "${PACKAGE_DIR}/"

            # 运行 windeployqt 部署 Qt 依赖到两个目录
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

            COMMENT "Deploying to both directories..."
    )

    # 打包命令（直接打包 package 目录）
    add_custom_target(package
            COMMAND ${CMAKE_COMMAND} -E tar "cfv" "${CMAKE_BINARY_DIR}/${PROJECT_NAME}-${VERSION}-win64.zip" --format=zip "${PROJECT_NAME}"
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/package
            COMMENT "Creating zip package..."
            DEPENDS ${PROJECT_NAME}
    )
endif()
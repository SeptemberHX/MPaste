cmake_minimum_required(VERSION 3.7)

set(VERSION 4.0)

project(MPaste)

#set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_PREFIX_PATH  "/Volumes/macData/Qt/6.2.4/macos/lib/cmake")
set(CMAKE_CXX_FLAGS "-g -Wall")

# generate a compile commands file as complete database for vim-YouCompleteMe or some other similar tools
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)


set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DQT_DEBUG")

set(BIN_NAME MPaste)

# Sources files
file(GLOB SRCS "*.h" "*.cpp" "widget/*.ui" "widget/*.h" "widget/*.cpp"
        "resources.qrc" "data/*.h" "data/*.cpp" "utils/*.h" "utils/*.cpp" )

# Find the library
find_package(PkgConfig REQUIRED)
find_package(Qt6 COMPONENTS Widgets Multimedia Network LinguistTools Xml REQUIRED)

set(TS_FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/translations/MPaste_zh_CN.ts
        )
set_source_files_properties(${TS_FILES} PROPERTIES OUTPUT_LOCATION ${CMAKE_BINARY_DIR}/translations)
qt6_create_translation(QM_FILES ${CMAKE_SOURCE_DIR} utils ${TS_FILES})
qt6_add_translation(QM_FILES ${TS_FILES})

IF (LINUX)
find_package(Qt5X11Extras REQUIRED)
find_package(KF5WindowSystem REQUIRED)
ENDIF()

add_subdirectory(KDSingleApplication)

add_executable(${BIN_NAME} ${SRCS} ${INTERFACES} utils/PlatformRelated.cpp utils/PlatformRelated.h ${QM_FILES})
target_include_directories(${BIN_NAME} PUBLIC
        ${PROJECT_BINARY_DIR}
        )

set(CUSTOM_LIBARAIES kdsingleapplication)

IF (LINUX)
    set(CUSTOM_LIBARAIES KF5::WindowSystem xdo ${CUSTOM_LIBARAIES} ${Qt5X11Extras_LIBRARIES})
ENDIF()

target_link_libraries(${BIN_NAME} PRIVATE
        Qt6::Widgets
        Qt6::Multimedia
        Qt6::Network
        Qt6::Xml
        ${CUSTOM_LIBARAIES}
        )

IF (LINUX)
include(deb.cmake)
include(CPack)
endif ()
cmake_minimum_required(VERSION 3.7)

set(VERSION 4.0)

project(MPaste)

#set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
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
find_package(Qt5Widgets REQUIRED)
find_package(Qt5Multimedia REQUIRED)
find_package(Qt5Network REQUIRED)

IF (UNIX)
find_package(Qt5X11Extras REQUIRED)
find_package(KF5WindowSystem REQUIRED)
ENDIF()

pkg_check_modules(QGSettings REQUIRED gsettings-qt)
add_subdirectory(KDSingleApplication)

add_executable(${BIN_NAME} ${SRCS} ${INTERFACES} utils/PlatformRelated.cpp utils/PlatformRelated.h)
target_include_directories(${BIN_NAME} PUBLIC
        ${Qt5Gui_PRIVATE_INCLUDE_DIRS}
        ${PROJECT_BINARY_DIR}
        ${QGSettings_INCLUDE_DIRS}
        )

set(CUSTOM_LIBARAIES kdsingleapplication)

IF (UNIX)
    set(CUSTOM_LIBARAIES KF5::WindowSystem xdo ${CUSTOM_LIBARAIES} ${Qt5X11Extras_LIBRARIES})
ENDIF()

target_link_libraries(${BIN_NAME} PRIVATE
        ${Qt5Widgets_LIBRARIES}
        ${Qt5Multimedia_LIBRARIES}
        ${Qt5Network_LIBRARIES}
        ${QGSettings_LIBRARIES}
        ${CUSTOM_LIBARAIES}
        )

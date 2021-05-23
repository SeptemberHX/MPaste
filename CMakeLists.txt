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
file(GLOB SRCS "*.h" "*.cpp" "widget/*.ui" "widget/*.h" "widget/*.cpp" "qhotkey/*.h" "qhotkey/*.cpp")

# Find the library
find_package(PkgConfig REQUIRED)
find_package(Qt5Widgets REQUIRED)
find_package(Qt5Concurrent REQUIRED)
find_package(Qt5X11Extras REQUIRED)
find_package(Qt5DBus REQUIRED)
find_package(Qt5Svg REQUIRED)
find_package(DtkWidget REQUIRED)
find_package(DtkCMake REQUIRED)
find_package(KF5WindowSystem REQUIRED)

pkg_check_modules(XCB_EWMH REQUIRED xcb-ewmh x11)
pkg_check_modules(DFrameworkDBus REQUIRED dframeworkdbus)
pkg_check_modules(QGSettings REQUIRED gsettings-qt)
pkg_check_modules(DtkGUI REQUIRED dtkgui)

add_executable(${BIN_NAME} ${SRCS} ${INTERFACES})
target_include_directories(${BIN_NAME} PUBLIC ${DtkWidget_INCLUDE_DIRS}
        ${XCB_EWMH_INCLUDE_DIRS}
        ${DFrameworkDBus_INCLUDE_DIRS}
        ${Qt5Gui_PRIVATE_INCLUDE_DIRS}
        ${PROJECT_BINARY_DIR}
        ${QGSettings_INCLUDE_DIRS}
        ${DtkGUI_INCLUDE_DIRS}
        ${KF5WindowSystem_INCLUDE_DIRS}
        )

target_link_libraries(${BIN_NAME} PRIVATE
        ${XCB_EWMH_LIBRARIES}
        ${DFrameworkDBus_LIBRARIES}
        ${DtkWidget_LIBRARIES}
        ${Qt5Widgets_LIBRARIES}
        ${Qt5Concurrent_LIBRARIES}
        ${Qt5X11Extras_LIBRARIES}
        ${Qt5DBus_LIBRARIES}
        ${QGSettings_LIBRARIES}
        ${DtkGUI_LIBRARIES}
        ${Qt5Svg_LIBRARIES}
        KF5::WindowSystem
        xdo
        X11
        )

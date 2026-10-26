# input: 依赖 CPack、Debian 打包变量与安装布局。
# output: 对外提供 Debian 打包补充配置（cpack -G DEB 生成 mpaste_<版本>_<架构>.deb）。
# pos: 根目录中的 Linux 打包辅助脚本。
# update: 一旦我被更新，务必更新我的开头注释，以及所属的文件夹的 README.md。
include(GNUInstallDirs)

set(CPACK_GENERATOR "DEB")

# ---- debian/control ----
set(CPACK_PACKAGE_NAME "mpaste")
set(CPACK_DEBIAN_PACKAGE_NAME "mpaste")
set(CPACK_PACKAGE_VERSION_MAJOR "${_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${_VERSION_PATCH}")
# mpaste_0.8.1_amd64.deb; the architecture comes from dpkg on the build machine.
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)

# The libraries the binary links (Qt 6, libxdo, xcb...) are read from it by
# dpkg-shlibdeps. Qt's plugins are loaded at run time and so are listed here:
# SVG for the icons, Wayland for a Wayland session.
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "qt6-svg-plugins, qt6-wayland")
# The Wayland blur uses Qt's private GUI API, which holds within one minor release.
get_target_property(_mpaste_defines ${BIN_NAME} COMPILE_DEFINITIONS)
if(_mpaste_defines MATCHES "MPASTE_WAYLAND_BLUR")
    math(EXPR _qt_next_minor "${Qt6_VERSION_MINOR} + 1")
    string(APPEND CPACK_DEBIAN_PACKAGE_DEPENDS ", libqt6gui6 (<< ${Qt6_VERSION_MAJOR}.${_qt_next_minor}~)")
endif()
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "gnome-shell (>= 51)")
set(CPACK_STRIP_FILES ON)

set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/SeptemberHX/MPaste")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Clipboard history with cards, search and quick paste")
# One string: separate quoted parts would make a CMake list, joined with ';'.
set(CPACK_PACKAGE_DESCRIPTION "MPaste keeps what you copy - text, rich text, images, links and files - as \
cards you can search, preview, pin and paste back. On GNOME Wayland it works with its companion Shell \
extension, which this package installs; each user enables it once with \
'gnome-extensions enable mpaste@mpaste.local' and logs in again.")
set(CPACK_PACKAGE_CONTACT "september_hx@outlook.com")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "SeptemberHX <september_hx@outlook.com>")

set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA "${CMAKE_SOURCE_DIR}/debian/scripts/postinst")

# ---- files ----
# The upstream changelog, compressed as Debian keeps it.
add_custom_command(
    OUTPUT "${CMAKE_BINARY_DIR}/changelog.gz"
    COMMAND gzip -cn9 "${CMAKE_SOURCE_DIR}/CHANGELOG.md" > "${CMAKE_BINARY_DIR}/changelog.gz"
    DEPENDS "${CMAKE_SOURCE_DIR}/CHANGELOG.md"
    COMMENT "Compressing changelog"
)
add_custom_target(changelog ALL DEPENDS "${CMAKE_BINARY_DIR}/changelog.gz")

# As a target, so the build's RUNPATH (a local deps root, say) is dropped on install.
install(TARGETS ${BIN_NAME} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ${CMAKE_SOURCE_DIR}/MPaste.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
# Started with the session, as a clipboard history has to be running to record;
# each user can turn it off in their startup applications.
install(FILES ${CMAKE_SOURCE_DIR}/debian/MPaste-autostart.desktop DESTINATION /etc/xdg/autostart RENAME MPaste.desktop)
install(FILES ${CMAKE_SOURCE_DIR}/resources/mpaste.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/48x48/apps)
install(FILES ${CMAKE_SOURCE_DIR}/resources/mpaste.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(DIRECTORY ${CMAKE_BINARY_DIR}/translations DESTINATION ${CMAKE_INSTALL_DATADIR}/MPaste)
# The GNOME Wayland companion, available to every user; each enables it once.
install(FILES
    ${CMAKE_SOURCE_DIR}/extensions/mpaste@mpaste.local/extension.js
    ${CMAKE_SOURCE_DIR}/extensions/mpaste@mpaste.local/metadata.json
    DESTINATION ${CMAKE_INSTALL_DATADIR}/gnome-shell/extensions/mpaste@mpaste.local)
install(FILES ${CMAKE_BINARY_DIR}/changelog.gz ${CMAKE_SOURCE_DIR}/debian/doc/copyright
    DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/mpaste)

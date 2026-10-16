# GNOME Wayland integration regression

Run `python3 tests/gnome-wayland/run.py` after building `build/MPaste`.
Requires GNOME Shell 51, Qt 6 development files, a C++ compiler, Python 3,
`gsettings`, `gdbus`, `gnome-extensions`, and `dbus-run-session`.

The test starts a headless Wayland compositor on a private D-Bus session and copies
both the production companion and a test-only keyboard driver into a temporary data
directory. Its configuration, clipboard history, application logs and Wayland socket
are isolated from the user's session. It terminates its processes on exit and prints
the temporary artifact directory for diagnostics.

Assertions cover native Wayland text (including Chinese), HTML, image and file-URL
capture; Alt+Q activation; bottom panel geometry; pasting an older rich-text item after
replacing the current clipboard; focus restoration; suppression of self-paste history
duplicates; and extension disable/enable recovery without restarting MPaste.

The helper produces clipboard updates after a fresh keyboard event, matching real
copy behavior. Reusing an old Wayland input serial can cause the compositor to reject
a selection update and would make a timer-only clipboard test invalid.

The driver samples visible panel geometry on every `before-paint` event during opening. The very first visible frame must already be bottom-aligned; a final-position-only check would miss center-to-bottom flashes. `--extension PATH` can test a previous companion version against the same regression.

The test client has a desktop entry with a distinctive source icon. After copying, a separate process reads the saved `.mpaste` icon with Qt and checks its pixel color, proving persistence rather than just an in-memory lookup. The default run exercises a file icon; `--themed-icon` exercises a named hicolor theme icon.

Space-preview regression opens and closes the preview twice, asserts 980×760 logical bounds centered in the actual work area, and verifies the bottom panel bounds are unchanged.

Optional native menu-blur validation: `menu-blur-scene.cpp` is an alternate application entry point that can be linked against the normal application objects/libraries (excluding `MPaste.cpp.o`). Set `MPASTE_MENU_BLUR_PROBE` to that executable when running `run.py`. The runner opens the menu over a checkerboard, saves `native-menu-blur.png` through the GNOME compositor, checks advertised blur support, and opens/closes the popup three times to exercise Wayland surface recreation. QWidget screenshots cannot validate compositor blur, so inspect the compositor capture instead.

Set `MPASTE_GLASS_POPUPS=1` with the menu probe to capture and inspect the hover toolbar, settings, Space preview, details, About and alias windows over the checkerboard. `MPASTE_TEST_LIGHT=1` repeats the scene with the light theme.

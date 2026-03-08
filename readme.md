# MPaste

A clipboard manager alternative to Paste for Linux & Windows.

> It has nothing related to `Paste` for mac. I just like its UX design, and decide to implement one in Qt

> For wayland, it cannot get the icons of current window, which is treated as the provider of current clipboard data. I haven't found how to fetch the window icon under wayland with window id. Check PlatformRelated.h for more details.

## Feature

* Clipboard history saved to files
* More elegant UI design
* History search
* auto paste

## Shortcut

* `Alt+[1-9, 0]`: quick select item. Holding `Alt` can show the shortcut tips
* Any characters: search mode
* Global shortcut to show window: Open your shortcut settings in system settings, and assign a shortcut for command `/path/to/your/MPaste`

> On Gnome, please install the extension: https://extensions.gnome.org/extension/1005/focus-my-window/  
> Or the window will not get focused after appearing with shortcut

## Default settings

> configure file path: `~/.config/MPaste/MPaste.conf`. Data path configure doesn't work yet.

* Max history size: 500
* History location: `~/.MPaste`
* Auto Paste when item selected

## Screenshot

Gif on [Imgur](https://i.imgur.com/79gyO0n.gifv)

### Windows 11

![Screenshot on Windows 11](./screenshot/mpaste_on_windows_11.png)

### Linux

![Screenshot on Ubuntu 21.04 with Gnome](https://i.imgur.com/q6OCzOT.png)

![Screenshot on Deepin V20 with DDE](https://i.imgur.com/iRUJK8I.png)

![Screenshot on neon with KDE](https://i.imgur.com/h5GXFkF.png)

## For Deepin V20

Download the deb package from the release page and install it.

**do not forget to set a shortcut for it in system settings**

## How to bulid from source

```shell
sudo apt install cmake g++ make libkf5windowsystem-dev qttools5-dev libqt5x11extras5-dev qtmultimedia5-dev libgsettings-qt-dev
git clone https://github.com/SeptemberHX/MPaste
cd MPaste
mkdir build
cd build
cmake ..
make -j8
```

### Build configuration notes

On Windows, the project no longer hardcodes local Qt / MinGW paths.

- Optional Qt prefix: `-DMPASTE_QT_ROOT=/path/to/Qt/6.x.x/<toolchain>`
- Optional MinGW runtime directory: `-DMPASTE_MINGW_BIN_DIR=/path/to/mingw/bin`
- Disable post-build Windows packaging: `-DMPASTE_ENABLE_WINDOWS_DEPLOY=OFF`

Example:

```shell
cmake -B build -DMPASTE_QT_ROOT=C:/Qt/6.8.0/mingw_64
cmake --build build
```

### Rendering backend

You can optionally control the OpenGL backend with the `MPASTE_OPENGL_BACKEND` environment variable:

- `auto`: do not force a backend
- `gles`: force OpenGL ES
- `software`: force software OpenGL
- `software-gles`: keep the previous compatibility behavior

Example:

```shell
set MPASTE_OPENGL_BACKEND=software
MPaste.exe
```

### History file compatibility

- New history files use a versioned on-disk format.
- Older `.mpaste` files are still loaded automatically.
- Invalid / corrupted history files are skipped instead of breaking the whole load process.

* [KDSingleApplication](https://github.com/KDAB/KDSingleApplication)
* sound effect from https://www.zapsplat.com/

<div>Icons made by <a href="https://www.flaticon.com/authors/pixel-perfect" title="Pixel perfect">Pixel perfect</a> from <a href="https://www.flaticon.com/" title="Flaticon">www.flaticon.com</a></div>

## Todo

* Use image instead of widget in scroll area to speed up
* Categories

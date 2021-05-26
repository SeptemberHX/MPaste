# MPaste

A clipboard manager alternative to Paste for Linux. Works on X11 and wayland.

> It has nothing related to `Paste` for mac. I just like its UX design, and decide to implement one in Qt

> Still working in progress

## Feature

* Clipboard history saved to files
* More elegant UI design
* History search

## Shortcut

* `Alt+[1-9, 0]`: quick select item. Holding `Alt` can show the shortcut tips
* Any characters: search mode
* Global shortcut to show window: Open your shortcut settings in system settings, and assign a shortcut for command `/path/to/your/MPaste`

> On Gnome wayland with 2x screen factor, `export QT_AUTO_SCREEN_SCALE_FACTOR=1` is needed before running.

> On Gnome, please install the extension: https://extensions.gnome.org/extension/1005/focus-my-window/  
> Or the window will not get focused after appearing with shortcut

## Default settings

* Max history size: 500
* History location: `~/.MPaste`

## Screenshot

Gif on [Imgur](https://i.imgur.com/79gyO0n.gifv)

![s2_ubuntu](https://i.imgur.com/DlshO2o.png)
![s1](./images/s1.png)

## How to bulid from source

```shell
sudo apt install cmake g++ make libkf5windowsystem-dev qttools5-dev libqt5x11extras5-dev qtmultimedia5-dev libgsettings-qt-dev
git clone https://github.com/SeptemberHX/MPaste
cd MPaste
mkdir build
cd build
cmake ..
make
```

* [KDSingleApplication](https://github.com/KDAB/KDSingleApplication)
* sound effect from https://www.zapsplat.com/

<div>Icons made by <a href="https://www.flaticon.com/authors/pixel-perfect" title="Pixel perfect">Pixel perfect</a> from <a href="https://www.flaticon.com/" title="Flaticon">www.flaticon.com</a></div>

## Todo

* Use image instead of widget in scroll area to speed up
* Categories

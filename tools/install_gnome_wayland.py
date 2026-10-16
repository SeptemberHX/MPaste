#!/usr/bin/env python3
"""Install the built MPaste and GNOME 51 bridge for the current user, without sudo."""
import argparse
import ast
import os
from pathlib import Path
import shutil
import subprocess


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, default=repo / 'build/MPaste')
    parser.add_argument('--autostart', action='store_true', help='Start MPaste at login')
    args = parser.parse_args()
    binary = args.binary.resolve()
    if not binary.is_file() or not os.access(binary, os.X_OK):
        parser.error(f'Build MPaste first: {binary} is not executable')
    version = subprocess.check_output(['gnome-shell', '--version'], text=True).strip()
    if not version.split()[-1].startswith('51.'):
        parser.error(f'This companion currently supports GNOME 51; found {version}')
    data = Path(os.environ.get('XDG_DATA_HOME', Path.home() / '.local/share'))
    config = Path(os.environ.get('XDG_CONFIG_HOME', Path.home() / '.config'))
    uuid = 'mpaste@mpaste.local'
    extension = data / 'gnome-shell/extensions' / uuid
    extension.mkdir(parents=True, exist_ok=True)
    for name in ('extension.js', 'metadata.json'):
        shutil.copy2(repo / 'extensions' / uuid / name, extension / name)
    install = data / 'mpaste'
    install.mkdir(parents=True, exist_ok=True)
    # Atomic replacement also permits updating an existing installation.
    shutil.copy2(binary, install / 'MPaste.new')
    (install / 'MPaste.new').replace(install / 'MPaste')
    if (binary.parent / 'translations').is_dir():
        shutil.copytree(binary.parent / 'translations', install / 'translations', dirs_exist_ok=True)
    shutil.copy2(repo / 'resources/mpaste.svg', install / 'mpaste.svg')
    launcher = install / 'launch'
    import shlex
    launcher.write_text('#!/bin/sh\n'
        'if [ "${XDG_SESSION_TYPE:-}" = wayland ]; then export QT_QPA_PLATFORM=wayland; fi\n'
        f'exec {shlex.quote(str(install / "MPaste"))} "$@"\n')
    launcher.chmod(0o755)
    def desktop_arg(path):
        return '"' + str(path).replace('\\', '\\\\').replace('"', '\\"').replace('`', '\\`').replace('$', '\\$').replace('%', '%%') + '"'
    desktop = ('[Desktop Entry]\nType=Application\nName=MPaste\n'
        'Comment=Clipboard history for GNOME Wayland\n'
        f'Exec={desktop_arg(launcher)} --show\nIcon={install / "mpaste.svg"}\n'
        'Terminal=false\nCategories=Utility;\nStartupWMClass=MPaste\n')
    (data / 'applications').mkdir(parents=True, exist_ok=True)
    (data / 'applications/MPaste.desktop').write_text(desktop)
    if args.autostart:
        (config / 'autostart').mkdir(parents=True, exist_ok=True)
        (config / 'autostart/MPaste.desktop').write_text(desktop.replace(' --show\n', '\n'))
    raw = subprocess.check_output(['gsettings', 'get', 'org.gnome.shell', 'enabled-extensions'], text=True)
    enabled = ast.literal_eval(raw.removeprefix('@as ').strip())
    if uuid not in enabled:
        enabled.append(uuid)
        subprocess.run(['gsettings', 'set', 'org.gnome.shell', 'enabled-extensions', repr(enabled)], check=True)
    # A newly installed extension is discovered at the next GNOME login.
    result = subprocess.run(['gnome-extensions', 'enable', uuid], capture_output=True, text=True)
    print(f'Installed MPaste: {install / "MPaste"}')
    print(f'Installed extension: {extension}')
    print('Log out and log back in to load the new extension, then press Alt+Q.')
    if result.returncode:
        print('The current Shell has not discovered the extension yet; activation is saved for next login.')


if __name__ == '__main__':
    main()

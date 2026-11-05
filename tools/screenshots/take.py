#!/usr/bin/env python3
"""Take the README screenshots in an isolated GNOME Wayland session.

Made-up content is copied by sample-client, which takes the desktop file id
of a real app so GNOME names it and gives it that app's icon. Nothing of the
user's own clipboard history is used: the session has its own config,
history and data folders under a temporary directory.

    python3 tools/screenshots/take.py [--binary build/MPaste] [--out screenshot]

Needs gnome-shell, the build's MPaste, Qt 6 development files (the client is
built here) and Pillow. The screen is 2880 x 1800 at scale 2; shots are saved
at 1x, as the README shows them.
"""
import argparse
import ast
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import time

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
LOGICAL = (1440, 900)

# Newest first, as the panel shows them: (desktop file id, what is copied).
SAMPLES = [
    ('org.gnome.Ptyxis', {'text': 'git rebase -i origin/master && git push --force-with-lease'}),
    ('firefox_firefox', {'text': 'https://github.com/SeptemberHX/MPaste/releases'}),
    ('firefox_firefox', {'text': 'Design review moved to Thursday 3 pm, room 4. Please read the attached spec first.',
                         'html': 'Design review moved to <b>Thursday 3 pm</b>, room 4. Please read the '
                                 '<a href="https://example.org/spec">attached spec</a> first.'}),
    ('org.gnome.Loupe', {'image': 'release-notes.png'}),
    ('org.gnome.Nautilus', {'urls': ['home/sample/Documents/Design spec v2.pdf']}),
    ('org.inkscape.Inkscape', {'text': '#5B8DEF'}),
    ('firefox_firefox', {'text': 'https://www.example.org/docs/getting-started/install-on-linux'}),
    ('org.gnome.Loupe', {'image': 'landscape.png'}),
]


def wait_for(check, description, timeout=20):
    until = time.monotonic() + timeout
    while time.monotonic() < until:
        value = check()
        if value:
            return value
        time.sleep(.1)
    raise AssertionError(f'Timed out: {description}')


def make_pictures(base):
    """The pictures that are copied: a landscape and a long page of notes."""
    from PIL import Image, ImageDraw, ImageFont
    w, h = 1600, 1000
    land = Image.new('RGB', (w, h))
    d = ImageDraw.Draw(land)
    for y in range(h):
        t = y / h
        if y < h * .62:
            c = (int(120 + 80 * t), int(170 + 50 * t), int(220 - 10 * t))
        else:
            c = (int(40 + 20 * t), int(90 + 30 * t), int(60 + 10 * t))
        d.line([(0, y), (w, y)], fill=c)
    d.ellipse((1080, 300, 1200, 420), fill=(255, 244, 200))
    land.save(base / 'landscape.png')

    font = '/usr/share/fonts/opentype/noto/NotoSans-Regular.ttf'
    bold = '/usr/share/fonts/opentype/noto/NotoSans-Bold.ttf'
    if not Path(font).exists():
        font = bold = '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'
    # About as tall as one and a half widths: whole in the preview, still readable.
    page = Image.new('RGB', (1400, 2100), (250, 250, 248))
    d = ImageDraw.Draw(page)
    title, head, body = ImageFont.truetype(bold, 76), ImageFont.truetype(bold, 46), ImageFont.truetype(font, 36)
    y = 90
    d.text((80, y), 'Release notes', font=title, fill=(30, 32, 38)); y += 120
    sections = [
        ('Faster search', ['Search now looks through text found in pictures.', 'Typing in the panel filters at once,',
                           'and results keep their order while you type.']),
        ('Previews', ['Space opens a picture at full size.', 'Scroll to zoom around the pointer,',
                      'drag to move, double-click to fit.']),
        ('Smaller and lighter', ['Saved pictures leave memory once they are on disk.',
                                 'The copy sound plays through the system.']),
        ('Text recognition', ['Pictures are read on this computer.', 'Nothing is uploaded.']),
    ]
    for heading, lines in sections:
        d.rounded_rectangle((80, y, 1320, y + 6), 3, fill=(91, 141, 239)); y += 40
        d.text((80, y), heading, font=head, fill=(30, 32, 38)); y += 80
        for line in lines:
            d.text((80, y), line, font=body, fill=(70, 74, 84)); y += 60
        y += 60
    page.crop((0, 0, page.width, y + 20)).save(base / 'release-notes.png')
    docs = base / 'home/sample/Documents'
    docs.mkdir(parents=True, exist_ok=True)
    (docs / 'Design spec v2.pdf').write_bytes(b'%PDF-1.4\n% made-up sample\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, default=REPO / 'build/MPaste')
    parser.add_argument('--out', type=Path, default=REPO / 'screenshot')
    parser.add_argument('--inside', action='store_true', help=argparse.SUPPRESS)
    args = parser.parse_args()
    if not args.inside:
        return subprocess.call(['dbus-run-session', '--', sys.executable, str(__file__), '--inside',
                                '--binary', str(args.binary.resolve()), '--out', str(args.out.resolve())])
    from PIL import Image

    base = Path(tempfile.mkdtemp(prefix='mpaste-shots-'))
    print(f'Session folder: {base}', flush=True)
    env = os.environ.copy()
    env.update(XDG_CONFIG_HOME=str(base / 'config'), XDG_DATA_HOME=str(base / 'data'),
               XDG_CACHE_HOME=str(base / 'cache'), GSETTINGS_BACKEND='keyfile', XDG_SESSION_TYPE='wayland',
               WAYLAND_DISPLAY=base.name, QT_QPA_PLATFORM='wayland', USER=base.name,
               MPASTE_LOG_DIR=str(base / 'logs'), LANG='en_US.UTF-8', LANGUAGE='en', LC_ALL='en_US.UTF-8')
    ext = base / 'data/gnome-shell/extensions'
    shutil.copytree(REPO / 'extensions/mpaste@mpaste.local', ext / 'mpaste@mpaste.local')
    shutil.copytree(REPO / 'tests/gnome-wayland/driver', ext / 'test-driver@mpaste.local')
    gset = lambda *a: subprocess.run(['gsettings', 'set', *a], env=env, check=True)
    gset('org.gnome.shell', 'enabled-extensions', "['mpaste@mpaste.local', 'test-driver@mpaste.local']")
    gset('org.gnome.desktop.interface', 'scaling-factor', '2')
    gset('org.gnome.desktop.interface', 'clock-format', '24h')
    make_pictures(base)

    qt_lib = subprocess.check_output(['qmake6', '-query', 'QT_INSTALL_LIBS'], text=True).strip()
    flags = shlex.split(subprocess.check_output(['pkg-config', '--cflags', '--libs', 'Qt6Widgets'],
                                               env=env | {'PKG_CONFIG_PATH': qt_lib + '/pkgconfig'}, text=True))
    subprocess.run(['c++', '-std=c++17', '-fPIC', str(HERE / 'sample-client.cpp'), '-o', str(base / 'client'), *flags],
                   check=True)

    procs = []

    def launch(cmd, log):
        f = (base / log).open('a')
        p = subprocess.Popen(cmd, env=env, stdout=f, stderr=subprocess.STDOUT)
        f.close()
        procs.append(p)
        return p

    def evaluate(code):
        r = subprocess.run(['gdbus', 'call', '--session', '--dest', 'org.gnome.Shell', '--object-path',
                            '/org/mpaste/Test', '--method', 'org.mpaste.Test.Eval', code],
                           env=env, text=True, capture_output=True)
        return None if r.returncode else json.loads(ast.literal_eval(r.stdout)[0])

    def key(*keys):
        values = ','.join('Clutter.KEY_' + k for k in keys)
        evaluate('(()=>{const keys=[' + values + '];'
                 'for(const k of keys)this.keyboard.notify_keyval(GLib.get_monotonic_time(),k,Clutter.KeyState.PRESSED);'
                 'for(const k of keys.reverse())this.keyboard.notify_keyval(GLib.get_monotonic_time(),k,Clutter.KeyState.RELEASED);'
                 'return true;})()')
        time.sleep(.15)

    def click(x, y, button=1):
        # Move first and let the widget see the pointer arrive, then press
        # and release apart: a press with no motion before it is not a click.
        evaluate(f'this.pointer.notify_absolute_motion(GLib.get_monotonic_time(),{x},{y});true')
        time.sleep(.25)
        evaluate(f'this.pointer.notify_button(GLib.get_monotonic_time(),{button},Clutter.ButtonState.PRESSED);true')
        time.sleep(.08)
        evaluate(f'this.pointer.notify_button(GLib.get_monotonic_time(),{button},Clutter.ButtonState.RELEASED);true')
        time.sleep(.4)

    def frame(title):
        return evaluate("(()=>{for(const a of global.get_window_actors()){const w=a.meta_window;"
                        f"if(w.get_title()==={json.dumps(title)}&&a.is_visible()){{const r=w.get_frame_rect();"
                        "return [r.x,r.y,r.width,r.height];}}return null;})()")

    def shot(name):
        path = base / f'{name}-raw.png'
        evaluate("Gio._promisify(Shell.Screenshot.prototype,'screenshot');this.captureDone=false;"
                 f"const s=Gio.File.new_for_path({json.dumps(str(path))}).replace(null,false,Gio.FileCreateFlags.NONE,null);"
                 "new Shell.Screenshot().screenshot(false,s).then(()=>{s.close(null);this.captureDone=true;});true")
        wait_for(lambda: evaluate('this.captureDone'), 'screenshot')
        image = Image.open(path).convert('RGB')
        return image, image.width / LOGICAL[0]

    def save(image, scale, box, name):
        """box in logical pixels; saved at 1x."""
        x, y, w, h = box
        part = image.crop((int(x * scale), int(y * scale), int((x + w) * scale), int((y + h) * scale)))
        part = part.resize((int(w), int(h)), Image.LANCZOS)
        args.out.mkdir(parents=True, exist_ok=True)
        part.save(args.out / f'{name}.png', optimize=True)
        print(f'saved {name}.png {part.size[0]}x{part.size[1]}', flush=True)

    def write_config(theme, card, onboarding=1):
        conf = base / 'config/MPaste/MPaste.conf'
        conf.parent.mkdir(parents=True, exist_ok=True)
        conf.write_text(f'[main]\nsaveDir={base / "history"}\nplaySound=false\nthemeMode={theme}\ncardStyle={card}\n'
                        f'onboardingVersion={onboarding}\n[privacy]\nfetchLinkPreviews=false\n')

    app = None

    def start_mpaste(theme, card, onboarding=1):
        nonlocal app
        if app:
            app.terminate()
            app.wait(5)
        write_config(theme, card, onboarding)
        log = base / 'app.log'
        start = log.stat().st_size if log.exists() else 0
        app = launch([str(args.binary)], 'app.log')
        wait_for(lambda: '[startup] event-loop checkpoint 500ms' in log.read_bytes()[start:].decode(errors='replace'),
                 'MPaste startup', 30)
        time.sleep(1.5)

    def show_panel():
        key('Alt_L', 'q')
        wait_for(lambda: frame('MPaste'), 'panel', 10)
        time.sleep(1.8)

    def hide_panel():
        key('Escape')
        time.sleep(1)

    try:
        launch(['gnome-shell', '--headless', '--no-x11', '--wayland-display=' + base.name,
                '--virtual-monitor=2880x1800'], 'shell.log')
        wait_for(lambda: evaluate('true'), 'GNOME test driver', 30)
        evaluate('Main.overview.hide(); true')
        start_mpaste(2, 0)

        # Copy the samples, oldest first, each from its app.
        for app_id, data in reversed(SAMPLES):
            client = launch([str(base / 'client'), str(base), app_id], 'client.log')
            wait_for(lambda: evaluate("global.display.focus_window?.get_title() === 'MPaste Sample'"), 'sample focus')
            key('F12')
            payload = dict(data)
            if 'image' in payload:
                payload['image'] = str(base / payload['image'])
            if 'urls' in payload:
                payload['urls'] = [str(base / u) for u in payload['urls']]
            log = base / 'app.log'
            start = log.stat().st_size
            (base / 'command.json').write_text(json.dumps(payload))
            wait_for(lambda: 'clipboardUpdated' in log.read_bytes()[start:].decode(errors='replace'), 'capture')
            time.sleep(.8)   # the thumbnail
            client.terminate()
            client.wait(5)
            time.sleep(.3)

        panel_box = (0, LOGICAL[1] - 450, LOGICAL[0], 450)
        for theme, card, name in ((2, 0, 'panel-still-dark'), (2, 1, 'panel-aurora-dark'), (1, 0, 'panel-still-light')):
            start_mpaste(theme, card)
            show_panel()
            image, scale = shot(name)
            save(image, scale, panel_box, name)
            hide_panel()

        # Preview: the long page of notes (fourth card), Space.
        start_mpaste(2, 1)
        show_panel()
        for _ in range(3):
            key('Right')
        key('space')
        time.sleep(2)
        image, scale = shot('preview')
        save(image, scale, (0, 0, *LOGICAL), 'preview')
        key('space')
        time.sleep(.8)

        # Details of the picture: right click on its card, then the menu's
        # second item (Paste as plain text, Details).
        panel = frame('MPaste')
        card_x = panel[0] + 16 + 128 + 3 * 278
        card_y = panel[1] + 52 + 108
        click(card_x, card_y, 3)
        time.sleep(.5)
        key('Down')
        key('Down')
        key('Return')
        wait_for(lambda: frame('MPaste Details'), 'details', 10)
        time.sleep(2)
        details = frame('MPaste Details')
        image, scale = shot('details')
        save(image, scale, details, 'details')
        key('Escape')
        time.sleep(.5)

        # Settings, at Text recognition: the panel's gear, then the sixth row
        # of its side bar.
        if not frame('MPaste'):
            show_panel()
        panel = frame('MPaste')
        if os.environ.get('MPASTE_SHOTS_DEBUG'):
            print('windows', evaluate("global.get_window_actors().map(a=>[a.meta_window.get_title(),a.is_visible(),"
                                      "(r=>[r.x,r.y,r.width,r.height])(a.meta_window.get_frame_rect())])"), flush=True)
            dbg, dscale = shot('debug-before-gear')
            save(dbg, dscale, (0, 0, *LOGICAL), 'debug-before-gear')
        click(panel[0] + 1146, panel[1] + 25)   # the gear opens a menu: Settings, Quit
        time.sleep(.6)
        key('Down')
        key('Return')
        wait_for(lambda: frame('Settings'), 'settings', 10)
        time.sleep(1.5)   # it sizes itself to its page once shown, and moves
        settings = frame('Settings')
        click(settings[0] + 110, settings[1] + 300)
        time.sleep(1.2)
        settings = frame('Settings')
        image, scale = shot('settings')
        save(image, scale, settings, 'settings')

        # The first-run guide, at the hotkey step.
        start_mpaste(2, 1, onboarding=0)
        wait_for(lambda: frame('MPaste') and evaluate(
            "global.get_window_actors().some(a=>a.meta_window.get_title()==='MPaste'&&a.meta_window.get_frame_rect().height>400)"),
            'guide', 10)
        time.sleep(1)
        guide = evaluate("(()=>{for(const a of global.get_window_actors()){const w=a.meta_window;const r=w.get_frame_rect();"
                         "if(w.get_title()==='MPaste'&&r.height>400&&r.height<800)return [r.x,r.y,r.width,r.height];}return null;})()")
        click(guide[0] + guide[2] - 62, guide[1] + guide[3] - 44)   # Start, bottom right
        time.sleep(1)
        image, scale = shot('onboarding')
        save(image, scale, guide, 'onboarding')
    finally:
        for p in reversed(procs):
            if p.poll() is None:
                p.terminate()
        for p in procs:
            try:
                p.wait(5)
            except subprocess.TimeoutExpired:
                p.kill()
        if os.environ.get('MPASTE_SHOTS_KEEP'):
            print(f'kept {base}')
        else:
            shutil.rmtree(base, ignore_errors=True)


if __name__ == '__main__':
    sys.exit(main())

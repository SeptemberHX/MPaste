#!/usr/bin/env python3
"""Real GNOME 51/Wayland roundtrip test in a private bus and headless compositor."""
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


def wait_for(check, description, timeout=15):
    until = time.monotonic() + timeout
    while time.monotonic() < until:
        value = check()
        if value:
            return value
        time.sleep(.1)
    raise AssertionError(f'Timed out: {description}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, default=REPO / 'build/MPaste')
    parser.add_argument('--extension', type=Path, default=REPO / 'extensions/mpaste@mpaste.local')
    parser.add_argument('--themed-icon', action='store_true')
    parser.add_argument('--inside', action='store_true', help=argparse.SUPPRESS)
    args = parser.parse_args()
    if not args.inside:
        return subprocess.call(['dbus-run-session', '--', sys.executable, str(__file__),
                                '--inside', '--binary', str(args.binary.resolve()),
                                '--extension', str(args.extension.resolve()),
                                *(['--themed-icon'] if args.themed_icon else [])])
    base = Path(tempfile.mkdtemp(prefix='mpaste-wayland-test-'))
    print(f'Test artifacts: {base}', flush=True)
    env = os.environ.copy()
    env.update(XDG_CONFIG_HOME=str(base / 'config'), XDG_DATA_HOME=str(base / 'data'),
               GSETTINGS_BACKEND='keyfile', XDG_SESSION_TYPE='wayland',
               WAYLAND_DISPLAY=base.name, QT_QPA_PLATFORM='wayland', USER=base.name,
               MPASTE_LOG_DIR=str(base / 'logs'))
    extension_dir = base / 'data/gnome-shell/extensions'
    shutil.copytree(args.extension.resolve(), extension_dir / 'mpaste@mpaste.local')
    shutil.copytree(HERE / 'driver', extension_dir / 'test-driver@mpaste.local')
    settings = base / 'config/MPaste/MPaste.conf'
    settings.parent.mkdir(parents=True)
    settings.write_text(f'[main]\nsaveDir={base / "history"}\nplaySound=false\nthemeMode={1 if os.environ.get("MPASTE_TEST_LIGHT") else 2}\n')
    subprocess.run(['gsettings', 'set', 'org.gnome.shell', 'enabled-extensions',
                    "['mpaste@mpaste.local', 'test-driver@mpaste.local']"], env=env, check=True)
    qt_lib = subprocess.check_output(['qmake6', '-query', 'QT_INSTALL_LIBS'], text=True).strip()
    pkg_env = env | {'PKG_CONFIG_PATH': qt_lib + '/pkgconfig'}
    flags = shlex.split(subprocess.check_output(['pkg-config', '--cflags', '--libs', 'Qt6Widgets'],
                                               env=pkg_env, text=True))
    subprocess.run(['c++', '-std=c++17', '-fPIC', str(HERE / 'client.cpp'),
                    '-o', str(base / 'client'), *flags], check=True)
    icon = base / 'source-icon.svg'
    icon.write_text('<svg xmlns="http://www.w3.org/2000/svg" width="64" height="64"><rect width="64" height="64" fill="#12abcd"/></svg>')
    icon_reference = str(icon)
    if args.themed_icon:
        theme = base / 'data/icons/hicolor'
        (theme / 'scalable/apps').mkdir(parents=True)
        (theme / 'index.theme').write_text('[Icon Theme]\nName=Hicolor\nDirectories=scalable/apps\n[scalable/apps]\nSize=64\nType=Scalable\nMinSize=16\nMaxSize=256\nContext=Applications\n')
        shutil.copy2(icon, theme / 'scalable/apps/org.mpaste.IconTest.svg')
        icon_reference = 'org.mpaste.IconTest'
    applications = base / 'data/applications'
    applications.mkdir(parents=True, exist_ok=True)
    (applications / 'org.mpaste.IconTest.desktop').write_text(
        f'[Desktop Entry]\nType=Application\nName=MPaste Icon Test\nExec={base / "client"}\nIcon={icon_reference}\n')
    processes = []
    def launch(command, log):
        output = (base / log).open('w')
        proc = subprocess.Popen(command, env=env, stdout=output, stderr=subprocess.STDOUT)
        output.close()
        processes.append(proc)
        return proc
    def evaluate(code):
        result = subprocess.run(['gdbus', 'call', '--session', '--dest', 'org.gnome.Shell',
            '--object-path', '/org/mpaste/Test', '--method', 'org.mpaste.Test.Eval', code],
            env=env, text=True, capture_output=True)
        if result.returncode:
            return None
        return json.loads(ast.literal_eval(result.stdout)[0])
    def key(*keys):
        values = ','.join('Clutter.KEY_' + k for k in keys)
        assert evaluate('(()=>{const keys=[' + values + '];'
            'for(const k of keys)this.keyboard.notify_keyval(GLib.get_monotonic_time(),k,Clutter.KeyState.PRESSED);'
            'for(const k of keys.reverse())this.keyboard.notify_keyval(GLib.get_monotonic_time(),k,Clutter.KeyState.RELEASED);'
            'return true;})()')
    def copy(data, expected):
        # A real copy follows keyboard input. Wayland rejects reused input serials.
        key('F12')
        time.sleep(.1)
        log_path = base / 'app.log'
        start = log_path.stat().st_size
        (base / 'command.json').write_text(json.dumps(data))
        wait_for(lambda: any('clipboardUpdated' in line and expected in line
            for line in log_path.read_bytes()[start:].decode(errors='replace').splitlines()),
            'capture ' + expected)
        print('PASS capture', expected, flush=True)
    try:
        launch(['gnome-shell', '--headless', '--no-x11', '--wayland-display=' + base.name,
                '--virtual-monitor=1440x900', '--virtual-monitor=1920x1080'], 'shell.log')
        wait_for(lambda: evaluate('true'), 'GNOME test driver')
        app = launch([str(args.binary.resolve())], 'app.log')
        wait_for(lambda: '[startup] event-loop checkpoint 500ms' in (base / 'app.log').read_text(), 'MPaste startup')
        launch([str(base / 'client'), str(base)], 'client.log')
        wait_for(lambda: evaluate("Main.overview.hide();global.display.focus_window?.get_title() === 'MPaste Test Target'"), 'target focus')
        evaluate("global.display.focus_window.move_to_monitor(0); true")
        evaluate("(()=>{const m=Main.layoutManager.monitors[0];this.pointer.notify_absolute_motion(GLib.get_monotonic_time(),m.x+20,m.y+20);return true;})()")
        wait_for(lambda: evaluate("global.display.focus_window?.get_monitor() === 0"), 'initial target monitor')
        copy({'text': 'Wayland 中文 history'}, 'type=1')
        saved_item = next((base / 'history/Clipboard').glob('*.mpaste'))
        subprocess.run([str(base / 'client'), '--verify-icon', str(saved_item)],
                       env=env | {'QT_QPA_PLATFORM': 'offscreen'}, check=True)
        print('PASS source application icon persisted with clipboard history', flush=True)
        copy({'text': 'Rich history', 'html': '<b>Rich history</b>'}, 'type=4')
        copy({'image': True}, 'type=3')
        copy({'urls': '/tmp/mpaste-wayland-example.txt'}, 'type=5')
        copy({'text': 'Current clipboard must not be pasted'}, 'type=1')
        count = len(list((base / 'history/Clipboard').glob('*.mpaste')))
        assert count == 5, count
        evaluate("""this.panelFrames = [];
            this.frameTrace = global.stage.connect('before-paint', () => {
                for (const actor of global.get_window_actors()) {
                    const window = actor.meta_window;
                    if (window.get_title() !== 'MPaste' || !actor.is_visible() || actor.opacity === 0) continue;
                    const r = window.get_frame_rect();
                    this.panelFrames.push([r.x, r.y, r.width, r.height]);
                }
            }); true""")
        key('Alt_L', 'q')
        wait_for(lambda: evaluate("global.display.focus_window?.get_title() === 'MPaste'"), 'Alt+Q activation')
        rect = evaluate('(()=>{const r=global.display.focus_window.get_frame_rect();return [r.x,r.y,r.width,r.height];})()')
        initial_area = evaluate('(()=>{const a=Main.layoutManager.getWorkAreaForMonitor(0);return [a.x,a.y,a.width,a.height];})()')
        ax,ay,aw,ah = initial_area
        assert rect[0] == ax and rect[2] == aw and rect[1] + rect[3] == ay + ah, (rect,initial_area)
        time.sleep(.3)
        frames = evaluate('global.stage.disconnect(this.frameTrace); this.panelFrames')
        assert frames, 'No visible panel frames sampled'
        assert all(r[0] == ax and r[2] == aw and r[1] + r[3] == ay + ah for r in frames), frames
        print('PASS first visible frame and every mapped frame at screen bottom', flush=True)
        print('PASS global hotkey and bottom panel geometry', flush=True)
        for attempt in range(2):
            key('space')
            wait_for(lambda: evaluate("global.display.focus_window?.get_title() === 'MPaste Preview'"), 'Space preview focus')
            time.sleep(.2)
            geometry = evaluate("""(()=>{
                const w=global.display.focus_window, r=w.get_frame_rect();
                const a=Main.layoutManager.getWorkAreaForMonitor(w.get_monitor());
                return [r.x,r.y,r.width,r.height,a.x,a.y,a.width,a.height];
            })()""")
            x,y,w,h,ax,ay,aw,ah=geometry
            # Fixed width; the height follows the content, from 240 up to 85% of the work area.
            assert w==min(980,aw-32) and 240<=h<=max(240,round(ah*0.85)),geometry
            assert abs(x-(ax+(aw-w)//2))<=1 and abs(y-(ay+(ah-h)//2))<=1,geometry
            panel = evaluate("(()=>{const w=global.get_window_actors().map(a=>a.meta_window).find(w=>w.get_title()==='MPaste');const r=w.get_frame_rect();return [r.x,r.y,r.width,r.height];})()")
            assert panel == rect, (panel,rect)
            key('Escape')
            wait_for(lambda: evaluate("global.display.focus_window?.get_title() === 'MPaste'"), 'preview dismissal')
        print('PASS preview geometry fitted to content, repeat opening and unchanged panel bounds',flush=True)
        key('Home')
        for _ in range(3): key('Right')
        key('Return')
        wait_for(lambda: (base / 'pasted.txt').exists() and (base / 'pasted.txt').read_text() == 'Rich history', 'historical rich-text paste')
        assert 'font-weight:700' in (base / 'pasted.html').read_text(), 'HTML bold formatting lost'
        assert evaluate("global.display.focus_window?.get_title() === 'MPaste Test Target'")
        time.sleep(.5)
        assert len(list((base / 'history/Clipboard').glob('*.mpaste'))) == count
        assert app.poll() is None
        print('PASS historical paste, focus restoration and no self-capture duplicate', flush=True)
        # Reopen on the pointer monitor while focus stays on the other monitor.
        # Check the real Qt surface on every frame, not only Mutter's final rect.
        for monitor in [1, 0]:
            assert evaluate(f"""(()=>{{
                const target=global.get_window_actors().map(a=>a.meta_window)
                    .find(w=>w.get_title()==='MPaste Test Target');
                target.move_to_monitor({1-monitor});
                Main.activateWindow(target);
                return true;
            }})()""")
            wait_for(lambda: evaluate(f"global.display.focus_window?.get_monitor() === {1-monitor} && global.display.focus_window?.get_title() === 'MPaste Test Target'"), 'target monitor')
            evaluate(f"(()=>{{const m=Main.layoutManager.monitors[{monitor}];this.pointer.notify_absolute_motion(GLib.get_monotonic_time(),m.x+20,m.y+20);return true;}})()")
            # Allow Qt's deferred application-inactive hide to finish after the
            # previous popup before sending the next real shortcut.
            time.sleep(.25)
            evaluate("""this.panelFrames=[];
                this.frameTrace=global.stage.connect('before-paint',()=>{
                    for(const actor of global.get_window_actors()) {
                        const w=actor.meta_window;
                        if(w.get_title()!=='MPaste'||!actor.is_visible()||actor.opacity===0)continue;
                        const r=w.get_frame_rect();
                        this.panelFrames.push([r.x,r.y,r.width,r.height]);
                    }
                }); true""")
            key('Alt_L', 'q')
            wait_for(lambda: evaluate("global.display.focus_window?.get_title() === 'MPaste'"), 'panel on pointer monitor')
            time.sleep(.3)
            area = evaluate(f"(()=>{{const a=Main.layoutManager.getWorkAreaForMonitor({monitor});return [a.x,a.y,a.width,a.height];}})()")
            frames = evaluate('global.stage.disconnect(this.frameTrace);this.panelFrames')
            ax,ay,aw,ah = area
            assert frames and all(r[0]==ax and r[2]==aw and r[1]+r[3]==ay+ah for r in frames), (monitor,area,frames)
            key('Alt_L', 'q')
            wait_for(lambda: evaluate("global.display.focus_window?.get_title() === 'MPaste Test Target'"), 'panel dismissal')
            time.sleep(.25)
        print('PASS pointer monitor overrides focused monitor, with correct width before first frame', flush=True)
        # Test service reconnection without restarting MPaste.
        for action in ['disable', 'enable']:
            subprocess.run(['gnome-extensions', action, 'mpaste@mpaste.local'], env=env, check=True)
        time.sleep(.5)
        copy({'text': 'After extension reconnect'}, 'type=1')
        key('Alt_L', 'q')
        wait_for(lambda: evaluate("global.display.focus_window?.get_title() === 'MPaste'"), 'reconnected shortcut')
        print('PASS extension reconnect', flush=True)
        if os.environ.get('MPASTE_MENU_BLUR_PROBE'):
            probe = launch([os.environ['MPASTE_MENU_BLUR_PROBE']], 'blur-probe.log')
            wait_for(lambda: evaluate("global.display.focus_window?.get_title() === 'MPaste Blur Test'"), 'blur test scene')
            for cycle in range(3):
                key('F12')
                time.sleep(.3)
                if cycle == 0:
                    screenshot_path = str(base / 'native-menu-blur.png')
                    evaluate("Gio._promisify(Shell.Screenshot.prototype,'screenshot'); true")
                    evaluate("this.captureDone=false; this.captureError=null; "
                        "const stream=Gio.File.new_for_path(" + json.dumps(screenshot_path) + ").replace(null,false,Gio.FileCreateFlags.NONE,null);"
                        "new Shell.Screenshot().screenshot(false,stream).then(()=>{stream.close(null);this.captureDone=true;}).catch(e=>{this.captureError=String(e);});true")
                    wait_for(lambda: evaluate('this.captureDone'), 'compositor screenshot')
                key('Escape')
                time.sleep(.1)
                assert probe.poll() is None, 'Popup blur lifecycle crashed'
            assert 'compositor blur supported: true' in (base / 'blur-probe.log').read_text()
            print('PASS native menu blur capability, compositor capture and popup recreation',flush=True)
            key('Alt_L', 'q')
            wait_for(lambda: evaluate("global.display.focus_window?.get_title() === 'MPaste'"), 'frosted panel focus')
            time.sleep(.3)
            evaluate("this.captureDone=false; "
                "const panelStream=Gio.File.new_for_path(" + json.dumps(str(base / 'native-panel-blur.png')) + ").replace(null,false,Gio.FileCreateFlags.NONE,null);"
                "new Shell.Screenshot().screenshot(false,panelStream).then(()=>{panelStream.close(null);this.captureDone=true;});true")
            wait_for(lambda: evaluate('this.captureDone'), 'panel compositor screenshot')
            assert 'compositor blur supported: true' in (base / 'app.log').read_text()
            print('PASS native panel blur capability and compositor capture',flush=True)
            if os.environ.get('MPASTE_GLASS_POPUPS'):
                evaluate('this.pointer.notify_absolute_motion(GLib.get_monotonic_time(),120,610);true')
                time.sleep(.3)
                evaluate("this.captureDone=false; "
                    "const hoverOutput=Gio.File.new_for_path(" + json.dumps(str(base / 'hover-glass.png')) + ").replace(null,false,Gio.FileCreateFlags.NONE,null);"
                    "new Shell.Screenshot().screenshot(false,hoverOutput).then(()=>{hoverOutput.close(null);this.captureDone=true;});true")
                wait_for(lambda: evaluate('this.captureDone'), 'hover tool glass capture')
                assert app.poll() is None
                for popup_key, popup_name in [('F2','settings'),('F3','preview'),('F4','details'),('F5','about'),('F6','alias')]:
                    evaluate("Main.activateWindow(global.get_window_actors().map(a=>a.meta_window).find(w=>w.get_title()==='MPaste Blur Test'));true")
                    time.sleep(.1)
                    key(popup_key)
                    wait_for(lambda: evaluate("global.display.focus_window?.get_title() === 'MPaste Glass Popup'"), popup_name + ' popup focus')
                    time.sleep(.25)
                    evaluate("this.captureDone=false; "
                        "const output=Gio.File.new_for_path(" + json.dumps(str(base / (popup_name + '-glass.png'))) + ").replace(null,false,Gio.FileCreateFlags.NONE,null);"
                        "new Shell.Screenshot().screenshot(false,output).then(()=>{output.close(null);this.captureDone=true;});true")
                    wait_for(lambda: evaluate('this.captureDone'), popup_name + ' compositor capture')
                    key('Escape')
                    time.sleep(.15)
                assert probe.poll() is None
                print('PASS settings, preview, details, About and alias glass surfaces',flush=True)
        return 0
    finally:
        for proc in reversed(processes):
            if proc.poll() is None:
                proc.terminate()
                try: proc.wait(timeout=5)
                except subprocess.TimeoutExpired: proc.kill(); proc.wait()


if __name__ == '__main__':
    sys.exit(main())

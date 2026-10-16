import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import Clutter from 'gi://Clutter';
import Meta from 'gi://Meta';
import Shell from 'gi://Shell';
import * as Main from 'resource:///org/gnome/shell/ui/main.js';
import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';
export default class Driver extends Extension {
 enable() {
  this.keyboard = global.stage.context.get_backend().get_default_seat().create_virtual_device(Clutter.InputDeviceType.KEYBOARD_DEVICE);
  this.pointer = global.stage.context.get_backend().get_default_seat().create_virtual_device(Clutter.InputDeviceType.POINTER_DEVICE);
  this.object = Gio.DBusExportedObject.wrapJSObject('<node><interface name="org.mpaste.Test"><method name="Eval"><arg type="s" direction="in" name="code"/><arg type="s" direction="out" name="result"/></method></interface></node>',this);
  this.object.export(Gio.DBus.session,'/org/mpaste/Test');
 }
 Eval(code) { try { return JSON.stringify(eval(code)) ?? 'null'; } catch(e) {return String(e.stack);} }
 disable() { this.object.unexport(); this.keyboard=null; this.pointer=null; }
}

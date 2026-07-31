/*
 * cdwindow.vala
 * Simple GtkWindow and GtkPopover subclasses that allow custom drawing with Cairo.
 * 
 * compile with:
 * valac --pkg gtk4 --pkg graphene-gobject-1.0 -c cdwindow.vala -C -H cdwindow.h
 * 
 * Copyright 2024-2026 Daniel Kondor <kondor.dani@gmail.com>
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 * 
 * 
 */


public class CDWindow : Gtk.Window {
	public signal void pending_unmap();
	
	public signal void draw(Cairo.Context ctx);
	
	public CDWindow() {
		Object();
		base.set_decorated(false);
		this.add_css_class("cairo-dock");
	}
	
	public override void unmap () {
		pending_unmap();
		base.unmap();
	}
	
	public override void snapshot (Gtk.Snapshot snapshot) {
		var rect = Graphene.Rect();
		rect.init(0.0f, 0.0f, get_width(), get_height());
		var ctx = snapshot.append_cairo(rect);
		draw(ctx);
	}
}


public class CDPopup : Gtk.Popover {
	public signal void pending_unmap();
	
	public signal void draw(Cairo.Context ctx);
	
	protected bool snapshot_base_first = true;
	
	public CDPopup() {
		Object();
		base.set_has_arrow(false);
		this.add_css_class("cairo-dock");
	}
	
	public override void unmap () {
		pending_unmap();
		base.unmap();
	}
	
	public override void snapshot (Gtk.Snapshot snapshot) {
		if (snapshot_base_first) base.snapshot(snapshot);
		var rect = Graphene.Rect();
		rect.init(0.0f, 0.0f, get_width(), get_height());
		var ctx = snapshot.append_cairo(rect);
		draw(ctx);
		if (!snapshot_base_first) base.snapshot(snapshot);
	}
}


public class CDMenu : CDPopup {
	private Gtk.Box box;
	private CDMenu open_submenu = null;
	private GLib.Source submenu_source = null;
	
	public CDMenu() {
		Object(css_name: "cdmenu");
		box = new Gtk.Box(Gtk.Orientation.VERTICAL, 0);
		box.set_margin_top(4);
		box.set_margin_bottom(4);
		set_autohide(true); // set as modal -- not sure if this does anything
		set_child(box);
		snapshot_base_first = false;
	}
	
	public void add_menu_item(Gtk.Widget item) {
		box.append(item);
	}
	
	public void popup_submenu(CDMenu submenu) {
		close_submenu();
		
		open_submenu = submenu;
		submenu_source = new GLib.TimeoutSource(200);
		submenu_source.set_callback(() => {
			if(get_visible() && open_submenu != null) {
				open_submenu.popup();
				submenu_source = null; // will be destroyed after the callback returns
			}
			return GLib.Source.REMOVE;
		});
		submenu_source.attach(null);
	}
	
	public void close_submenu() {
		if(open_submenu != null) {
			if(submenu_source != null) {
				// not opened yet
				submenu_source.destroy();
				submenu_source = null;
			}
			else open_submenu.popdown(); // already open
			open_submenu = null;
		}
	}
	
	public void popdown_recursive() {
		popdown();
		CDMenuItem parent = get_parent() as CDMenuItem;
		while(parent != null) {
			CDMenu parent_menu = parent.get_parent_menu();
			parent = parent_menu.get_parent() as CDMenuItem;
			parent_menu.popdown();
		}
	}
}


public class CDArrow : Gtk.Widget {
	private Gsk.Path path;
	
	public CDArrow() {
		Object(css_name : "cdarrow", valign : Gtk.Align.CENTER, margin_end : 4);
		set_size_request(8, 12);
		
		var builder = new Gsk.PathBuilder();
		builder.move_to(1.0f, 0.5f);
		builder.line_to(7.0f, 6.0f);
		builder.line_to(1.0f, 11.5f);
		builder.close();
		path = builder.to_path();
	}
	
	public override void snapshot (Gtk.Snapshot snapshot) {
		snapshot.append_fill(path, Gsk.FillRule.WINDING, get_color());
	}
}

public class CDMenuItemBase : Gtk.Box {
	private CDMenu parent_menu;
	protected CDMenu submenu;
	
	public CDMenuItemBase(CDMenu parent_menu) {
		Object(orientation : Gtk.Orientation.HORIZONTAL, spacing : 0, css_name : "cdmenuitem");
		submenu = null;
		
		var ctrl = new Gtk.EventControllerMotion();
		ctrl.enter.connect((ctrl, x, y) => {
			if(submenu != null) {
				if(submenu.get_visible()) return; // already open, nothing to do
				parent_menu.popup_submenu(submenu);
			}
			else parent_menu.close_submenu();
			this.set_state_flags(Gtk.StateFlags.PRELIGHT, false);
			this.queue_draw();
		});
		ctrl.leave.connect((ctrl) => {
			if(submenu != null && submenu.get_visible()) return; // ignore, we just entered out own submenu
			this.unset_state_flags(Gtk.StateFlags.PRELIGHT);
			this.queue_draw();
		});
		this.add_controller(ctrl);
		
		parent_menu.add_menu_item(this);
		this.parent_menu = parent_menu;
	}
	
	public CDMenu get_parent_menu() { return parent_menu; }
}

public class CDMenuItem : CDMenuItemBase {
	public signal void clicked();
	
	private Gtk.Label lbl;
	private Gtk.Image img;
	
	public CDMenuItem(string label, CDMenu parent_menu) {
		base(parent_menu);
		
		img = new Gtk.Image();
		img.set_size_request(24, 24);
		img.set_margin_top(3);
		img.set_margin_bottom(3);
		img.set_margin_start(4);
		this.append(img);
		if (label != null)
		{
			lbl = new Gtk.Label(label);
			lbl.set_hexpand(true);
			lbl.set_halign(Gtk.Align.START);
			lbl.set_margin_start(4);
			lbl.set_margin_end(6);
			this.append(lbl);
		}
		else lbl = null;
		
		var click = new Gtk.GestureClick();
		click.set_button(1); // only the left mouse button
		click.pressed.connect((click, n_press, x, y) => {
			if(n_press == 1) click.set_state(Gtk.EventSequenceState.CLAIMED);
		});
		click.released.connect((click, n_press, x, y) => {
			if(n_press == 1) clicked();
			parent_menu.popdown_recursive();
		});
		this.add_controller(click);
	}
	
	public void set_image(Gdk.Paintable new_img) {
		int h = new_img.get_intrinsic_height();
		int w = new_img.get_intrinsic_width();
		if(h == 0) h = 24;
		if(w == 0) w = 24;
		img.set_size_request(w, h);
		int mt = 2, mb = 2;
		if(h < 24) {
			int tmp = (24 - h) / 2;
			mt += tmp;
			mb += tmp;
			if(h % 2 == 1) mt += 1;
		}
		img.set_margin_top(mt);
		img.set_margin_bottom(mb);
		img.set_from_paintable(new_img);
		this.queue_draw();
	}
	
	public void set_submenu(CDMenu menu) {
		submenu = menu;
		menu.set_parent(this);
		menu.set_position(Gtk.PositionType.RIGHT);
		
		this.append(new CDArrow());
	}
}

public class CDMenuSeparator : Gtk.Widget {
	private Gsk.Stroke stroke;
	
	public CDMenuSeparator(CDMenu parent_menu) {
		Object(css_name : "cdmenuseparator");
		set_size_request(-1, 3);
		parent_menu.add_menu_item(this);
		
		stroke = new Gsk.Stroke(1.0f);
	}
	
	public override void snapshot (Gtk.Snapshot snapshot) {
		var builder = new Gsk.PathBuilder();
		int w = get_width();
		builder.move_to(0.05f * w, 0.5f);
		builder.line_to(0.95f * w, 0.5f);
		//!! TODO: consider append_linear_gradient() instead?
		snapshot.append_stroke(builder.to_path(), stroke, get_color());
	}
}


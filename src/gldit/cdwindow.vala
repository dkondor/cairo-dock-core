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
	
	public CDWindow () {
		Object();
		base.set_decorated(false);
		this.add_css_class("cairo-dock");
	}
	
	public override void unmap () {
		pending_unmap();
		base.unmap();
	}
	
	public override void snapshot (Gtk.Snapshot snapshot) {
		var surface = this.get_surface();
		var rect = Graphene.Rect();
		rect.init(0.0f, 0.0f, surface.get_width(), surface.get_height());
		var ctx = snapshot.append_cairo(rect);
		draw(ctx);
	}
}


public class CDPopup : Gtk.Popover {
	public signal void pending_unmap();
	
	public signal void draw(Cairo.Context ctx);
	
	public CDPopup () {
		Object();
		base.set_has_arrow(false);
		this.add_css_class("cairo-dock");
	}
	
	public override void unmap () {
		pending_unmap();
		base.unmap();
	}
	
	public override void snapshot (Gtk.Snapshot snapshot) {
		base.snapshot(snapshot);
		var surface = this.get_surface();
		var rect = Graphene.Rect();
		rect.init(0.0f, 0.0f, surface.get_width(), surface.get_height());
		var ctx = snapshot.append_cairo(rect);
		draw(ctx);
	}
}


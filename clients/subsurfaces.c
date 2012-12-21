/*
 * Copyright © 2010 Intel Corporation
 * Copyright © 2012 Collabora, Ltd.
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cairo.h>
#include <math.h>
#include <assert.h>

#include <linux/input.h>
#include <wayland-client.h>

#include "window.h"

struct subwindow {
	struct window *window;
	struct widget *widget;
	struct wl_subsurface *subsurface;
};

struct demoapp {
	struct display *display;
	struct window *window;
	struct widget *widget;

	struct wl_subcompositor *subcompositor;

	struct subwindow *sub;
};

static void
sub_redraw_handler(struct widget *widget, void *data)
{
	struct subwindow *sub = data;
	cairo_surface_t *surface;
	cairo_t *cr;
	struct rectangle allocation;

	widget_get_allocation(sub->widget, &allocation);

	surface = window_get_surface(sub->window);

	cr = cairo_create(surface);
	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	cairo_rectangle(cr,
			allocation.x,
			allocation.y,
			allocation.width,
			allocation.height);
	cairo_set_source_rgba(cr, 0.8, 0, 0, 0.8);
	cairo_fill(cr);

	cairo_destroy(cr);

	cairo_surface_destroy(surface);
}

static struct subwindow *
subsurface_create(struct demoapp *app)
{
	struct subwindow *sub;
	struct wl_surface *parent;
	struct wl_surface *surface;

	if (!app->subcompositor)
		return NULL;

	sub = calloc(1, sizeof *sub);
	if (!sub)
		return NULL;

	sub->window = window_create_custom(app->display);
	sub->widget = window_add_widget(sub->window, sub);

	widget_set_redraw_handler(sub->widget, sub_redraw_handler);

	widget_schedule_resize(sub->widget, 150, 100);

	parent = window_get_wl_surface(app->window);
	surface = window_get_wl_surface(sub->window);
	sub->subsurface = wl_subcompositor_get_subsurface(app->subcompositor,
							  surface, parent);
	wl_subsurface_set_position(sub->subsurface, 50, 50);

	return sub;
}

static void
redraw_handler(struct widget *widget, void *data)
{
	struct demoapp *app = data;
	cairo_surface_t *surface;
	cairo_t *cr;
	struct rectangle allocation;

	widget_get_allocation(app->widget, &allocation);

	surface = window_get_surface(app->window);

	cr = cairo_create(surface);
	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	cairo_rectangle(cr,
			allocation.x,
			allocation.y,
			allocation.width,
			allocation.height);
	cairo_set_source_rgba(cr, 0, 0, 0, 0.8);
	cairo_fill(cr);

	cairo_destroy(cr);

	cairo_surface_destroy(surface);
}

static void
keyboard_focus_handler(struct window *window,
		       struct input *device, void *data)
{
	struct demoapp *app = data;

	(void)app;
	window_schedule_redraw(app->window);
}

static void
key_handler(struct window *window, struct input *input, uint32_t time,
	    uint32_t key, uint32_t sym,
	    enum wl_keyboard_key_state state, void *data)
{
	struct demoapp *app = data;

	if (state == WL_KEYBOARD_KEY_STATE_RELEASED)
		return;

	switch (sym) {
	case XKB_KEY_Escape:
		display_exit(app->display);
		break;
	}
}

static void
global_handler(struct display *display, uint32_t name, const char *interface,
	       uint32_t version, void *data)
{
	struct demoapp *app = data;

	if (!strcmp(interface, "wl_subcompositor") && version >= 1) {
		app->subcompositor =
			display_bind(display, name,
				     &wl_subcompositor_interface, 1);
	}
}

static struct demoapp *
demoapp_create(struct display *display)
{
	struct demoapp *app;

	app = calloc(1, sizeof *app);
	if (!app)
		return NULL;

	app->display = display;
	display_set_user_data(app->display, app);
	display_set_global_handler(app->display, global_handler);

	app->window = window_create(app->display);
	app->widget = frame_create(app->window, app);
	window_set_title(app->window, "Wayland Sub-surface Demo");

	window_set_key_handler(app->window, key_handler);
	window_set_user_data(app->window, app);
	window_set_keyboard_focus_handler(app->window, keyboard_focus_handler);

	widget_set_redraw_handler(app->widget, redraw_handler);

	widget_schedule_resize(app->widget, 500, 400);

	app->sub = subsurface_create(app);

	return app;
}

static void
demoapp_destroy(struct demoapp *app)
{
	widget_destroy(app->widget);
	window_destroy(app->window);

	if (app->subcompositor)
		wl_subcompositor_destroy(app->subcompositor);

	free(app);
}

int
main(int argc, char *argv[])
{
	struct display *display;
	struct demoapp *app;

	display = display_create(argc, argv);
	if (display == NULL) {
		fprintf(stderr, "failed to create display: %m\n");
		return -1;
	}

	app = demoapp_create(display);

	display_run(display);

	demoapp_destroy(app);
	display_destroy(display);

	return 0;
}

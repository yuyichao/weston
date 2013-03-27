/*
 * Copyright © 2012-2013 Intel Corporation
 *
 * Permission to use, copy, modify, distribute, and sell this software and
 * its documentation for any purpose is hereby granted without fee, provided
 * that the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of the copyright holders not be used in
 * advertising or publicity pertaining to distribution of the software
 * without specific, written prior permission.  The copyright holders make
 * no representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS
 * SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS, IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER
 * RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF
 * CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <wayland-server.h>
#include "compositor.h"

#include "input-method-server-protocol.h"

struct input_panel {
	struct wl_listener destroy_listener;
	struct wl_listener show_input_panel_listener;
	struct wl_listener hide_input_panel_listener;
	struct wl_listener update_input_panel_listener;

	struct wl_resource *binding;

	struct wl_list surfaces;

	bool showing_input_panels;

	struct {
		struct weston_layer input_panel;

		struct weston_layer *previous;
		struct weston_layer *next;

		bool visible;
	} layer;

	struct {
		struct weston_surface *surface;
		pixman_box32_t cursor_rectangle;
	} text_input;

};

struct input_panel_surface {
	struct wl_resource resource;

	struct input_panel *input_panel;

	struct wl_list link;
	struct weston_surface *surface;
	struct wl_listener surface_destroy_listener;

	struct weston_output *output;
	uint32_t panel;
};


static void
show_input_panels(struct wl_listener *listener, void *data)
{
	struct input_panel *input_panel =
		container_of(listener, struct input_panel,
			     show_input_panel_listener);
	struct input_panel_surface *surface, *next;
	struct weston_surface *ws;

	input_panel->text_input.surface = (struct weston_surface*)data;

	if (input_panel->showing_input_panels)
		return;

	input_panel->showing_input_panels = true;

	if (input_panel->layer.visible) {
		wl_list_insert(&input_panel->layer.previous->link,
			       &input_panel->layer.input_panel.link);
	}

	wl_list_for_each_safe(surface, next,
			      &input_panel->surfaces, link) {
		ws = surface->surface;
		if (!ws->buffer_ref.buffer)
			continue;
		weston_surface_geometry_dirty(ws);
		wl_list_insert(&input_panel->layer.input_panel.surface_list,
			       &ws->layer_link);
		weston_surface_update_transform(ws);
		weston_surface_damage(ws);
		weston_slide_run(ws, ws->geometry.height, 0, NULL, NULL);
	}
}

static void
hide_input_panels(struct wl_listener *listener, void *data)
{
	struct input_panel *input_panel =
		container_of(listener, struct input_panel,
			     hide_input_panel_listener);
	struct weston_surface *surface, *next;

	if (!input_panel->showing_input_panels)
		return;

	input_panel->showing_input_panels = false;

	if (input_panel->layer.visible)
		wl_list_remove(&input_panel->layer.input_panel.link);

	wl_list_for_each_safe(surface, next,
			      &input_panel->layer.input_panel.surface_list, layer_link)
		weston_surface_unmap(surface);
}

static void
update_input_panels(struct wl_listener *listener, void *data)
{
	struct input_panel *input_panel =
		container_of(listener, struct input_panel,
			     update_input_panel_listener);

	memcpy(&input_panel->text_input.cursor_rectangle, data, sizeof(pixman_box32_t));
}


static void
input_panel_configure(struct weston_surface *surface, int32_t sx, int32_t sy, int32_t width, int32_t height)
{
	struct input_panel_surface *ip_surface = surface->private;
	struct input_panel *input_panel = ip_surface->input_panel;
	struct weston_mode *mode;
	float x, y;
	uint32_t show_surface = 0;

	if (width == 0)
		return;

	if (!weston_surface_is_mapped(surface)) {
		if (!input_panel->showing_input_panels)
			return;

		show_surface = 1;
	}

	if (ip_surface->panel) {
		x = input_panel->text_input.surface->geometry.x + input_panel->text_input.cursor_rectangle.x2;
		y = input_panel->text_input.surface->geometry.y + input_panel->text_input.cursor_rectangle.y2;
	} else {
		mode = ip_surface->output->current;

		x = ip_surface->output->x + (mode->width - width) / 2;
		y = ip_surface->output->y + mode->height - height;
	}

	weston_surface_configure(surface,
				 x, y,
				 width, height);

	if (show_surface) {
		wl_list_insert(&input_panel->layer.input_panel.surface_list,
			       &surface->layer_link);
		weston_surface_update_transform(surface);
		weston_surface_damage(surface);
		weston_slide_run(surface, surface->geometry.height, 0, NULL, NULL);
	}
}

static void
destroy_input_panel_surface(struct input_panel_surface *input_panel_surface)
{
	wl_list_remove(&input_panel_surface->surface_destroy_listener.link);
	wl_list_remove(&input_panel_surface->link);

	input_panel_surface->surface->configure = NULL;

	free(input_panel_surface);
}

static struct input_panel_surface *
get_input_panel_surface(struct weston_surface *surface)
{
	if (surface->configure == input_panel_configure) {
		return surface->private;
	} else {
		return NULL;
	}
}

static void
input_panel_handle_surface_destroy(struct wl_listener *listener, void *data)
{
	struct input_panel_surface *ipsurface = container_of(listener,
							     struct input_panel_surface,
							     surface_destroy_listener);

	if (ipsurface->resource.client) {
		wl_resource_destroy(&ipsurface->resource);
	} else {
		wl_signal_emit(&ipsurface->resource.destroy_signal,
			       &ipsurface->resource);
		destroy_input_panel_surface(ipsurface);
	}
}
static struct input_panel_surface *
create_input_panel_surface(struct input_panel *input_panel,
			   struct weston_surface *surface)
{
	struct input_panel_surface *input_panel_surface;

	input_panel_surface = calloc(1, sizeof *input_panel_surface);
	if (!input_panel_surface)
		return NULL;

	surface->configure = input_panel_configure;
	surface->private = input_panel_surface;

	input_panel_surface->input_panel = input_panel;

	input_panel_surface->surface = surface;

	wl_signal_init(&input_panel_surface->resource.destroy_signal);
	input_panel_surface->surface_destroy_listener.notify = input_panel_handle_surface_destroy;
	wl_signal_add(&surface->surface.resource.destroy_signal,
		      &input_panel_surface->surface_destroy_listener);

	wl_list_init(&input_panel_surface->link);

	return input_panel_surface;
}

static void
input_panel_surface_set_toplevel(struct wl_client *client,
				 struct wl_resource *resource,
				 struct wl_resource *output_resource,
				 uint32_t position)
{
	struct input_panel_surface *input_panel_surface = resource->data;
	struct input_panel *input_panel = input_panel_surface->input_panel;

	wl_list_insert(&input_panel->surfaces,
		       &input_panel_surface->link);

	input_panel_surface->output = output_resource->data;
	input_panel_surface->panel = 0;
}

static void
input_panel_surface_set_panel(struct wl_client *client,
			      struct wl_resource *resource)
{
	struct input_panel_surface *input_panel_surface = resource->data;
	struct input_panel *input_panel = input_panel_surface->input_panel;

	wl_list_insert(&input_panel->surfaces,
		       &input_panel_surface->link);

	input_panel_surface->panel = 1;
}

static const struct input_panel_surface_interface input_panel_surface_implementation = {
	input_panel_surface_set_toplevel,
	input_panel_surface_set_panel
};

static void
destroy_input_panel_surface_resource(struct wl_resource *resource)
{
	struct input_panel_surface *ipsurf = resource->data;

	destroy_input_panel_surface(ipsurf);
}

static void
input_panel_get_input_panel_surface(struct wl_client *client,
				    struct wl_resource *resource,
				    uint32_t id,
				    struct wl_resource *surface_resource)
{
	struct weston_surface *surface = surface_resource->data;
	struct input_panel *input_panel = resource->data;
	struct input_panel_surface *ipsurf;

	if (get_input_panel_surface(surface)) {
		wl_resource_post_error(surface_resource,
				       WL_DISPLAY_ERROR_INVALID_OBJECT,
				       "input_panel::get_input_panel_surface already requested");
		return;
	}

	ipsurf = create_input_panel_surface(input_panel, surface);
	if (!ipsurf) {
		wl_resource_post_error(surface_resource,
				       WL_DISPLAY_ERROR_INVALID_OBJECT,
				       "surface->configure already set");
		return;
	}

	ipsurf->resource.destroy = destroy_input_panel_surface_resource;
	ipsurf->resource.object.id = id;
	ipsurf->resource.object.interface = &input_panel_surface_interface;
	ipsurf->resource.object.implementation =
		(void (**)(void)) &input_panel_surface_implementation;
	ipsurf->resource.data = ipsurf;

	wl_client_add_resource(client, &ipsurf->resource);
}

static const struct input_panel_interface input_panel_implementation = {
	input_panel_get_input_panel_surface
};

static void
input_panel_destroy(struct wl_listener *listener, void *data)
{
	struct input_panel *input_panel = 
		container_of(listener, struct input_panel, destroy_listener);

	wl_list_remove(&input_panel->show_input_panel_listener.link);
	wl_list_remove(&input_panel->hide_input_panel_listener.link);
	wl_list_remove(&input_panel->update_input_panel_listener.link);

	free(input_panel);
}

static void
unbind_input_panel(struct wl_resource *resource)
{
	struct input_panel *input_panel = resource->data;

	input_panel->binding = NULL;
	free(resource);
}

static void
bind_input_panel(struct wl_client *client,
	      void *data, uint32_t version, uint32_t id)
{
	struct input_panel *input_panel = data;
	struct wl_resource *resource;

	resource = wl_client_add_object(client, &input_panel_interface,
					&input_panel_implementation,
					id, input_panel);

	if (input_panel->binding != NULL) {
		wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
				       "interface object already bound");
		wl_resource_destroy(resource);
		return;
	}
	
	resource->destroy = unbind_input_panel;
	input_panel->binding = resource;
}

struct input_panel *
input_panel_create(struct weston_compositor *ec)
{
	struct input_panel *input_panel;

	input_panel = malloc(sizeof *input_panel);
	if (!input_panel)
		return NULL;
	memset(input_panel, 0, sizeof *input_panel);

	input_panel->destroy_listener.notify = input_panel_destroy;
	wl_signal_add(&ec->destroy_signal, &input_panel->destroy_listener);
	input_panel->show_input_panel_listener.notify = show_input_panels;
	wl_signal_add(&ec->show_input_panel_signal, &input_panel->show_input_panel_listener);
	input_panel->hide_input_panel_listener.notify = hide_input_panels;
	wl_signal_add(&ec->hide_input_panel_signal, &input_panel->hide_input_panel_listener);
	input_panel->update_input_panel_listener.notify = update_input_panels;
	wl_signal_add(&ec->update_input_panel_signal, &input_panel->update_input_panel_listener);

	wl_list_init(&input_panel->surfaces);

	weston_layer_init(&input_panel->layer.input_panel, NULL);

	if (wl_display_add_global(ec->wl_display, &input_panel_interface,
				  input_panel, bind_input_panel) == NULL)
		return NULL;

	return input_panel;
}

void
input_panel_show_layer(struct input_panel *input_panel,
		       struct weston_layer *previous,
		       struct weston_layer *next)
{
	if (input_panel->layer.visible) {
		fprintf(stderr, "%s - input panel layer is already shown.\n", __FUNCTION__);
		return;
	}

	input_panel->layer.previous = previous;
	input_panel->layer.next = next;

	input_panel->layer.visible = true;

	if (input_panel->showing_input_panels) {
		wl_list_insert(&input_panel->layer.previous->link,
			       &input_panel->layer.input_panel.link);
		wl_list_insert(&input_panel->layer.input_panel.link,
			       &input_panel->layer.next->link);
	} else {
		wl_list_insert(&previous->link, &next->link);

	}
}

void
input_panel_hide_layer(struct input_panel *input_panel)
{
	if (!input_panel->layer.visible) {
		fprintf(stderr, "%s - input panel layer is already hidden.\n", __FUNCTION__);
		return;
	}

	input_panel->layer.visible = false;

	if (input_panel->showing_input_panels)
		wl_list_remove(&input_panel->layer.input_panel.link);
}


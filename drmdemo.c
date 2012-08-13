/* -*- mode: c; c-basic-offset: 2 -*- */

#include <glib.h>

#include <fcntl.h>
#include <unistd.h>

#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include <xf86drm.h>
#include <xf86drmMode.h>

#include <cairo.h>
#include <librsvg/rsvg.h>
#include <librsvg/rsvg-cairo.h>

#include "device.c"
#include "buffer.c"

typedef struct {
  Device *device;
  Buffer *buffer;

  int time;
} AppData;

static void
draw_on_buffer (AppData *appdata)
{
  cairo_surface_t *surface, *craig;
  cairo_t *cr;
  RsvgHandle *tiger_handle;
  GError *error = NULL;

  Buffer *buffer = appdata->buffer;

  surface = cairo_image_surface_create_for_data (buffer->pixels,
                                                 CAIRO_FORMAT_ARGB32,
                                                 buffer->width,
                                                 buffer->height,
                                                 buffer->stride);
  cr = cairo_create (surface);

  cairo_set_source_rgb (cr, 1, 1, 1);
  cairo_paint (cr);

  cairo_set_source_rgb (cr, 1, 0, 0);
  cairo_rectangle (cr, 75, 75, 681, 800);
  cairo_fill (cr);

  craig = cairo_image_surface_create_from_png ("craig.png");
  cairo_translate (cr, 50, 50);
  cairo_set_source_surface (cr, craig, 0, 0);
  cairo_rectangle (cr, 0, 0, 681, 800);
  cairo_fill (cr);
  cairo_surface_destroy (craig);

  tiger_handle = rsvg_handle_new_from_file ("tiger.svg", &error);
  if (error != NULL)
    {
      g_warning ("Error loading tiger.svg: %s", error->message);
      g_error_free (error);
    }
  else
    {
      cairo_translate (cr, 500 + 10 * appdata->time, 200);
      rsvg_handle_render_cairo (tiger_handle, cr);
      g_object_unref (tiger_handle);
    }

  cairo_destroy (cr);
  cairo_surface_destroy (surface);

  ++appdata->time;
}

int
main (int argc, char **argv)
{
  Buffer buffer;
  Device device;
  AppData appdata;
  GMainLoop *mainloop;
  int ret = 1;

  g_type_init ();

  mainloop = g_main_loop_new (NULL, FALSE);

  memset (&appdata, 0, sizeof (Buffer));
  memset (&buffer, 0, sizeof (Buffer));
  memset (&device, 0, sizeof (Device));

  appdata.buffer = &buffer;
  appdata.device = &device;

  if (!device_open (&device))
    goto out;

  if (!device_find_crtc (&device))
    goto out;

  buffer.device = &device;

  /* Use the current resolution of the card. */
  buffer.width = device.crtc->mode.hdisplay;
  buffer.height = device.crtc->mode.vdisplay;

  if (!buffer_new (&buffer))
    goto out;

  if (!buffer_map (&buffer))
    goto out;

  if (!device_show_buffer (&device, buffer.id, 0, 0))
    {
      g_warning ("Could not show our buffer");
      goto out;
    }

  g_idle_add ((GSourceFunc) draw_on_buffer, &appdata);
  g_timeout_add_seconds (5, (GSourceFunc) g_main_loop_quit, mainloop);
  g_main_loop_run (mainloop);

  if (!device_show_buffer (&device, device.crtc->buffer_id, device.crtc->x, device.crtc->y))
    {
      g_warning ("Could not show previous buffer");
      goto out;
    }

  /* Do the necessary cleanup. */
  ret = 0;

 out:
  buffer_free (&buffer);
  device_free (&device);
  return ret;
}

/* -*- mode: c; c-basic-offset: 2 -*- */

#include <glib.h>
#include <glib-unix.h>

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

#include "device.c"
#include "buffer.c"

typedef struct {
  Buffer buffer;
  cairo_surface_t *surface;
  cairo_t *cr;
} AppBuf;

#define NUM_BUFFERS 10

typedef struct {
  Device *device;

  AppBuf appbuf[NUM_BUFFERS];
  int curbuf;

  AppBuf cursor[2];
  int cursor_x, cursor_y, cursor_buf;
  int cursor_dir_x, cursor_dir_y;

  int time;
  int x, dir;

  cairo_surface_t *craig;
  RsvgHandle *tiger;
} AppData;

static void
set_up_for_buffer (AppData *appdata, cairo_t **cr)
{
  *cr = appdata->appbuf[appdata->curbuf].cr;
}

static void
swap_buffer (AppData *appdata)
{
  Device *device = appdata->device;
  Buffer *buffer = &appdata->appbuf[appdata->curbuf].buffer;
  device_page_flip (device, buffer->id, appdata);
  appdata->curbuf++;
  if (appdata->curbuf >= NUM_BUFFERS)
    appdata->curbuf = 0;
}

static void
step_cursor (AppData *appdata)
{
  Buffer *buffer = &appdata->cursor[appdata->cursor_buf].buffer;

  device_cursor (appdata->device,
                 buffer->handle,
                 appdata->cursor_x - 32, appdata->cursor_y - 32,
                 buffer->width, buffer->height);

  appdata->cursor_buf = (appdata->cursor_buf + 1) % 2;
  appdata->cursor_x += 6 * appdata->cursor_dir_x;
  appdata->cursor_y += 10 * appdata->cursor_dir_y;
  if (appdata->cursor_x < 0 || appdata->cursor_x > 1920)
    appdata->cursor_dir_x *= -1;
  if (appdata->cursor_y < 0 || appdata->cursor_y > 1080)
    appdata->cursor_dir_y *= -1;
}

static void
step (AppData *appdata)
{
  cairo_t *cr;

  set_up_for_buffer (appdata, &cr);

  cairo_save (cr);
  cairo_set_source_rgb (cr, 1, 1, 1);
  cairo_paint (cr);

  appdata->x += 10 * appdata->dir;
  if (appdata->x > 1239 || appdata->x < 0)
    appdata->dir *= -1;

  cairo_set_source_rgb (cr, 1, 0, 0);
  cairo_rectangle (cr, appdata->x - 50, 75, 681, 800);
  cairo_fill (cr);

  cairo_translate (cr, appdata->x, 200);
  cairo_set_source_surface (cr, appdata->craig, 0, 0);
  cairo_rectangle (cr, 0, 0, 681, 800);
  cairo_fill (cr);
  cairo_restore (cr);

  /*
  cairo_save (cr);
  cairo_scale (cr, 2, 2);
  rsvg_handle_render_cairo (appdata->tiger, cr);
  cairo_restore (cr);
  */

  swap_buffer (appdata);

  step_cursor (appdata);
  step_cursor (appdata);
  step_cursor (appdata);
  step_cursor (appdata);
  step_cursor (appdata);
  step_cursor (appdata);
  step_cursor (appdata);

  ++appdata->time;
}

static gboolean
make_appbuf (AppBuf *appbuf, Device *device, int w, int h)
{
  appbuf->buffer.device = device;
  appbuf->buffer.width = w;
  appbuf->buffer.height = h;
  if (!buffer_new (&appbuf->buffer) || !buffer_map(&appbuf->buffer))
    return FALSE;

  appbuf->surface = cairo_image_surface_create_for_data (appbuf->buffer.pixels,
                                                         CAIRO_FORMAT_ARGB32,
                                                         appbuf->buffer.width,
                                                         appbuf->buffer.height,
                                                         appbuf->buffer.stride);
  appbuf->cr = cairo_create (appbuf->surface);
  return TRUE;
}

static gboolean
make_appbufs (AppData *appdata)
{
  int i, w, h;

  /* Use the current resolution of the card. */
  w = appdata->device->crtc->mode.hdisplay;
  h = appdata->device->crtc->mode.vdisplay;

  for (i = 0; i < NUM_BUFFERS; i++)
    if (!make_appbuf (&appdata->appbuf[i], appdata->device, w, h))
      return FALSE;

  return TRUE;
}

static void
handle_page_flip (int fd, unsigned frame, unsigned sec, unsigned usec, void *data)
{
  AppData *appdata = data;
  step (appdata);
}

static gboolean
handle_drm_event (int fd, GIOCondition cond, gpointer user_data)
{
  drmEventContext evctx = {
    .version = DRM_EVENT_CONTEXT_VERSION,
    .page_flip_handler = handle_page_flip,
  };
  drmHandleEvent (fd, &evctx);
  return G_SOURCE_CONTINUE;
}

static void
draw_cursor (AppBuf *appbuf, double r, double g, double b)
{
  cairo_set_source_rgb (appbuf->cr, r, g, b);
  cairo_arc (appbuf->cr, 32, 32, 32, 0, 3.14 * 2);
  cairo_fill (appbuf->cr);
  cairo_surface_flush (appbuf->surface);
}

int
main (int argc, char **argv)
{
  Device device;
  AppData appdata;
  GMainLoop *mainloop;
  int ret = 1;

  mainloop = g_main_loop_new (NULL, FALSE);

  memset (&appdata, 0, sizeof (AppData));
  memset (&device, 0, sizeof (Device));

  appdata.device = &device;

  if (!device_open (&device))
    goto out;

  if (!device_find_crtc (&device))
    goto out;

  if (!make_appbufs (&appdata))
    goto out;

  make_appbuf (&appdata.cursor[0], appdata.device, 64, 64);
  make_appbuf (&appdata.cursor[1], appdata.device, 64, 64);
  draw_cursor (&appdata.cursor[0], 1.0, 0.0, 0.5);
  draw_cursor (&appdata.cursor[1], 0.5, 0.0, 1.0);

  appdata.craig = cairo_image_surface_create_from_png ("craig.png");
  appdata.tiger = rsvg_handle_new_from_file ("tiger.svg", NULL);

  appdata.cursor_dir_x = 1;
  appdata.cursor_dir_y = 1;
  appdata.dir = 1;

  g_unix_fd_add (device.fd, G_IO_IN, handle_drm_event, &appdata);
  device_show_buffer (&device, appdata.appbuf[0].buffer.id, 0, 0);
  step (&appdata);
  g_main_loop_run (mainloop);

  if (!device_show_buffer (&device, device.crtc->buffer_id, device.crtc->x, device.crtc->y))
    {
      g_warning ("Could not show previous buffer");
      goto out;
    }

  /* Do the necessary cleanup. */
  ret = 0;

 out:
  device_free (&device);

  if (0) buffer_free (NULL);

  cairo_surface_destroy (appdata.craig);
  g_object_unref (appdata.tiger);

  return ret;
}

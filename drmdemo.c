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

  /* rsvg_handle_render_cairo (appdata->tiger, cr); */
  /* cairo_translate (cr, appdata->x, 200); */

  cairo_set_source_rgb (cr, 1, 0, 0);
  cairo_rectangle (cr, appdata->x - 50, 75, 681, 800);
  cairo_fill (cr);

  cairo_translate (cr, appdata->x, 200);
  cairo_set_source_surface (cr, appdata->craig, 0, 0);
  cairo_rectangle (cr, 0, 0, 681, 800);
  cairo_fill (cr);
  cairo_restore (cr);

  swap_buffer (appdata);

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

int
main (int argc, char **argv)
{
  Device device;
  AppData appdata;
  GMainLoop *mainloop;
  /* GError *error = NULL; */
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

  appdata.craig = cairo_image_surface_create_from_png ("craig.png");

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
  /* g_object_unref (appdata.tiger); */

  return ret;
}

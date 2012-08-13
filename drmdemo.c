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

typedef struct {
  int fd;
  drmModeRes *resources;
  drmModeConnector *connector;
  drmModeCrtc *crtc;
} Device;

typedef struct {
  Device *device;

  unsigned char *pixels;
  uint64_t size;

  uint32_t id;
  uint32_t handle;
  uint32_t width;
  uint32_t height;
  uint32_t stride;
} Buffer;

static gboolean
device_open (Device *device)
{
  gboolean ret = FALSE;

  device->fd = open ("/dev/dri/card0", O_RDWR);
  if (device->fd < 0)
    {
      g_warning ("Unable to open DRI device");
      goto out;
    }

  ret = TRUE;

 out:
  return ret;
}

static gboolean
device_find_crtc (Device *device)
{
  gboolean ret = FALSE;
  drmModeRes *resources;
  drmModeConnector *connector;
  drmModeEncoder *encoder;
  drmModeCrtc *crtc;
  int i;

  resources = drmModeGetResources (device->fd);

  /* Find the first active connector to display on. */
  for (i = 0; i < resources->count_connectors; i++)
    {
      connector = drmModeGetConnector (device->fd,
                                       resources->connectors[i]);
      if (connector == NULL)
        continue;

      if (connector->connection == DRM_MODE_CONNECTED &&
          connector->count_modes > 0)
        break;

      drmModeFreeConnector(connector);
    }

  if (i == resources->count_connectors)
    {
      g_warning ("Could not find an active connector");
      goto out;
    }

  /* Find an associated encoder for that connector. */
  encoder = drmModeGetEncoder (device->fd, connector->encoder_id);

  /* Now grab the CRTC for that encoder. */
  crtc = drmModeGetCrtc (device->fd, encoder->crtc_id);

  device->resources = resources;
  device->connector = connector;
  device->crtc = crtc;

  ret = TRUE;

 out:
  return ret;
}

static gboolean
device_show_buffer (Device *device,
                    int buffer_id,
                    int x,
                    int y)
{
  gboolean ret = FALSE;

  if (drmModeSetCrtc (device->fd,
                      device->crtc->crtc_id,
                      buffer_id,
                      x, y,
                      &device->connector->connector_id, 1,
                      &device->crtc->mode) != 0)
    {
      g_warning ("Could not set CRTC to display buffer %u: %m", buffer_id);
      goto out;
    }

  ret = TRUE;

 out:
  return ret;
}

static void
device_free (Device *device)
{
  if (device->resources != NULL)
    drmModeFreeResources (device->resources);

  if (device->connector != NULL)
    drmModeFreeConnector (device->connector);

  if (device->crtc != NULL)
    drmModeFreeCrtc (device->crtc);

  if (device->fd > 0)
    close (device->fd);
}

static gboolean
buffer_new (Buffer *buffer)
{
  struct drm_mode_create_dumb create_dumb_buffer_request;
  gboolean ret = FALSE;

  memset (&create_dumb_buffer_request, 0, sizeof (struct drm_mode_create_dumb));

  create_dumb_buffer_request.width = buffer->width;
  create_dumb_buffer_request.height = buffer->height;
  create_dumb_buffer_request.bpp = 32;
  create_dumb_buffer_request.flags = 0;

  if (drmIoctl (buffer->device->fd, DRM_IOCTL_MODE_CREATE_DUMB,
                &create_dumb_buffer_request) < 0)
    {
      g_warning ("Could not allocate frame buffer %m");
      goto out;
    }

  buffer->size = create_dumb_buffer_request.size;
  buffer->handle = create_dumb_buffer_request.handle;
  buffer->stride = create_dumb_buffer_request.pitch;

  /* Create the frame buffer from the card buffer object. */
  if (drmModeAddFB (buffer->device->fd,
                    buffer->width,
                    buffer->height,
                    24, /* depth */
                    32, /* bpp */
                    buffer->stride,
                    buffer->handle,
                    &buffer->id) != 0)
    {
      g_warning ("Could not set up frame buffer %m");
      goto out;
    }

  ret = TRUE;

 out:
  return ret;
}

static gboolean
buffer_free (Buffer *buffer)
{
  struct drm_mode_destroy_dumb destroy_dumb_buffer_request;
  gboolean ret = FALSE;

  /* Unmap the buffer. */
  if (buffer->pixels != NULL)
    munmap (buffer->pixels, buffer->size);

  /* Destroy the framebuffer. */
  if (buffer->id != 0)
    drmModeRmFB (buffer->device->fd, buffer->id);

  /* Destroy the buffer object on the card. */

  if (buffer->handle != 0)
    {
      memset (&destroy_dumb_buffer_request, 0, sizeof (struct drm_mode_map_dumb));
      destroy_dumb_buffer_request.handle = buffer->handle;

      if (drmIoctl (buffer->device->fd,
                    DRM_IOCTL_MODE_DESTROY_DUMB,
                    &destroy_dumb_buffer_request) < 0)
        {
          g_warning ("Could not destroy buffer %m");
          goto out;
        }
    }

  ret = TRUE;

 out:
  return ret;
}

static gboolean
buffer_map (Buffer *buffer)
{
  struct drm_mode_map_dumb map_dumb_buffer_request;
  gboolean ret = FALSE;

  memset (&map_dumb_buffer_request, 0, sizeof (struct drm_mode_map_dumb));
  map_dumb_buffer_request.handle = buffer->handle;
  if (drmIoctl (buffer->device->fd,
                DRM_IOCTL_MODE_MAP_DUMB,
                &map_dumb_buffer_request) < 0)
    {
      g_warning ("Could not map buffer %m");
      goto out;
    }

  buffer->pixels = mmap (0, buffer->size,
                         PROT_READ | PROT_WRITE, MAP_SHARED,
                         buffer->device->fd, map_dumb_buffer_request.offset);

  if (buffer->pixels == MAP_FAILED)
    {
      g_warning ("Could not mmap buffer %m");
      goto out;
    }

  ret = TRUE;

 out:
  return ret;
}

static void
draw_on_buffer (Buffer *buffer)
{
  cairo_surface_t *surface, *craig;
  cairo_t *cr;
  RsvgHandle *tiger_handle;
  GError *error = NULL;

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

  g_type_init ();
  tiger_handle = rsvg_handle_new_from_file ("tiger.svg", &error);
  if (error != NULL)
    {
      g_warning ("Error loading tiger.svg: %s", error->message);
      g_error_free (error);
    }
  else
    {
      cairo_translate (cr, 500, 200);
      rsvg_handle_render_cairo (tiger_handle, cr);
      g_object_unref (tiger_handle);
    }

  cairo_destroy (cr);
  cairo_surface_destroy (surface);
}

int
main (int argc, char **argv)
{
  Buffer buffer;
  Device device;
  int ret = 1;

  memset (&buffer, 0, sizeof (Buffer));
  memset (&device, 0, sizeof (Device));

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

  /* draw! */
  draw_on_buffer (&buffer);

  if (!device_show_buffer (&device, buffer.id, 0, 0))
    {
      g_warning ("Could not show our buffer");
      goto out;
    }

  sleep (5);

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

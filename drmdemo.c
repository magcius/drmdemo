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

int
main (int argc, char **argv)
{
  int i;
  int fd;
  uint32_t buffer_id;
  drmModeRes *resources = NULL;
  drmModeConnector *connector = NULL;
  drmModeEncoder *encoder = NULL;
  drmModeCrtc *crtc = NULL;
  struct drm_mode_create_dumb create_dumb_buffer_request;
  struct drm_mode_destroy_dumb destroy_dumb_buffer_request;
  struct drm_mode_map_dumb map_dumb_buffer_request;
  unsigned char *mapped_buffer;
  int ret = 1;

  fd = open ("/dev/dri/card0", O_RDWR);
  if (fd < 0)
    {
      g_warning ("Unable to open DRI device");
      goto out;
    }

  resources = drmModeGetResources (fd);

  /* Find the first active connector to display on. */
  for (i = 0; i < resources->count_connectors; i++)
    {
      connector = drmModeGetConnector (fd,
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

  /* Find the associated encoder for that connector. */
  encoder = drmModeGetEncoder (fd, connector->encoder_id);

  /* Now grab the CRTC for that encoder. */
  crtc = drmModeGetCrtc (fd, encoder->crtc_id);

  /* OK, we found a CRTC. Now, allocate a buffer object on the card. */
  memset (&create_dumb_buffer_request, 0, sizeof (struct drm_mode_create_dumb));

  /* Use the current resolution of the card. */
  create_dumb_buffer_request.width = crtc->mode.hdisplay;
  create_dumb_buffer_request.height = crtc->mode.vdisplay;
  create_dumb_buffer_request.bpp = 32;
  create_dumb_buffer_request.flags = 0;

  if (drmIoctl (fd, DRM_IOCTL_MODE_CREATE_DUMB,
                &create_dumb_buffer_request) < 0)
    {
      g_warning ("Could not allocate frame buffer %m");
      goto out;
    }

  /* Create the frame buffer from the card buffer object. */
  if (drmModeAddFB (fd,
                    create_dumb_buffer_request.width,
                    create_dumb_buffer_request.height,
                    24, /* depth */
                    create_dumb_buffer_request.bpp,
                    create_dumb_buffer_request.pitch,
                    create_dumb_buffer_request.handle,
                    &buffer_id) != 0)
    {
      g_warning ("Could not set up frame buffer %m");
      goto out;
    }

  memset (&map_dumb_buffer_request, 0, sizeof (struct drm_mode_map_dumb));
  map_dumb_buffer_request.handle = create_dumb_buffer_request.handle;
  if (drmIoctl (fd, DRM_IOCTL_MODE_MAP_DUMB,
                &map_dumb_buffer_request) < 0)
    {
      g_warning ("Could not map buffer %m");
      goto destroy_buffer;
    }

  mapped_buffer = mmap (0, create_dumb_buffer_request.size,
                        PROT_READ | PROT_WRITE, MAP_SHARED,
                        fd, map_dumb_buffer_request.offset);

  if (mapped_buffer == MAP_FAILED)
    {
      g_warning ("Could not mmap buffer %m");
      goto rm_fb;
    }

  /* draw! */
  {
    cairo_surface_t *surface, *craig;
    cairo_t *cr;
    RsvgHandle *tiger_handle;
    GError *error = NULL;

    surface = cairo_image_surface_create_for_data (mapped_buffer,
                                                   CAIRO_FORMAT_ARGB32,
                                                   create_dumb_buffer_request.width,
                                                   create_dumb_buffer_request.height,
                                                   create_dumb_buffer_request.pitch);
    cr = cairo_create (surface);

    cairo_set_source_rgb (cr, 1, 1, 1);
    cairo_paint (cr);

    cairo_set_source_rgb (cr, 1, 0, 0);
    cairo_rectangle (cr, 75, 75, 681, 800);
    cairo_fill (cr);

    craig = cairo_image_surface_create_from_png ("craig.png");
    cairo_set_source_surface (cr, craig, 0, 0);
    cairo_rectangle (cr, 50, 50, 681, 800);
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

  /* Set the CRTC to output our buffer for five seconds. */
  if (drmModeSetCrtc (fd, crtc->crtc_id, buffer_id,
                      0, 0,
                      &connector->connector_id, 1,
                      &crtc->mode) != 0)
    {
      g_warning ("Could not set CRTC to display our buffer %m");
      goto munmap;
    }

  sleep (5);

  /* Set the CRTC back to the buffer it was displaying before. */
  if (drmModeSetCrtc (fd,
                      crtc->crtc_id,
                      crtc->buffer_id,
                      crtc->x,
                      crtc->y,
                      &connector->connector_id, 1,
                      &crtc->mode) < 0)
    {
      g_warning ("Could not set CRTC to display previous buffer %m");
      goto munmap;
    }

  /* Do the necessary cleanup. */
  ret = 0;

 munmap:
  /* Unmap the buffer. */
  munmap (mapped_buffer, create_dumb_buffer_request.size);

 rm_fb:
  /* Destroy the framebuffer. */
  drmModeRmFB (fd, buffer_id);

 destroy_buffer:
  /* Destroy the buffer object on the card. */
  memset (&destroy_dumb_buffer_request, 0, sizeof (struct drm_mode_map_dumb));
  destroy_dumb_buffer_request.handle = create_dumb_buffer_request.handle;

  if (drmIoctl (fd,
                DRM_IOCTL_MODE_DESTROY_DUMB,
                &destroy_dumb_buffer_request) < 0)
    {
      g_warning ("Could not destroy buffer %m");
    }

 out:

  if (crtc != NULL)
    drmModeFreeCrtc (crtc);

  if (encoder != NULL)
    drmModeFreeEncoder (encoder);

  if (connector != NULL)
    drmModeFreeConnector (connector);

  if (resources != NULL)
    drmModeFreeResources (resources);

  return ret;
}

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

int
main (int argc, char **argv)
{
  int i;
  int fd;
  uint32_t buffer_id;
  drmModeRes *resources;
  drmModeConnector *connector;
  drmModeEncoder *encoder;
  drmModeCrtc *crtc;
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

  /* Find an associated encoder for that connector. */
  for (i = 0; i < resources->count_encoders; i++)
    {
      encoder = drmModeGetEncoder (fd, resources->encoders[i]);

      if (encoder == NULL)
        continue;

      if (encoder->encoder_id == connector->encoder_id)
        break;

      drmModeFreeEncoder (encoder);
    }

  if (i == resources->count_encoders)
    {
      g_warning ("Could not find associated encoder");
      goto out;
    }

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
    int x, y;
    int stride = create_dumb_buffer_request.pitch;

    /* Draw a red rectangle */
    for (x = 20; x < 100; x++)
      {
        for (y = 20; y < 100; y++)
          {
            void *ptr = &mapped_buffer[y * stride + x * 4];
            uint32_t *pixel = ptr;

            /* ARGB */
            *pixel = 0xFFFF0000;
          }
      }
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

 out:
  return ret;
}

/* -*- mode: c; c-basic-offset: 2 -*- */

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

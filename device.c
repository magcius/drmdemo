/* -*- mode: c; c-basic-offset: 2 -*- */

typedef struct {
  int fd;
  drmModeRes *resources;
  drmModeConnector *connector;
  drmModeCrtc *crtc;
} Device;

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

  drmVersionPtr version = drmGetVersion (device->fd);
  g_print ("Driver name: %s\n", version->name);
  drmFreeVersion (version);

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
device_page_flip (Device *device,
                  int buffer_id,
                  void *data)
{
  drmModePageFlip (device->fd,
                   device->crtc->crtc_id,
                   buffer_id, DRM_MODE_PAGE_FLIP_EVENT, data);
}

static void
device_cursor (Device *device,
               int bo_handle, int x, int y, int w, int h)
{
  struct drm_mode_cursor arg = {};
  arg.flags = DRM_MODE_CURSOR_BO | DRM_MODE_CURSOR_MOVE;
  arg.crtc_id = device->crtc->crtc_id;
  arg.x = x;
  arg.y = y;
  arg.width = w;
  arg.height = h;
  arg.handle = bo_handle;
  drmIoctl (device->fd, DRM_IOCTL_MODE_CURSOR, &arg);
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

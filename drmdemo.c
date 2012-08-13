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
  drmModeRes *resources;
  drmModeConnector *connector;
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

 out:
  ret = 0;

  return ret;
}

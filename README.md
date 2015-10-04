# drmdemo

Coding against the Direct Rendering Manager

## Prerequisites
### Debian-likes
    apt-get install build-essential pkg-config libdrm-dev libcairo2-dev libglib2.0-dev librsvg2-dev
    

## Troubleshooting
    ** (process:14023): WARNING **: Unable to open DRI device
    (process:14023): GLib-GObject-CRITICAL **: g_object_unref: assertion 'G_IS_OBJECT (object)' failed

Either `/dev/dri/card0` is not available or you need to switch VTs to somewhere X is not running, e.g. Ctrl-Alt-F2.

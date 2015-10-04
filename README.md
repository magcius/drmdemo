# drmdemo

Coding against the Direct Rendering Manager

## Prerequisites
### Debian-likes
    apt-get install build-essential pkg-config libdrm-dev libcairo2-dev libglib2.0-dev librsvg2-dev
    

## Troubleshooting
    ** (process:14023): WARNING **: Unable to open DRI device
    (process:14023): GLib-GObject-CRITICAL **: g_object_unref: assertion 'G_IS_OBJECT (object)' failed

You need to run ./drmdemo as root - strace will show `open("/dev/dri/card0", O_RDWR)          = -1 EACCES (Permission denied)` 

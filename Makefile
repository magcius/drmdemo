
pkgs = glib-2.0 libdrm cairo librsvg-2.0
CFLAGS = $(shell pkg-config --cflags $(pkgs)) -g -O0 -Wall -Werror -Wformat-security
LDFLAGS = $(shell pkg-config --libs $(pkgs))

drmdemo: drmdemo.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

all: drmdemo

clean:
	rm drmdemo

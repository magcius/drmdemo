
CFLAGS = `pkg-config --cflags --libs glib-2.0 libdrm cairo librsvg-2.0` -Wall -Werror -Wformat-security

all: drmdemo

clean:
	rm drmdemo

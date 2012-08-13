
CFLAGS = `pkg-config --cflags --libs glib-2.0 libdrm cairo` -Wall -Werror -Wformat-security

all: drmdemo

clean:
	rm drmdemo

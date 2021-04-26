
FLAGS = -O3 -ffast-math

all: browser-to-websocket.so
# browser-to-websocket

browser-to-websocket: browser-to-websocket.c
	$(CC) $< $(shell pkg-config --cflags --libs jack libwebsockets) -lpthread $(FLAGS) -o $@

browser-to-websocket.so: browser-to-websocket.lib.c
	$(CC) $< $(shell pkg-config --cflags --libs jack) -shared -lpthread $(FLAGS) -o $@

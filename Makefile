
FLAGS = -O0 -g

all: browser-to-websocket

browser-to-websocket: browser-to-websocket.c
	$(CC) $< $(shell pkg-config --cflags --libs libwebsockets) $(FLAGS) -o $@

#!/usr/bin/env python3

import os
from struct import unpack
from tornado import web, websocket
from tornado.ioloop import IOLoop
from tornado.template import Loader

# local
from jacklib import jacklib
from jacklib.jacklib_helpers import translate_audio_port_buffer

bufferSize = 4096

class IndexHandler(web.RequestHandler):
    def get(self, path):
        path = (__file__).split(os.path.sep)[-1].replace(".py",".html")
        loader = Loader(os.path.dirname(__file__))
        self.write(loader.load(path).generate())

class ServerWebSocket(websocket.WebSocketHandler):
    def open(self):
        print("websocket open")
        self.set_nodelay(True)
        self.audiopackets = []
        self.client = jacklib.client_open("browser2jack", jacklib.JackNoStartServer, None)
        if not self.client:
            raise Exception("Failed to create JACK client")
        self.port = jacklib.port_register(self.client, "output", jacklib.JACK_DEFAULT_AUDIO_TYPE, jacklib.JackPortIsOutput, 0)
        jacklib.set_process_callback(self.client, self.audio_cb, None)
        jacklib.activate(self.client)

    def on_close(self):
        print("websocket close")
        if self.client:
            jacklib.deactivate(self.client)
            jacklib.client_close(self.client)
            self.client = None

    def on_message(self, message):
        buf = list(unpack(f'{bufferSize}f', message))
        buf.reverse()
        self.audiopackets = buf + self.audiopackets

    def audio_cb(self, nframes, arg):
        buf = translate_audio_port_buffer(jacklib.port_get_buffer(self.port, nframes))
        if len(self.audiopackets) == 0:
            for i in range(nframes):
                buf[i] = 0.0
        else:
            for i in range(nframes):
                buf[i] = self.audiopackets.pop()
        return 0

application = web.Application([
        (r"/(index.html)?$", IndexHandler),
        (r"/websocket/?$", ServerWebSocket),
        (r"/(.*)", web.StaticFileHandler, {"path": os.path.dirname(__file__)}),
    ], debug=True)

application.listen(8021, address="0.0.0.0")
IOLoop.instance().start()

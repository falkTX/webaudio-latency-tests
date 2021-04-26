#!/usr/bin/env python3

import os
from struct import unpack
from tornado import web, websocket
from tornado.ioloop import IOLoop
from tornado.template import Loader

from ctypes import c_bool, c_char_p, c_uint, cdll

class IndexHandler(web.RequestHandler):
    def get(self, path):
        path = (__file__).split(os.path.sep)[-1].replace(".py",".html")
        loader = Loader(os.path.dirname(__file__))
        self.write(loader.load(path).generate())

class ServerWebSocket(websocket.WebSocketHandler):
    def open(self):
        print("websocket open")
        self.set_nodelay(True)

        self.clib = cdll.LoadLibrary((__file__).replace(".py",".so"))
        self.clib.bbjack_init.argtypes = None
        self.clib.bbjack_init.restype = c_bool
        self.clib.bbjack_cleanup.argtypes = None
        self.clib.bbjack_cleanup.restype = None
        self.clib.bbjack_audiodata.argtypes = [c_char_p, c_uint]
        self.clib.bbjack_audiodata.restype = c_bool
        self.clib.bbjack_init()

    def on_close(self):
        print("websocket close")
        self.clib.bbjack_cleanup()

    def on_message(self, message):
        if not self.clib.bbjack_audiodata(message, len(message)):
            IOLoop.instance().stop()

application = web.Application([
        (r"/(index.html)?$", IndexHandler),
        (r"/websocket/?$", ServerWebSocket),
        (r"/(.*)", web.StaticFileHandler, {"path": os.path.dirname(__file__)}),
    ], debug=True)

application.listen(8021, address="0.0.0.0")
print("WebServer starting on http://localhost:8021/")
IOLoop.instance().start()

# -*- coding: utf-8 -*-
# pylint: disable=C0301,W0105,W0401,W0614

from tarantool.connection import Connection
from tarantool.const import *
from tarantool.error import *

def connect(host="localhost", port=33013):
    return Connection(host, port,
                      socket_timeout=SOCKET_TIMEOUT,
                      reconnect_max_attempts=RECONNECT_MAX_ATTEMPTS,
                      reconnect_delay=RECONNECT_DELAY,
                      connect_now=True)

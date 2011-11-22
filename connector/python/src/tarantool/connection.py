# -*- coding: utf-8 -*-
# pylint: disable=C0301,W0105,W0401,W0614
'''
This module provides low-level API for Tarantool
'''

import socket
import sys
import time

from tarantool.response import Response
from tarantool.request import (
                    Request,
                    RequestCall,
                    RequestDelete,
                    RequestInsert,
                    RequestSelect,
                    RequestUpdate)
from tarantool.space import Space
from tarantool.const import *
from tarantool.error import *



class Connection(object):
    '''\
    Represents low-level interface to the Tarantool server.
    This class can be used directly or using object-oriented wrappers.
    '''

    def __init__(self, host, port,
                 socket_timeout=SOCKET_TIMEOUT,
                 reconnect_max_attempts=RECONNECT_MAX_ATTEMPTS,
                 reconnect_delay=RECONNECT_DELAY,
                 connect_now=True):
        '''\
        Initialize an connection to the server.

        :param str host: Server hostname or IP-address
        :param int port: Server port
        :param bool connect_now: if True (default) than __init__() actually creates network connection.
                             if False than you have to call connect() manualy.
        '''
        self.host = host
        self.port = port
        self.socket_timeout = socket_timeout
        self.reconnect_delay = reconnect_delay
        self.reconnect_max_attempts = reconnect_max_attempts
        self._socket = None
        if connect_now:
            self.connect()


    def connect(self):
        '''\
        Create connection to the host and port specified in __init__().
        Usually there is no need to call this method directly,
        since it is called when you create an `Connection` instance.

        :raise: `NetworkError`
        '''

        try:
            # If old socket already exists - close it and re-create
            if self._socket:
                self._socket.close()
            self._socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self._socket.setsockopt(socket.SOL_TCP, socket.TCP_NODELAY, 1)
            self._socket.connect((self.host, self.port))
            # It is important to set socket timeout *after* connection.
            # Otherwise the timeout exception will rised, even if the server does not listen
            self._socket.settimeout(self.socket_timeout)
        except socket.error as e:
            raise NetworkError(e)


    def _send_request_wo_reconnect(self, request, field_types=None):
        '''\
        :rtype: `Response` instance

        :raise: NetworkError
        '''
        assert isinstance(request, Request)

        # Repeat request in a loop if the server returns completion_status == 1 (try again)
        for attempt in xrange(RETRY_MAX_ATTEMPTS):    # pylint: disable=W0612
            try:
                self._socket.sendall(bytes(request))
                response = Response(self._socket, field_types)
            except socket.error as e:
                raise NetworkError(e)

            if response.completion_status != 1:
                return response
            warn(response.return_message, RetryWarning)

        # Raise an error if the maximum number of attempts have been made
        raise DatabaseError(response.return_code, response.return_message)


    def _send_request(self, request, field_types=None):
        '''\
        Send the request to the server through the socket.
        Return an instance of `Response` class.

        :param request: object representing a request
        :type request: `Request` instance

        :rtype: `Response` instance
        '''
        assert isinstance(request, Request)

        connected = True
        attempt = 1
        while True:
            try:
                if not connected:
                    time.sleep(self.reconnect_delay)
                    self.connect()
                    connected = True
                    warn("Successfully reconnected", NetworkWarning)
                response = self._send_request_wo_reconnect(request, field_types)
                break
            except NetworkError as e:
                if attempt > self.reconnect_max_attempts:
                    raise
                warn("%s : Reconnect attempt %d of %d"%(e.message, attempt, self.reconnect_max_attempts), NetworkWarning)
                attempt += 1
                connected = False

        return response

    def call(self, func_name, *args, **kwargs):
        '''\
        Execute CALL request. Call stored Lua function.

        :param func_name: stored Lua function name
        :type func_name: str
        :param args: list of function arguments
        :type args: list or tuple
        :param return_tuple: True indicates that it is required to return the inserted tuple back
        :type return_tuple: bool

        :rtype: `Response` instance
        '''
        assert isinstance(func_name, str)
        assert len(args) != 0

        # This allows to use a tuple or list as an argument
        if isinstance(args[0], (list, tuple)):
            args = args[0]

        # Check if 'field_types' keyword argument is passed
        field_types = kwargs.get("field_types", None)

        request = RequestCall(func_name, args, return_tuple=True)
        response = self._send_request(request, field_types=field_types)
        return response


    def insert(self, space_no, values, return_tuple=False, field_types=None):
        '''\
        Execute INSERT request.
        Insert single record into a space `space_no`.

        :param int space_no: space id to insert a record
        :type space_no: int
        :param values: record to be inserted. The tuple must contain only scalar (integer or strings) values
        :type values: tuple
        :param return_tuple: True indicates that it is required to return the inserted tuple back
        :type return_tuple: bool

        :rtype: `Response` instance
        '''
        assert isinstance(values, tuple)

        request = RequestInsert(space_no, values, return_tuple)
        return self._send_request(request, field_types=field_types)


    def delete(self, space_no, key, return_tuple=False, field_types=None):
        '''\
        Execute DELETE request.
        Delete single record identified by `key` (using primary index).

        :param space_no: space id to delete a record
        :type space_no: int
        :param key: key that identifies a record
        :type key: int or str
        :param return_tuple: indicates that it is required to return the deleted tuple back
        :type return_tuple: bool

        :rtype: `Response` instance
        '''
        assert isinstance(key, (int, basestring))

        request = RequestDelete(space_no, key, return_tuple)
        return self._send_request(request, field_types=field_types)


    def update(self, space_no, key, op_list, return_tuple=False, field_types=None):
        '''\
        Execute UPDATE request.
        Update single record identified by `key` (using primary index).

        List of operations allows to update individual fields.

        :param space_no: space id to update a record
        :type space_no: int
        :param key: key that identifies a record
        :type key: int or str
        :param op_list: list of operations. Each operation is tuple of three values
        :type op_list: a list of the form [(field_1, symbol_1, arg_1), (field_2, symbol_2, arg_2),...]
        :param return_tuple: indicates that it is required to return the updated tuple back
        :type return_tuple: bool

        :rtype: `Response` instance
        '''
        assert isinstance(key, (int, basestring))

        request = RequestUpdate(space_no, key, op_list, return_tuple)
        return self._send_request(request, field_types=field_types)


    def ping(self):
        '''\
        Execute PING request.
        Send empty request and receive empty response from server.

        :return: response time in seconds
        :rtype: float
        '''
        t0 = time.time()
        self._socket.sendall(struct_LLL.pack(0xff00, 0, 0))
        request_type, body_length, request_id = struct_LLL.unpack(self._socket.recv(12)) # pylint: disable=W0612
        t1 = time.time()
        assert request_type == 0xff00
        assert body_length == 0
        return t1 - t0


    def _select(self, space_no, index_no, values, offset=0, limit=0xffffffff, field_types=None):
        '''\
        Low level version of select() method.

        :param space_no: space id to select data
        :type space_no: int
        :param index_no: index id to use
        :type index_no: int
        :param values: list of values to search over the index
        :type values: list of tuples
        :param offset: offset in the resulting tuple set
        :type offset: int
        :param limit: limits the total number of returned tuples
        :type limit: int

        :rtype: `Response` instance
        '''

        # 'values' argument must be a list of tuples
        assert isinstance(values, (list, tuple))
        assert len(values) != 0
        assert isinstance(values[0], (list, tuple))

        request = RequestSelect(space_no, index_no, values, offset, limit)
        response = self._send_request(request, field_types=field_types)
        return response


    def select(self, space_no, index_no, values, offset=0, limit=0xffffffff, field_types=None):
        '''\
        Execute SELECT request.
        Select and retrieve data from the database.

        :param space_no: specifies which space to query
        :type space_no: int
        :param index_no: specifies which index to use
        :type index_no: int
        :param values: list of values to search over the index
        :type values: list of tuples
        :param offset: offset in the resulting tuple set
        :type offset: int
        :param limit: limits the total number of returned tuples
        :type limit: int

        :rtype: `Response` instance

        Select one single record (from space=0 and using index=0)
        >>> select(0, 0, 1)

        Select several records using single-valued index
        >>> select(0, 0, [1, 2, 3])
        >>> select(0, 0, [(1,), (2,), (3,)]) # the same as above

        Select serveral records using composite index
        >>> select(0, 1, [(1,'2'), (2,'3'), (3,'4')])

        Select single record using composite index
        >>> select(0, 1, [(1,'2')])
        This is incorrect
        >>> select(0, 1, (1,'2'))
        '''

        # Perform smart type cheching (scalar / list of scalars / list of tuples)
        if isinstance(values, (int, basestring)): # scalar
            # This request is looking for one single record
            values = [(values, )]
        elif isinstance(values, (list, tuple, set, frozenset)):
            assert len(values) > 0
            if isinstance(values[0], (int, basestring)): # list of scalars
                # This request is looking for several records using single-valued index
                # Ex: select(space_no, index_no, [1, 2, 3])
                # Transform a list of scalar values to a list of tuples
                values = [(v, ) for v in values]
            elif isinstance(values[0], (list, tuple)): # list of tuples
                # This request is looking for serveral records using composite index
                pass
            else:
                raise ValueError("Invalid value type, expected one of scalar (int or str) / list of scalars / list of tuples ")

        return self._select(space_no, index_no, values, offset, limit, field_types=field_types)


    def space(self, space_no, field_types=None):
        '''\
        Create `Space` instance for particular space

        `Space` instance encapsulates the identifier of the space and provides more convenient syntax
        for accessing the database space.

        :param space_no: identifier of the space
        :type space_no: int

        :rtype: `Space` instance
        '''
        return Space(self, space_no, field_types)


# -*- coding: utf-8 -*-
# pylint: disable=C0301,W0105,W0401,W0614

import ctypes
import socket
import struct
import sys

from tarantool.const import *
from tarantool.error import *



if sys.version_info < (2, 6):
    bytes = str    # pylint: disable=W0622

class field(bytes):
    '''\
    Represents a single element of the Tarantool's tuple
    '''
    def __new__(cls, value):
        '''\
        Create new instance of Tarantool field (single tuple element)
        '''
        # Since parent class is immutable, we should override __new__, not __init__

        if isinstance(value, unicode):
            return super(field, cls).__new__(cls, value.encode("utf-8", "replace"))
        if sys.version_info.major < 3 and isinstance(value, str):
            return super(field, cls).__new__(cls, value)
        if isinstance(value, (bytearray, bytes)):
            return super(field, cls).__new__(cls, value)
        if isinstance(value, int):
            if value <= 0xFFFFFFFF:
                # 32 bit integer
                return super(field, cls).__new__(cls, struct_L.pack(value))
            else:
                # 64 bit integer
                return super(field, cls).__new__(cls, struct_Q.pack(value))
        # NOTE: It is posible to implement float
        raise TypeError("Unsupported argument type '%s'"%(type(value).__name__))


    def __int__(self):
        '''\
        Cast filed to int
        '''
        if len(self) == 4:
            return struct_L.unpack(self)[0]
        elif len(self) == 8:
            return struct_Q.unpack(self)[0]
        else:
            raise ValueError("Unable to cast field to int: length must be 4 or 8 bytes, field length is %d"%len(self))


    if sys.version_info.major > 2:
        def __str__(self):
            '''\
            Cast filed to str
            '''
            return self.decode("utf-8", "replace")
    else:
        def __unicode__(self):
            '''\
            Cast filed to unicode
            '''
            return self.decode("utf-8", "replace")



class Response(list):
    '''\
    Represents a single response from the server in compliance with the Tarantool protocol.
    Responsible for data encapsulation (i.e. received list of tuples) and parses binary
    packet received from the server.
    '''

    def __init__(self, _socket, field_types=None):
        '''\
        Create an instance of `Response` using data received from the server.

        __init__() itself reads data from the socket, parses response body and
        sets appropriate instance attributes.

        :params _socket: socket connected to the server
        :type _socket: instance of socket.socket class (from stdlib)
        '''
        # This is not necessary, because underlying list data structures are created in the __new__(). But let it be.
        super(Response, self).__init__()

        self._body_length = None
        self._request_id = None
        self._request_type = None
        self._completion_status = None
        self._return_code = None
        self._return_message = None
        self._rowcount = None
        self.field_types = field_types

        # Read response header
        buff = ctypes.create_string_buffer(16)
        nbytes = _socket.recv_into(buff, 16, )

        # Immediately raises an exception if the data cannot be read
        if nbytes != 16:
            raise socket.error(socket.errno.ECONNABORTED, "Software caused connection abort")

        # Unpack header (including <return_code> attribute)
        self._request_type, self._body_length, self._request_id, self._return_code = struct_LLLL.unpack(buff)

        # Separate return_code and completion_code
        self._completion_status = self._return_code & 0x00ff
        self._return_code = self._return_code >> 8

        # Unpack body if there is one (i.e. not PING)
        if self._body_length != 0:

            # In the protocol description <body_length> includes 4 bytes of <return_code>
            self._body_length -= 4

            # Read response body
            buff = ctypes.create_string_buffer(self._body_length)
            nbytes = _socket.recv_into(buff)

            # Immediately raises an exception if the data cannot be read
            if nbytes != self._body_length:
                raise socket.error(socket.errno.ECONNABORTED, "Software caused connection abort")

            if self._return_code == 0:
                # If no errors, unpack response body
                self._unpack_body(buff)
            else:
                # In case of error unpack body as error message
                self._unpack_message(buff)
                if self._completion_status == 2:
                    raise DatabaseError(self._return_code, self._return_message)


    def _unpack_message(self, buff):
        '''\
        Extract error message from response body
        Called when return_code! = 0.

        :param buff: buffer containing request body
        :type byff: ctypes buffer
        :return: error message
        :rtype:  str
        '''

        self._return_message = unicode(buff.value, "utf8", "replace")


    @staticmethod
    def _unpack_int_base128(varint, offset):
        """Implement Perl unpack's 'w' option, aka base 128 decoding."""
        res = ord(varint[offset])
        if ord(varint[offset]) >= 0x80:
            offset += 1
            res = ((res - 0x80) << 7) + ord(varint[offset])
            if ord(varint[offset]) >= 0x80:
                offset += 1
                res = ((res - 0x80) << 7) + ord(varint[offset])
                if ord(varint[offset]) >= 0x80:
                    offset += 1
                    res = ((res - 0x80) << 7) + ord(varint[offset])
                    if ord(varint[offset]) >= 0x80:
                        offset += 1
                        res = ((res - 0x80) << 7) + ord(varint[offset])
        return res, offset + 1


    def _unpack_tuple(self, buff):
        '''\
        Unpacks the tuple from byte buffer
        <tuple> ::= <cardinality><field>+

        :param buff: byte array of the form <cardinality><field>+
        :type buff: ctypes buffer or bytes

        :return: tuple of unpacked values
        :rtype: tuple
        '''

        cardinality = struct_L.unpack_from(buff)[0]
        _tuple = ['']*cardinality
        offset = 4    # The first 4 bytes in the response body is the <count> we have already read
        for i in xrange(cardinality):
            field_size, offset = self._unpack_int_base128(buff, offset)
            field_data = struct.unpack_from("<%ds"%field_size, buff, offset)[0]
            _tuple[i] = field(field_data)
            offset += field_size

        return tuple(_tuple)


    def _unpack_body(self, buff):
        '''\
        Parse the response body.
        After body unpacking its data available as python list of tuples

        For each request type the response body has the same format:
        <insert_response_body> ::= <count> | <count><fq_tuple>
        <update_response_body> ::= <count> | <count><fq_tuple>
        <delete_response_body> ::= <count> | <count><fq_tuple>
        <select_response_body> ::= <count><fq_tuple>*
        <call_response_body>   ::= <count><fq_tuple>

        :param buff: buffer containing request body
        :type byff: ctypes buffer
        '''

        # Unpack <count> (first 4 bytes) - how many records returned
        self._rowcount = struct_L.unpack_from(buff)[0]

        # If the response body contains only <count> - there is no tuples to unpack
        if self._body_length == 4:
            return

        # Parse response tuples (<fq_tuple>)
        if self._rowcount > 0:
            offset = 4    # The first 4 bytes in the response body is the <count> we have already read
            while offset < self._body_length:
                '''
                # In resonse tuples have the form <size><tuple> (<fq_tuple> ::= <size><tuple>).
                # Attribute <size> takes into account only size of tuple's <field> payload,
                # but does not include 4-byte of <cardinality> field.
                # Therefore the actual size of the <tuple> is greater to 4 bytes.
                '''
                tuple_size = struct.unpack_from("<L", buff, offset)[0] + 4
                tuple_data = struct.unpack_from("<%ds"%(tuple_size), buff, offset+4)[0]
                tuple_value = self._unpack_tuple(tuple_data)
                if self.field_types:
                    self.append(self._cast_tuple(tuple_value))
                else:
                    self.append(tuple_value)

                offset = offset + tuple_size + 4    # This '4' is a size of <size> attribute


    @property
    def completion_status(self):
        return self._completion_status

    @property
    def rowcount(self):
        return self._rowcount

    @property
    def return_code(self):
        return self._return_code

    @property
    def return_message(self):
        return self._return_message


    @staticmethod
    def _cast_field(cast_to, value):
        '''\
        Convert field type from raw bytes to native python type

        :param cast_to: native python type to cast to
        :type cast_to: a type object (one of bytes, int, unicode (str for py3k))
        :param value: raw value from the database
        :type value: bytes

        :return: converted value
        :rtype: value of native python type (one of bytes, int, unicode (str for py3k))
        '''

        if cast_to in (int, unicode):
            return cast_to(value)
        elif cast_to in (any, bytes):
            return value
        else:
            raise TypeError("Invalid field type %s"%(cast_to))


    def _cast_tuple(self, values):
        '''\
        Convert values of the tuple from raw bytes to native python types

        :param values: tuple of the raw database values
        :type value: tuple of bytes

        :return: converted tuple value
        :rtype: value of native python types (bytes, int, unicode (or str for py3k))
        '''
        result = []
        for i in xrange(len(values)):
            if i < len(self.field_types):
                result.append(self._cast_field(self.field_types[i], values[i]))
            else:
                result.append(self._cast_field(self.field_types[-1], values[i]))

        return tuple(result)

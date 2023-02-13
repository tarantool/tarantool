"""
GDB extension for Tarantool post-mortem analysis.
To use, just put 'source <path-to-this-file>' in gdb.
"""

import gdb.printing
import gdb.types
import argparse
import base64
import logging
import struct
import itertools
import re
import sys
from collections import namedtuple

slice = itertools.islice
if sys.version_info[0] == 2:
    filter = itertools.ifilter
    zip = itertools.izip
elif sys.version_info[0] == 3:
    unicode = str

logger = logging.getLogger('gdb.tarantool')
logger.setLevel(logging.WARNING)

def dump_type(type):
    return 'tag={} code={}'.format(type.tag, type.code)

def nth(iterable, n, default=None):
    """Returns the nth item or a default value."""
    return next(itertools.islice(iterable, n, None), default)

def equal_types(type1, type2):
    return type1.code == type2.code and type1.tag == type2.tag

# couple of functions below don't raise an exception and should be used
# when type/value may or may not exist

def find_type(type):
    try:
        return gdb.lookup_type(type)
    except Exception as exc:
        return None

def find_value(sym):
    try:
        return gdb.parse_and_eval(sym)
    except Exception as exc:
        return None

def cast_ptr(dest_type, ptr, offset):
    dest_ptr = ptr.cast(gdb.lookup_type('char').pointer()) + offset
    return dest_ptr.cast(dest_type.pointer())

def container_of(ptr, container_type, field):
    return cast_ptr(container_type, ptr, -(container_type[field].bitpos // 8)).dereference()

INT32_MAX = 2**31 - 1
INT32_MIN = -2**31
INT_MAX = INT32_MAX
INT_MIN = INT32_MIN

pp = gdb.printing.RegexpCollectionPrettyPrinter("tarantool")
gdb.printing.register_pretty_printer(gdb.current_objfile(), pp, True)


class InputStream(object):
    """
Helper class that implements stream reading from bytes buffer
Also it provides a few methods to read the basic primitives (considering byte order):
- signed/unsigned integers (of 1, 2, 4 and 8 bytes long)
- floating point numbers of single and double precision (float and double)
    """
    def __init__(self, data):
        self.__data = data.cast(gdb.lookup_type('uint8_t').pointer())
        self.__pos = 0

    @property
    def pos(self):
        return self.__pos

    def seek(self, offs):
        self.__pos += offs

    def read(self, size):
        buf = bytearray(size)
        for i in range(0, size):
            buf[i] = int(self.__data[self.__pos + i])
        self.__pos += size
        return buf

def make_stream_read_method(size, fmt):
    return lambda self, big_endian=True: \
        struct.unpack_from(('>' if big_endian else '<') + fmt, self.read(size))[0]

stream_read_methods = {
    'read_u8': (1, 'B'),
    'read_i8': (1, 'b'),
    'read_u16': (2, 'H'),
    'read_i16': (2, 'h'),
    'read_u32': (4, 'I'),
    'read_i32': (4, 'i'),
    'read_u64': (8, 'Q'),
    'read_i64': (8, 'q'),
    'read_float': (4, 'f'),
    'read_double': (8, 'd'),
}

for method in stream_read_methods:
    size, fmt = stream_read_methods[method]
    setattr(InputStream, method, make_stream_read_method(size, fmt))

class MsgPack(object):
    # base types
    MP_NIL = gdb.parse_and_eval('MP_NIL')
    MP_UINT = gdb.parse_and_eval('MP_UINT')
    MP_INT = gdb.parse_and_eval('MP_INT')
    MP_STR = gdb.parse_and_eval('MP_STR')
    MP_BIN = gdb.parse_and_eval('MP_BIN')
    MP_ARRAY = gdb.parse_and_eval('MP_ARRAY')
    MP_MAP = gdb.parse_and_eval('MP_MAP')
    MP_BOOL = gdb.parse_and_eval('MP_BOOL')
    MP_FLOAT = gdb.parse_and_eval('MP_FLOAT')
    MP_DOUBLE = gdb.parse_and_eval('MP_DOUBLE')
    MP_EXT = gdb.parse_and_eval('MP_EXT')

    # parser hints
    MP_HINT = gdb.parse_and_eval('MP_HINT')
    MP_HINT_STR_8 = gdb.parse_and_eval('MP_HINT_STR_8')
    MP_HINT_STR_16 = gdb.parse_and_eval('MP_HINT_STR_16')
    MP_HINT_STR_32 = gdb.parse_and_eval('MP_HINT_STR_32')
    MP_HINT_ARRAY_16 = gdb.parse_and_eval('MP_HINT_ARRAY_16')
    MP_HINT_ARRAY_32 = gdb.parse_and_eval('MP_HINT_ARRAY_32')
    MP_HINT_MAP_16 = gdb.parse_and_eval('MP_HINT_MAP_16')
    MP_HINT_MAP_32 = gdb.parse_and_eval('MP_HINT_MAP_32')
    MP_HINT_EXT_8 = gdb.parse_and_eval('MP_HINT_EXT_8')
    MP_HINT_EXT_16 = gdb.parse_and_eval('MP_HINT_EXT_16')
    MP_HINT_EXT_32 = gdb.parse_and_eval('MP_HINT_EXT_32')

    # lookup tables
    mp_type_hint = gdb.parse_and_eval('mp_type_hint')
    mp_parser_hint = gdb.parse_and_eval('mp_parser_hint')
    mp_char2escape = gdb.parse_and_eval('mp_char2escape')

    @classmethod
    def typeof(cls, data): # mp_typeof
        c = data.read_u8()
        data.seek(-1)
        return cls.mp_type_hint[c]

    @staticmethod
    def decode_nil(data): # mp_decode_nil
        c = data.read_u8()
        if c != 0xc0:
            raise gdb.GdbError("mp_decode_nil: unexpected data ({})".format(c))

    @staticmethod
    def decode_bool(data): # mp_decode_bool
        c = data.read_u8()
        if c == 0xc3:
            return True
        elif c == 0xc2:
            return False
        else:
            raise gdb.GdbError("mp_decode_bool: unexpected data ({})".format(c))

    @staticmethod
    def decode_uint(data): # mp_decode_uint
        c = data.read_u8()
        if c == 0xcc:
            return data.read_u8()
        elif c == 0xcd:
            return data.read_u16()
        elif c == 0xce:
            return data.read_u32()
        elif c == 0xcf:
            return data.read_u64()
        else:
            if c > 0x7f:
                raise gdb.GdbError("mp_decode_uint: unexpected data ({})".format(c))
            return c

    @staticmethod
    def decode_int(data): # mp_decode_int
        c = data.read_u8()
        if c == 0xd0:
            return data.read_i8()
        elif c == 0xd1:
            return data.read_i16()
        elif c == 0xd2:
            return data.read_i32()
        elif c == 0xd3:
            return data.read_i64()
        else:
            if c < 0xe0:
                raise gdb.GdbError("mp_decode_int: unexpected data ({})".format(c))
            data.seek(-1)
            return data.read_i8()

    @staticmethod
    def decode_strl(data): # mp_decode_strl
        c = data.read_u8()
        if c == 0xd9:
            return data.read_u8()
        elif c == 0xda:
            return data.read_u16()
        elif c == 0xdb:
            return data.read_u32()
        else:
            if c < 0xa0 or c > 0xbf:
                raise gdb.GdbError("mp_decode_strl: unexpected data ({})".format(c))
            return c & 0x1f

    @staticmethod
    def decode_binl(data): # mp_decode_binl
        c = data.read_u8()
        if c == 0xc4:
            return data.read_u8()
        elif c == 0xc5:
            return data.read_u16()
        elif c == 0xc6:
            return data.read_u32()
        else:
            raise gdb.GdbError("mp_decode_binl: unexpected data ({})".format(c))

    @staticmethod
    def decode_float(data): # mp_decode_float
        c = data.read_u8()
        if c != 0xca:
            raise gdb.GdbError("mp_decode_float: unexpected data ({})".format(c))
        return data.read_float()

    @staticmethod
    def decode_double(data): # mp_decode_double
        c = data.read_u8()
        if c != 0xcb:
            raise gdb.GdbError("mp_decode_double: unexpected data ({})".format(c))
        return data.read_double()

    @staticmethod
    def decode_array_slowpath(c, data): # mp_decode_array_slowpath
        if c & 0x1 == 0xdc & 0x1:
            return data.read_u16()
        elif c & 0x1 == 0xdd & 0x1:
            return data.read_u32()
        else:
            raise gdb.GdbError("mp_decode_array_slowpath: unexpected data ({})".format(c))

    @staticmethod
    def decode_array(data): # mp_decode_array
        c = data.read_u8();
        if not (c & 0x40):
            return c & 0xf
        return MsgPack.decode_array_slowpath(c, data)

    @staticmethod
    def decode_map(data): # mp_decode_map
        c = data.read_u8();
        if c == 0xde:
            return data.read_u16()
        elif c == 0xdf:
            return data.read_u32()
        else:
            if c < 0x80 or c > 0x8f:
                raise gdb.GdbError("mp_decode_map: unexpected data ({})".format(c))
            return c & 0xf

    @staticmethod
    def decode_extl(data): # mp_decode_extl
        c = data.read_u8()
        if c in [0xd4, 0xd5, 0xd6, 0xd7, 0xd8]:
            len = 1 << (c - 0xd4)
        elif c == 0xc7:
            len = data.read_u8()
        elif c == 0xc8:
            len = data.read_u16()
        elif c == 0xc9:
            len = data.read_u32()
        else:
            raise gdb.GdbError("mp_decode_extl: unexpected data ({})".format(c))
        type = data.read_u8()
        return len, type

    @classmethod
    def to_string_ext(cls, data, depth, ext_len, ext_type): # mp_snprint_ext_default
        data.seek(ext_len)
        return '(extension: type {}, len {})'.format(ext_type, ext_len)

    @classmethod
    def next_slowpath(cls, data, k): # mp_next_slowpath
        while k > 0:
            c = data.read_u8()
            l = cls.mp_parser_hint[c]
            if l >= 0:
                if l == 0 and k % 64 == 0:
                    while k > 8:
                        u = data.read_u64()
                        if u != c * 0x0101010101010101:
                            data.seek(-8)
                            break
                        k -= 8
                    k -= 1
                    continue

                data.seek(l)
                k -= 1
                continue

            elif l > cls.MP_HINT:
                k -= l
                k -= 1
                continue

            if l == cls.MP_HINT_STR_8:
                len = data.read_u8()
                data.seek(len)

            elif l == cls.MP_HINT_STR_16:
                len = data.read_u16()
                data.seek(len)

            elif l == cls.MP_HINT_STR_32:
                len = data.read_u32()
                data.seek(len)

            elif l == cls.MP_HINT_ARRAY_16:
                k += data.read_u16()

            elif l == cls.MP_HINT_ARRAY_32:
                k += data.read_u32()

            elif l == cls.MP_HINT_MAP_16:
                k += 2 * data.read_u16()

            elif l == cls.MP_HINT_MAP_32:
                k += 2 * data.read_u32()

            elif l == cls.MP_HINT_EXT_8:
                len = data.read_u8()
                data.read_u8()
                data.seek(len)

            elif l == cls.MP_HINT_EXT_16:
                len = data.read_u16()
                data.read_u8()
                data.seek(len)

            elif l == cls.MP_HINT_EXT_32:
                len = data.read_u32()
                data.read_u8()
                data.seek(len)

            else:
                raise gdb.GdbError("next_slowpath: unexpected data ({})".format(l))

    @classmethod
    def next(cls, data): # mp_next
        k = 1
        while k > 0:
            c = data.read_u8()
            l = cls.mp_parser_hint[c];
            if l >= 0:
                data.seek(l)
                k -= 1
                continue
            elif c == 0xd9:
                len = data.read_u8()
                data.seek(len)
                k -= 1
                continue
            elif l > cls.MP_HINT:
                k -= l
                k -= 1
                continue
            else:
                data.seek(-1)
                cls.next_slowpath(data, k)
                return

    def to_string_data(cls, data, depth):
        s = str()
        mp_type = cls.typeof(data)

        if mp_type == cls.MP_NIL:
            cls.decode_nil(data)
            s += 'null'

        elif mp_type == cls.MP_UINT:
            s += str(cls.decode_uint(data)) + 'U'

        elif mp_type == cls.MP_INT:
            s += str(cls.decode_int(data))

        elif mp_type == cls.MP_STR:
            len = cls.decode_strl(data)
            s += '"'
            s += unicode(data.read(len), 'utf-8')
            s += '"'

        elif mp_type == cls.MP_BIN:
            len = cls.decode_binl(data)
            s += '!!binary '
            s += base64.b64encode(str(data.read(len)))

        elif mp_type == cls.MP_ARRAY:
            s += '['
            if depth == 0:
                s += '...'
                cls.next(data)
            else:
                depth -= 1
                count = cls.decode_array(data)
                for i in range(0, count):
                    if i > 0:
                        s += ', '
                    s += cls.to_string_data(data, depth)
                depth += 1
            s += ']'

        elif mp_type == cls.MP_MAP:
            s += '{'
            if depth == 0:
                s += '...'
                cls.next(data)
            else:
                depth -= 1
                count = cls.decode_map(data)
                for i in range(0, count):
                    if i > 0:
                        s += ', '
                    s += cls.to_string_data(data, depth)
                    s += ': '
                    s += cls.to_string_data(data, depth)
                depth += 1
            s += '}'

        elif mp_type == cls.MP_BOOL:
            s += 'true' if cls.decode_bool(data) else 'false'

        elif mp_type == cls.MP_FLOAT:
            s += str(cls.decode_float(data))

        elif mp_type == cls.MP_DOUBLE:
            s += str(cls.decode_double(data))

        elif mp_type == cls.MP_EXT:
            ext_len, ext_type = cls.decode_extl(data)
            s += cls.to_string_ext(data, depth, ext_len, ext_type)

        else:
            raise gdb.GdbError("print_recursive: unexpected type ({})".format(mp_type))

        return s

    def __init__(self, val):
        self.val = val

    def to_string(self, max_depth=None, max_len=None):
        if max_depth is None:
            max_depth = -1
        s = self.to_string_data(InputStream(self.val), max_depth)
        return s if max_len is None else s[:max_len]

    def __str__(self):
        return self.to_string()


class TtMsgPack(MsgPack):
    # extension types
    MP_DECIMAL = find_value('MP_DECIMAL')
    MP_UUID = find_value('MP_UUID')
    MP_DATETIME = find_value('MP_DATETIME')
    MP_ERROR = find_value('MP_ERROR')
    MP_COMPRESSION = find_value('MP_COMPRESSION')
    MP_INTERVAL = find_value('MP_INTERVAL')

    UUID_PACKED_LEN = find_value('UUID_PACKED_LEN')

    MP_ERROR_STACK = find_value('MP_ERROR_STACK')
    MP_ERROR_MAX = find_value('MP_ERROR_MAX')
    mp_error_field_to_json_key = find_value('mp_error_field_to_json_key')

    @classmethod
    def decode_decimal(cls, data, len): # decimal_unpack
        pos = data.pos

        mp_type = cls.typeof(data)
        if mp_type == cls.MP_UINT:
            scale = cls.decode_uint(data)
        elif mp_type == cls.MP_INT:
            scale = cls.decode_int(data)
        else:
            return None, "decode_decimal: unexpected scale type ({})".format(mp_type)

        bcd = data.read(len - (data.pos - pos))
        return DecNumber.from_bcd(bcd, scale)

    @classmethod
    def to_string_decimal(cls, data, len): # mp_snprint_decimal
        d, err = cls.decode_decimal(data, len)
        if err is not None:
            gdb.write(err, gdb.STDERR)
        if d is None:
            return
        return 'dec:' + str(d)

    @classmethod
    def decode_uuid(cls, data, len): # uuid_unpack
        if len != cls.UUID_PACKED_LEN:
            return None, "uuid_unpack: unexpected length ({})".format(len)
        uuid = Uuid(dict(
            time_low = data.read_u32(),
            time_mid = data.read_u16(),
            time_hi_and_version = data.read_u16(),
            clock_seq_hi_and_reserved = data.read_u8(),
            clock_seq_low = data.read_u8(),
            node = [ data.read_u8() for _ in range(0, 6) ],
        ))
        if not uuid.is_valid():
            return uuid, "uuid_unpack: invalid uuid"
        return uuid, None

    @classmethod
    def to_string_uuid(cls, data, len): # mp_snprint_uuid
        uuid, err = cls.decode_uuid(data, len)
        if err is not None:
            gdb.write(err, gdb.STDERR)
        if uuid is None:
            return
        return str(uuid)

    @classmethod
    def decode_datetime(cls, data, len): # datetime_unpack
        SZ_TAIL = gdb.parse_and_eval('sizeof(struct datetime) - sizeof(((struct datetime *)0)->epoch)')

        if len != 8 and len != 8 + SZ_TAIL:
            return None

        epoch = data.read_i64(False)
        if len == 8:
            date = Datetime(dict(
                epoch = epoch,
                nsec = 0,
                tzoffset = 0,
                tzindex = 0,
            ))
        else:
            date = Datetime(dict(
                epoch = epoch,
                nsec = data.read_i32(False),
                tzoffset = data.read_i16(False),
                tzindex = data.read_i16(False),
            ))

        if not date.is_valid():
            return date, 'invalid date'

        return date, None

    @classmethod
    def to_string_datetime(cls, data, len): # mp_snprint_datetime
        date, err = cls.decode_datetime(data, len)
        if err is not None:
            gdb.write(err, gdb.STDERR)
        return str(date)


    @classmethod
    def to_string_error_one(cls, data, depth): # mp_print_error_one
        s += '{'
        if depth == 0:
            s += '...}'
            return
        depth -= 1
        if cls.typeof(data) != cls.MP_MAP:
            raise gdb.GdbError("print_error_one: wrong type {} (expected {})", cls.typeof(data), cls.MP_MAP)
        map_size = cls.decode_map(data)
        for i in range(0, map_size):
            if i != 0:
                s += ', '
            if cls.typeof(data) != cls.MP_UINT:
                raise gdb.GdbError("print_error_one: wrong type {} (expected {})", cls.typeof(data), cls.MP_UINT)
            key = cls.decode_uint(data)
            if key < cls.MP_ERROR_MAX:
                s += cls.mp_error_field_to_json_key[key].string()
            else:
                s += '{}: '.format(key)
            s += cls.to_string_data(data, depth)
        s += '}'

    @classmethod
    def to_string_error_stack(cls, data, depth): # mp_print_error_stack
        s += '['
        if depth == 0:
            s += '...]'
            return
        depth -= 1
        if cls.typeof(data) != cls.MP_ARRAY:
            return -1
        arr_size = cls.decode_array(data)
        for i in range(0, arr_size):
            if i != 0:
                s += ', '
            s += cls.to_string_error_one(data, depth)
        s += ']'

    @classmethod
    def to_string_error(cls, data, depth): # mp_print_error
        s = str()
        s += '{'
        if depth == 0:
            s += '...}'
            return
        depth -= 1
        if cls.typeof(data) != cls.MP_MAP:
            return -1
        map_size = cls.decode_map(data)
        for i in range(0, map_size):
            if i != 0:
                s += ', '
            if cls.typeof(data) != cls.MP_UINT:
                return -1
            key = cls.decode_uint(data)
            if key == cls.MP_ERROR_STACK:
                s += '"stack": '
                s += cls.to_string_error_stack(data, depth)
            else:
                s += '{}: '.format(key)
                s += cls.to_string_data(data, depth)
        s += '}'

    @classmethod
    def decode_compression(cls, data, len):
        pos = data.pos
        self = Compression(dict(
            type = cls.decode_uint(data),
            raw_size = cls.decode_uint(data),
            size = len - (data.pos - pos),
        ))
        data.seek(self.size)
        return self

    @classmethod
    def to_string_compression(cls, data, len): # mp_snprint_compression
        ext = cls.decode_compression(data, len)
        if ext is None:
            return
        return str(ext)

    @classmethod
    def decode_interval_field(cls, data):
        mp_type = cls.typeof(data)
        if mp_type == cls.MP_UINT:
            value = cls.decode_uint(data)
        elif mp_type == cls.MP_INT:
            value = cls.decode_int(data)
        else:
            raise gdb.GdbError("Interval.decode_field: unexpected type ({})".format(mp_type))
        return value

    INTERVAL_FIELD_YEAR = find_value('FIELD_YEAR')
    INTERVAL_FIELD_MONTH = find_value('FIELD_MONTH')
    INTERVAL_FIELD_WEEK = find_value('FIELD_WEEK')
    INTERVAL_FIELD_DAY = find_value('FIELD_DAY')
    INTERVAL_FIELD_HOUR = find_value('FIELD_HOUR')
    INTERVAL_FIELD_MINUTE = find_value('FIELD_MINUTE')
    INTERVAL_FIELD_SECOND = find_value('FIELD_SECOND')
    INTERVAL_FIELD_NANOSECOND = find_value('FIELD_NANOSECOND')
    INTERVAL_FIELD_ADJUST = find_value('FIELD_ADJUST')

    DT_SNAP = find_value('DT_SNAP')

    @classmethod
    def decode_interval(cls, data, len): # interval_unpack
        pos = data.pos
        itv = cls()
        num_fields = data.read_u8()
        for _ in range(0, num_fields):
            field = data.read_u8()
            value = cls.decode_interval_field(data)
            if field == cls.INTERVAL_FIELD_YEAR:
                itv.year = value
            elif field == cls.INTERVAL_FIELD_MONTH:
                itv.month = value
            elif field == cls.INTERVAL_FIELD_WEEK:
                itv.week = value
            elif field == cls.INTERVAL_FIELD_DAY:
                itv.day = value
            elif field == cls.INTERVAL_FIELD_HOUR:
                itv.hour = value
            elif field == cls.INTERVAL_FIELD_MINUTE:
                itv.min = value
            elif field == cls.INTERVAL_FIELD_SECOND:
                itv.sec = value
            elif field == cls.INTERVAL_FIELD_NANOSECOND:
                itv.nsec = value
            elif field == cls.INTERVAL_FIELD_ADJUST:
                if value > cls.DT_SNAP:
                    return None, "unexpected adjust value ({})".format(value)
                itv.adjust = value
            else:
                return None, "unexpected field type ({})".format(field)
        if data.pos - pos != len:
            data.seek(pos + len)
            return None, "unexpected length: expected - {}, actual - {}".format(len, data.pos - pos)
        return itv, None

    @classmethod
    def to_string_interval(cls, data, len): # mp_snprint_interval
        itv, err = cls.decode_interval(data, len)
        if err is not None:
            gdb.write(err, gdb.STDERR)
            return
        return str(itv)

    @classmethod
    def to_string_ext(cls, data, depth, ext_len, ext_type): # msgpack_snprint_ext
        pos = data.pos

        if ext_type == cls.MP_DECIMAL:
            s = cls.to_string_decimal(data, ext_len)
        elif ext_type == cls.MP_UUID:
            s = cls.to_string_uuid(data, ext_len)
        elif ext_type == cls.MP_DATETIME:
            s = cls.to_string_datetime(data, ext_len)
        elif ext_type == cls.MP_ERROR:
            s = cls.to_string_error(data, depth)
        elif ext_type == cls.MP_COMPRESSION:
            s = cls.to_string_compression(data, ext_len)
        elif ext_type == cls.MP_INTERVAL:
            s = cls.to_string_interval(data, ext_len)
        else:
            return super(TtMsgPack, cls).to_string_ext(data, depth, ext_len, ext_type)

        # make sure decoding ended up exactly where expected
        actual_ext_len = data.pos - pos
        if actual_ext_len != ext_len:
            gdb.write(cls.__name__ + ".to_string_ext: decoded length {} (expected {})".format(actual_ext_len, ext_len))
            data.seek(-actual_ext_len)
            return super(TtMsgPack, cls).to_string_ext(data, depth, ext_len, ext_type)

        return s

if find_type('struct decNumber') is not None:
    class DecNumber:
        DECIMAL_MAX_DIGITS = 38
        DECNUMDIGITS = DECIMAL_MAX_DIGITS

        DECDPUN = 4
        DECNUMUNITS = (DECNUMDIGITS + DECDPUN - 1) // DECDPUN

        DECPOWERS = [ 10**i for i in range(0, DECDPUN) ]

        DECNEG = 0x80
        DECINF = 0x40
        DECNAN = 0x20
        DECSNAN = 0x10
        DECSPECIAL = DECINF | DECNAN | DECSNAN

        DECPMINUS = 0x0D
        DECPMINUSALT = 0x0B
        DECNUMMAXE = 999999999

        @classmethod
        def d2u(cls, d): # D2U
            return (d + cls.DECDPUN - 1) // cls.DECDPUN

        @classmethod
        def msu_digits(cls, d): # MSUDIGITS
            return d - (cls.d2u(d) - 1) * cls.DECDPUN

        @classmethod
        def to_digit(cls, u, cut): # TODIGIT
            if cut + 1 < cls.DECDPUN: # drop higher digits, if any
                u %= cls.DECPOWERS[cut + 1]
            c = 0
            pow = cls.DECPOWERS[cut] * 2
            if u > pow:
                pow *= 4
                if u >= pow: u -= pow; c += 8
                pow = pow // 2
                if u >= pow: u -= pow; c += 4
                pow = pow // 2
            if u >= pow: u -= pow; c += 2
            pow = pow // 2
            if u >= pow: u -= pow; c += 1
            return chr(ord('0') + c)

        def __init__(self, val):
            self.val = val

        @classmethod
        def from_bcd(cls, bcd, scale): # decPackedToNumber
            if scale > cls.DECIMAL_MAX_DIGITS or scale <= -cls.DECIMAL_MAX_DIGITS:
                return None, "from_bcd: unexpected scale ({})".format(scale)

            last = len(bcd) - 1
            first = 0
            up = 0
            cut = 0

            dn = cls(dict( # decNumberZero
                bits = 0,
                exponent = 0,
                digits = 1,
                lsu = [ 0 for _ in range(0, cls.DECNUMUNITS) ],
            ))

            nib = bcd[last] & 0x0f
            if nib == cls.DECPMINUS or nib == cls.DECPMINUSALT:
                dn.val['bits'] = cls.DECNEG
            elif nib <= 9:
                return None, "not a sign nibble"

            while bcd[first] == 0:
                first += 1

            digits = (last - first) * 2 + 1
            if (bcd[first] & 0xf0) == 0:
                digits -= 1
            if digits != 0:
                dn.val['digits'] = digits

            dn.val['exponent'] = -scale
            if scale >= 0:
                if (dn.val['digits'] - scale - 1) < -cls.DECNUMMAXE:
                    return None, "underflow"

            else:
                if scale < -cls.DECNUMMAXE or (dn.val['digits'] - scale - 1) > cls.DECNUMMAXE:
                    return None, "overflow"

            if digits == 0: return dn

            while True:
                nib = (bcd[last] & 0xf0) >> 4;
                if nib > 9: return None, "unexpected digit in high nibble: bcd[{}]={:02x}".format(last, bcd[last])

                if cut == 0: dn.val['lsu'][up] = nib
                else: dn.val['lsu'][up] += nib * cls.DECPOWERS[cut]
                digits -= 1
                if digits == 0: break
                cut += 1
                if cut == cls.DECDPUN:
                    up += 1
                    cut = 0

                last -= 1
                nib = bcd[last] & 0x0f
                if nib > 9: return None, "unexpected digit in low nibble: bcd[{}]={:02x}".format(last, bcd[last])

                if cut == 0: dn.val['lsu'][up] = nib
                else: dn.val['lsu'][up] += nib * cls.DECPOWERS[cut]
                digits -= 1
                if digits == 0: break
                cut += 1
                if cut == cls.DECDPUN:
                    up += 1
                    cut = 0

            return dn, None

        def is_negative(self): # decNumberIsNegative
            return (self.val['bits'] & self.DECNEG) != 0

        def is_infinite(self): # decNumberIsInfinite
            return (self.val['bits'] & self.DECINF) != 0

        def is_snan(self): # decNumberIsSNaN
            return (self.val['bits'] & self.DECSNAN) != 0

        def is_special(self): # decNumberIsSpecial
            return (self.val['bits'] & self.DECSPECIAL) != 0

        def __str__(self): # decToString
            self_exp = self.val['exponent']
            self_digits = self.val['digits']
            self_lsu = self.val['lsu']

            num_units = self.d2u(self_digits)
            up = num_units - 1

            s = ''
            if self.is_negative():
                s += '-'

            if self.is_special():
                if self.is_infinite():
                    s += "Infinity"
                    return s

                if self.is_snan():
                    s += 's'
                s += "NaN"
                if self_exp != 0 or (self_lsu[0] == 0 and self_digits == 1):
                    return s

            cut = self.msu_digits(self_digits)
            cut -= 1

            if self_exp == 0:
                for i in range(num_units, 0, -1):
                    u = self_lsu[up]
                    for j in range(cut, -1, -1):
                        s += self.to_digit(u, j)
                return s

            pre = self_digits + self_exp

            u = self_lsu[up]
            if pre > 0:
                n = pre
                for i in range(pre, 0, -1):
                    if cut < 0:
                        if up == 0: break
                        up -= 1
                        cut = self.DECDPUN - 1
                        u = self_lsu[up]
                    s += self.to_digit(u, cut)
                    cut -= 1

                if n < self_digits:
                    s += '.'
                    while True:
                        if cut < 0:
                            if up == 0: break
                            up -= 1
                            cut = self.DECDPUN - 1
                            u = self_lsu[up]
                        s += self.to_digit(u, cut)
                        cut -= 1
                else:
                    for i in range(pre, 0, -1):
                        s += '0'

            else:
                s += '0.'
                for i in range(pre, 0):
                    s += '0'
                while True:
                    if cut < 0:
                        if up == 0: break
                        up -= 1
                        cut = self.DECDPUN - 1
                        u = self_lsu[up]
                    s += self.to_digit(u, cut)
                    cut -= 1

            return s

    class DecNumberPrinter:
        def __init__(self, val):
            self.val = DecNumber(val)

        def to_string(self):
            return str(self.val)

    pp.add_printer('DecNumber', '^decNumber$', DecNumberPrinter)

if find_type('struct tt_uuid') is not None:
    class Uuid:
        def __init__(self, val):
            self.val = val

        def is_valid(self): # tt_uuid_validate
            n = self.val['clock_seq_hi_and_reserved']
            if (n & 0x80) != 0x00 and (n & 0xc0) != 0x80 and (n & 0xe0) != 0xc0:
                return False
            return True

        def __str__(self): # tt_uuid_to_string
            return '{:08x}-{:04x}-{:04x}-{:02x}{:02x}-{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}'.format(
                self.val['time_low'], self.val['time_mid'], self.val['time_hi_and_version'],
                self.val['clock_seq_hi_and_reserved'], self.val['clock_seq_low'], self.val['node'][0],
                self.val['node'][1], self.val['node'][2], self.val['node'][3], self.val['node'][4], self.val['node'][5])

    class UuidPrinter:
        def __init__(self, val):
            self.val = Uuid(val)

        def to_string(self):
            return str(self.val)

    pp.add_printer('Uuid', '^tt_uuid$', UuidPrinter)

if find_type('struct datetime') is not None:
    class Datetime:
        ######################################################
        # this section corresponds to c-dt library

        DT_EPOCH_OFFSET = 0
        DT1901 = 693961 + DT_EPOCH_OFFSET
        DT2099 = 766644 + DT_EPOCH_OFFSET
        days_preceding_month = [
            [ 0, 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 ],
            [ 0, 0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335 ], # leap year
        ]

        @staticmethod
        def leap_year(y):
            return (y & 3) == 0 and (y % 100 != 0 or y % 400 == 0)

        @classmethod
        def dt_from_rdn(cls, rdn): # dt_from_rdn
            return rdn + cls.DT_EPOCH_OFFSET

        @classmethod
        def dt_to_yd(cls, d): # dt_to_yd
            y = 0

            if d >= cls.DT1901 and d <= cls.DT2099:
                d -= cls.DT1901 - 1
                y += (4 * d - 1) // 1461
                d -= (1461 * y) // 4
                y += 1901

            else:
                d -= cls.DT_EPOCH_OFFSET
                if d < 1:
                    n400 = 1 - d // 146097
                    y -= n400 * 400
                    d += n400 * 146097
                d -= 1
                y += 400 * (d // 146097)
                d %= 146097

                n100 = d // 36524
                y += 100 * n100
                d %= 36524

                y += 4 * (d // 1461)
                d %= 1461

                n1 = d // 365
                y += n1
                d %= 365

                if n100 == 4 or n1 == 4:
                    d = 366
                else:
                    y += 1
                    d += 1

            return y, d

        @classmethod
        def dt_to_ymd(cls, dt): # dt_to_ymd
            y, doy = cls.dt_to_yd(dt)
            l = cls.leap_year(y)
            m = 1 if doy < 32 else 1 + (5 * (doy - 59 - l) + 303) // 153

            if m < 1 or m > 12:
                raise gdb.GdbError("Datetime.st_to_ymd: unexpected month ({})".format(m))

            return y, m, doy - cls.days_preceding_month[l][m]

        ######################################################
        # this section corresponds to tzcode library

        zones_unsorted = gdb.parse_and_eval('zones_unsorted')
        zones_unsorted_size = gdb.parse_and_eval('sizeof(zones_unsorted) / sizeof(zones_unsorted[0])')

        @classmethod
        def timezone_name(cls, index): # timezone_name
            if index >= cls.zones_unsorted_size:
                raise gdb.GdbError("Datetime.timezone_name: index {} is out of bound".format(index))
            return cls.zones_unsorted[index]['name'].string()

        ######################################################
        # this section corresponds to tarantool core library

        SECS_PER_DAY = 86400
        DT_EPOCH_1970_OFFSET = 719163
        MAX_NANOS_PER_SEC = 2000000000

        MAX_DT_DAY_VALUE = INT_MAX
        MIN_DT_DAY_VALUE = INT_MIN
        SECS_EPOCH_1970_OFFSET = DT_EPOCH_1970_OFFSET * SECS_PER_DAY
        MAX_EPOCH_SECS_VALUE = MAX_DT_DAY_VALUE * SECS_PER_DAY - SECS_EPOCH_1970_OFFSET
        MIN_EPOCH_SECS_VALUE = MIN_DT_DAY_VALUE * SECS_PER_DAY - SECS_EPOCH_1970_OFFSET

        MAX_TZOFFSET = 14 * 60
        MIN_TZOFFSET = -12 * 60
        MAX_TZINDEX = 1024

        def __init__(self, val):
            self.val = val

        def is_valid(self): # datetime_validate
            if self.val['epoch'] < self.MIN_EPOCH_SECS_VALUE or self.val['epoch'] > self.MAX_EPOCH_SECS_VALUE:
                return False
            if self.val['nsec'] < 0 or self.val['nsec'] >= self.MAX_NANOS_PER_SEC:
                return False
            if self.val['tzoffset'] < self.MIN_TZOFFSET or self.val['tzoffset'] > self.MAX_TZOFFSET:
                return False
            if self.val['tzindex'] < 0 or self.val['tzindex'] > self.MAX_TZINDEX:
                return False
            return True

        def __str__(self): # datetime_to_string
            offset = self.val['tzoffset']
            tzindex = self.val['tzindex']
            rd_seconds = self.val['epoch'] + offset * 60 + self.SECS_EPOCH_1970_OFFSET
            rd_number = rd_seconds // self.SECS_PER_DAY
            if rd_number < INT_MIN or rd_number > INT_MAX:
                raise gdb.GdbError('{} is out of bound'.format(rd_number))
            dt = self.dt_from_rdn(rd_number)

            year, month, day = self.dt_to_ymd(dt)

            rd_seconds = rd_seconds % self.SECS_PER_DAY
            hour = (rd_seconds // 3600) % 24
            minute = (rd_seconds // 60) % 60
            second = rd_seconds % 60
            nanosec = self.val['nsec']

            s = str()
            s += '{:04d}-{:02d}-{:02d}T{:02d}:{:02d}:{:02d}'.format(
                year, month, day, hour, minute, second)
            if nanosec != 0:
                if nanosec % 1000000 == 0:
                    s += '.{:03d}'.format(nanosec // 1000000)
                elif nanosec % 1000 == 0:
                    s += '.{:06d}'.format(nanosec // 1000)
                else:
                    s += '.{:09d}'.format(nanosec)

            if tzindex != 0:
                tz_name = self.timezone_name(tzindex)
                if tz_name is None:
                    raise gdb.GdbError("Datetime::__str__: timezone name is missing")
                s += ('{}' if len(tz_name) <= 1 else ' {}').format(tz_name)

            elif offset == 0:
                s += 'Z'

            else:
                if offset < 0:
                    sign = '-';
                    offset = -offset;
                else:
                    sign = '+';
                s += '{}{:02d}{:02d}'.format(sign, offset // 60, offset % 60)

            return s

    class DatetimePrinter:
        def __init__(self, val):
            self.val = Datetime(val)

        def to_string(self):
            return str(self.val)

    pp.add_printer('Datetime', '^datetime$', DatetimePrinter)

if TtMsgPack.MP_COMPRESSION is not None:
    class Compression:
        COMPRESSION_TYPE_NONE = gdb.parse_and_eval('COMPRESSION_TYPE_NONE')
        COMPRESSION_TYPE_MAX = gdb.parse_and_eval('compression_type_MAX')
        compression_type_strs = gdb.parse_and_eval('compression_type_strs')

        def __init__(self, val):
            self.val = val

        def __str__(self):
            type = self.val['type']
            if type >= self.COMPRESSION_TYPE_NONE and type < self.COMPRESSION_TYPE_MAX:
                type = self.compression_type_strs[type].string()
            return 'compression({}):[{}]->[{}]'.format(type, self.val['raw_size'], self.val['size'])

if find_type('struct interval') is not None:
    class Interval:
        def __init__(self):
            self.year = 0
            self.month = 0
            self.week = 0
            self.day = 0
            self.hour = 0
            self.min = 0
            self.sec = 0
            self.nsec = 0
            self.adjust = 0

        def __str__(self): # interval_to_string
            signed_fmt = [
                '{:d}',
                '{:+d}',
            ]
            s = []
            if self.year != 0:
                s.append(signed_fmt[len(s) == 0].format(self.year) + ' years')
            if self.month != 0:
                s.append(signed_fmt[len(s) == 0].format(self.month) + ' months')
            if self.week != 0:
                s.append(signed_fmt[len(s) == 0].format(self.week) + ' weeks')
            if self.day != 0:
                s.append(signed_fmt[len(s) == 0].format(self.day) + ' days')
            if self.hour != 0:
                s.append(signed_fmt[len(s) == 0].format(self.hour) + ' hours')
            if self.min != 0:
                s.append(signed_fmt[len(s) == 0].format(self.min) + ' minutes')
            if self.sec != 0 or len(s) == 0:
                s.append(signed_fmt[len(s) == 0].format(self.sec) + ' seconds')
            if self.nsec != 0:
                s.append(signed_fmt[len(s) == 0].format(self.nsec) + ' nanoseconds')
            return ' '.join(s)

    class IntervalPrinter:
        def __init__(self, val):
            self.val = Interval(val)

        def to_string(self):
            return str(self.val)

    pp.add_printer('Interval', '^interval$', IntervalPrinter)


class MsgPackPrint(gdb.Command):
    """
Decode and print MsgPack referred by EXP in a human-readable form
Usage: tt-mp [OPTIONS]... EXP

Options:
  -max-depth NUMBER
    Set maximum depth for nested arrays and maps.
    When arrays or maps are nested beyond this depth then they
    will be replaced with either [...] or {...}

  -max-length NUMBER
    Set limit on string chars to print.
    """
    def __init__(self):
        super(MsgPackPrint, self).__init__('tt-mp', gdb.COMMAND_DATA)

    def invoke(self, arg, from_tty):
        parser = argparse.ArgumentParser(add_help=False)
        parser.add_argument('-max-depth', type=int)
        parser.add_argument('-max-length', type=int)
        args, exp_args = parser.parse_known_args(gdb.string_to_argv(arg))
        if not exp_args:
            raise gdb.GdbError("MsgPack is missing")
        mp = TtMsgPack(gdb.parse_and_eval(' '.join(exp_args)))
        gdb.write(mp.to_string(args.max_depth, args.max_length) + '\n')


class JsonTokenPrinter:
    """Print a json_token object."""

    JSON_TOKEN_NUM = gdb.parse_and_eval('JSON_TOKEN_NUM')
    JSON_TOKEN_STR = gdb.parse_and_eval('JSON_TOKEN_STR')
    JSON_TOKEN_ANY = gdb.parse_and_eval('JSON_TOKEN_ANY')
    JSON_TOKEN_END = gdb.parse_and_eval('JSON_TOKEN_END')

    tuple_field_type = gdb.lookup_type('tuple_field')

    def __init__(self, val):
        self.val = val

    def to_string(self):
        type = self.val['type']
        hash = self.val['hash']

        if type == self.JSON_TOKEN_NUM:
            s_id = ' id=' + str(self.val['num'])
        elif type == self.JSON_TOKEN_STR:
            s_id = ' id=' + self.val['str'].string('utf-8', 'strict', int(self.val['len']))
        else:
            s_id = str()
        s = 'type={}{} hash={} parent={} sibling_idx={}'.format(
            int(type),
            s_id,
            int(hash),
            str(self.val['parent']),
            int(self.val['sibling_idx']),
        )

        if self.val['parent'] != 0:
            field = container_of(self.val.address, self.tuple_field_type, 'token')
            s += ' field_id={} field_type={} offset_slot={} is_key/multikey_part={}/{}'.format(
                str(field['id']),
                str(field['type']),
                str(field['offset_slot']),
                str(field['is_key_part']),
                str(field['is_multikey_part']),
            )

        return s

    def children(self):
        for i in range(0, int(self.val['max_child_idx']) + 1):
            yield str(i), self.val['children'][i].dereference()

    def display_hint(self):
        return 'array'

pp.add_printer('JsonToken', '^json_token$', JsonTokenPrinter)


class TuplePrinter(object):
    """Print a tuple object."""

    tuple_type = gdb.lookup_type('struct tuple')
    support_compact = gdb.types.has_field(tuple_type, 'data_offset_bsize_raw')

    tuple_formats_sym = gdb.lookup_global_symbol('tuple_formats')
    if not tuple_formats_sym:
        raise NameError('tuple_formats is missing')
    tuple_formats = tuple_formats_sym.value()

    ptr_char = gdb.lookup_type('char').pointer()
    ptr_int32 = gdb.lookup_type('int32_t').pointer()
    ptr_uint32 = gdb.lookup_type('uint32_t').pointer()
    slot_extent_t = find_type('struct field_map_builder_slot_extent')

    # Printer configuration.
    # Initialization of config with default values is deferred so it can be
    # done in a single place to avoid duplication of default constants
    __config = None

    @classmethod
    def reset_config(cls,
                    mp_max_depth=None,
                    mp_max_length=None):
        cls.__config = dict(
            mp_max_depth=mp_max_depth,
            mp_max_length=mp_max_length,
        )

    def __new__(cls, val):
        # Deferred initialization of config
        if cls.__config is None:
            cls.reset_config()
        return super(TuplePrinter, cls).__new__(cls)

    def __init__(self, val):
        if not equal_types(val.type, self.tuple_type):
            raise gdb.GdbError("expression doesn't evaluate to tuple")
        self.val = val
        # Pull configuration from class variables into the instance for
        # convenience
        config = self.__class__.__config
        assert config is not None
        self.mp_max_depth = config['mp_max_depth']
        self.mp_max_length = config['mp_max_length']

    def is_compact(self): # tuple_is_compact
        return self.support_compact and self.val['data_offset_bsize_raw'] & 0x8000

    def format(self): # tuple_format
        return self.tuple_formats[self.val['format_id']].dereference()

    def data_offset(self): # tuple_data_offset
        # prior to introducing of compact mode data offset was just stored
        # in the corresponding field
        if not self.support_compact:
            return self.val['data_offset']
        # after introducing support of compact mode offset is stored
        # in a different way
        res = self.val['data_offset_bsize_raw']
        is_compact_bit = res >> 15
        res = (res & 0x7fff) >> (is_compact_bit * 8)
        return res

    def data(self): # tuple_data
        return self.val.address.cast(self.ptr_char) + self.data_offset()

    def field_map(self):
        slots = self.data().cast(self.ptr_int32)
        num_slots = int(self.format()['field_map_size']) // slots.type.target().sizeof
        fields = []
        key_by_offset = lambda offs: '{}(+{})'.format(str(TtMsgPack(self.data() + offs)), offs)
        for i in range(1, num_slots + 1):
            field_offs = slots[-i]
            key = str()
            if field_offs > 0:
                key = key_by_offset(field_offs)
            elif field_offs < 0:
                assert self.slot_extent_t is not None
                ext = (self.data() + field_offs).cast(self.slot_extent_t.pointer()).dereference()
                num_ext_keys = ext['size']
                offsets = ext['offset']
                key = '[{}-{}]:'.format(slots - ext.address.cast(slots.type), int(num_ext_keys))
                for ikey in range(0, num_ext_keys):
                    key += key_by_offset(offsets[ikey])
            else:
                key = 'missed'
            fields.append('[{}]:{}'.format(i, key))

        return ', '.join(fields)

    def children(self):
        for field in self.val.type.fields():
            child_name = field.name
            if field.name is None:
                child_name = str()
                child_val = (self.val.address.cast(self.ptr_char) + field.bitpos // 8).cast(field.type.pointer()).dereference()
            elif field.name == 'flags':
                child_val = '0x{:02x}'.format(int(self.val['flags']))
            else:
                child_val = self.val[field.name]
            yield child_name, child_val
        yield 'format', self.format()
        yield 'field_map', self.field_map()
        if self.support_compact:
            yield 'is_compact', self.is_compact()
        yield 'data_offset', self.data_offset()
        mp = TtMsgPack(self.data())
        yield 'data', mp.to_string(self.mp_max_depth, self.mp_max_length)

pp.add_printer('Tuple', '^tuple$', TuplePrinter)


class TuplePrint(gdb.Command):
    """
Decode and print tuple referred by EXP
Usage: tt-tuple [OPTIONS]... EXP

Options:
  -mp-max-depth NUMBER
    See '-max-depth' option of 'tt-mp' command

  -mp-max-length NUMBER
    See '-max-length' option of 'tt-mp' command
    """

    def __init__(self):
        super(TuplePrint, self).__init__('tt-tuple', gdb.COMMAND_DATA)

    def invoke(self, arg, from_tty):
        parser = argparse.ArgumentParser(add_help=False)
        parser.add_argument('-mp-max-depth', type=int)
        parser.add_argument('-mp-max-length', type=int)
        args, print_args = parser.parse_known_args(gdb.string_to_argv(arg))

        TuplePrinter.reset_config(
            mp_max_depth = args.mp_max_depth,
            mp_max_length = args.mp_max_length,
        )
        try:
            gdb.execute('print {}'.format(' '.join(print_args)), from_tty)
        except Exception as e:
            raise e
        finally:
            TuplePrinter.reset_config()

MsgPackPrint()
TuplePrint()


class TtListEntryInfo(object):
    def __init__(self, entry_info):
        self.__str = entry_info
        entry_type, _, fields = entry_info.partition('::')
        self.__entry_type = gdb.lookup_type('struct ' + entry_type)
        self.__offset = 0
        container_type = self.__entry_type
        for field in fields.split('::'):
            self.__offset -= int(container_type[field].bitpos/8)
            container_type = container_type[field].type

    def __eq__(self, other):
        return self.__str == other.__str

    def __hash__(self):
        return hash(str(self))

    def __str__(self):
        return self.__str

    @property
    def entry_type(self):
        return self.__entry_type

    def entry_from_item(self, item):
        assert equal_types(item.type.target(), TtList.rlist_type)
        return cast_ptr(self.__entry_type, item, self.__offset)


class TtListsLut(object):
    def build_list_variables_map():
        ret = {}
        rlist_syms = (
            ('box_on_call', 'trigger::link'),
            ('box_on_select', 'trigger::link'),
            ('box_on_shutdown_trigger_list', 'trigger::link'),
            ('box_raft_on_broadcast', 'trigger::link'),
            ('engines', 'engine::link'),
            ('log_rotate_list', 'log::in_log_list'), #static
            ('on_access_denied', 'trigger::link'),
            ('on_alter_func', 'trigger::link'),
            ('on_alter_sequence', 'trigger::link'),
            ('on_alter_space', 'trigger::link'),
            ('on_console_eval', 'trigger::link'),
            ('on_schema_init', 'trigger::link'),
            ('on_shutdown_trigger_list', 'on_shutdown_trigger::link'),
            ('popen_head', 'popen_handle::list'),
            ('replicaset_on_quorum_gain', 'trigger::link'),
            ('replicaset_on_quorum_loss', 'trigger::link'),
            ('session_on_auth', 'trigger::link'),
            ('session_on_connect', 'trigger::link'),
            ('session_on_disconnect', 'trigger::link'),
            ('shutdown_list', 'session::in_shutdown_list'),
        )
        for sym, entry in rlist_syms:
            try:
                ret[sym] = TtListEntryInfo(entry)
            except Exception as e:
                logger.debug(str(e))
        return ret

    def build_list_containers_map():
        ret = {}
        rlist_containers = (
            ('alter_space::ops', 'AlterSpaceOp::link'),
            ('alter_space::key_list', 'index_def::link'),
            ('applier::on_state', 'trigger::link'),
            ('cbus::endpoints', 'cbus_endpoint::in_cbus'),
            ('checkpoint::entries', 'checkpoint_entry::link'),
            ('cord::alive', 'fiber::link'),
            ('cord::dead', 'fiber::link'),
            ('cord::ready', 'fiber::state'),
            ('cpipe::on_flush', 'trigger::link'),
            ('create_ck_constraint_parse_def::checks', 'ck_constraint_parse::link'),
            ('create_fk_constraint_parse_def::fkeys', 'fk_constraint_parse::link'),
            ('fiber::on_stop', 'trigger::link'),
            ('fiber::on_yield', 'trigger::link'),
            ('fiber::wake', 'fiber::state'),
            ('fiber_channel::waiters', 'fiber::state'),
            ('fiber_cond::waiters', 'fiber::state'),
            ('fiber_pool::idle', 'fiber::state'),
            ('func::func_cache_pin_list', 'func_cache_holder::link'),
            ('gc_checkpoint::refs', 'gc_checkpoint_ref::in_refs'),
            ('gc_state::checkpoints', 'gc_checkpoint::in_checkpoints'),
            ('index::full_scans', 'full_scan_item::in_full_scans'),
            ('index::nearby_gaps', 'gap_item::in_nearby_gaps'),
            ('iproto_thread::stopped_connections', 'iproto_connection::in_stop_list'),
            ('journal_queue::waiters', 'fiber::state'),
            ('latch::queue', 'fiber::state'),
            ('luaL_serializer::on_update', 'trigger::link'),
            ('mempool::cold_slabs', 'mslab::next_in_cold'),
            ('memtx_join_ctx::entries', 'memtx_join_entry::in_ctx'),
            ('memtx_story::reader_list', 'tx_read_tracker::in_reader_list'),
            ('memtx_story_link::nearby_gaps', 'gap_item::in_nearby_gaps'),
            ('point_hole_item::ring', 'point_hole_item::ring'),
            ('raft::on_update', 'trigger::link'),
            ('RebuildCkConstraints::ck_constraint', 'ck_constraint::link'),
            ('recovery::on_close_log', 'trigger::link'),
            ('replicaset::anon', 'replica::in_anon'),
            ('replicaset::on_ack', 'trigger::link'),
            ('replicaset::applier::on_rollback', 'trigger::link'),
            ('replicaset::applier::on_wal_write', 'trigger::link'),
            ('schema_module::funcs', 'func_c::item'),
            ('space::before_replace', 'trigger::link'),
            ('space::child_fk_constraint', 'fk_constraint::in_child_space'),
            ('space::ck_constraint', 'ck_constraint::link'),
            ('space::memtx_stories', 'memtx_story::in_space_stories'),
            ('space::on_replace', 'trigger::link'),
            ('space::parent_fk_constraint', 'fk_constraint::in_parent_space'),
            ('space::space_cache_pin_list', 'space_cache_holder::link'),
            ('swim::dissemination_queue', 'swim_member::in_dissemination_queue'),
            ('swim::on_member_event', 'trigger::link'),
            ('swim::round_queue', 'swim_member::in_round_queue'),
            ('swim_scheduler::queue_output', 'swim_task::in_queue_output'),
            ('tx_manager::read_view_txs', 'txn::in_read_view_txs'),
            ('tx_manager::all_stories', 'memtx_story::in_all_stories'),
            ('tx_manager::traverse_all_stories', 'memtx_story::in_all_stories'), # struct rlist*
            ('tx_manager::all_txs', 'txn::in_all_txs'),
            ('txn::conflict_list', 'tx_conflict_tracker::in_conflict_list'),
            ('txn::conflicted_by_list', 'tx_conflict_tracker::in_conflicted_by_list'),
            ('txn::gap_list', 'gap_item::in_gap_list'),
            ('txn::full_scan_list', 'full_scan_item::in_full_scan_list'),
            ('txn::on_commit', 'trigger::link'),
            ('txn::on_rollback', 'trigger::link'),
            ('txn::on_wal_write', 'trigger::link'),
            ('txn::point_holes_list', 'point_hole_item::in_point_holes_list'),
            ('txn::read_set', 'tx_read_tracker::in_read_set'),
            ('txn::savepoints', 'txn_savepoint::link'),
            ('txn_limbo::queue', 'txn_limbo_entry::in_queue'),
            ('txn_stmt::on_commit', 'trigger::link'),
            ('txn_stmt::on_rollback', 'trigger::link'),
            ('sql_stmt_cache::gc_queue', 'stmt_cache_entry::link'),
            ('sys_alloc::allocations', 'container::rlist'),
            ('user::credentials_list', 'credentials::in_user'),
            ('vy_cache_env::cache_lru', 'vy_cache_node::in_lru'),
            ('vy_history::stmts', 'vy_history_node::link'),
            ('vy_join_ctx::entries', 'vy_join_entry::in_ctx'),
            ('vy_lsm::on_destroy', 'trigger::link'),
            ('vy_lsm::runs', 'vy_run::in_lsm'),
            ('vy_lsm::sealed', 'vy_mem::in_sealed'),
            ('vy_lsm_recovery_info::ranges', 'vy_range_recovery_info::in_lsm'),
            ('vy_lsm_recovery_info::runs', 'vy_run_recovery_info::in_lsm'),
            ('vy_quota::wait_queue', 'vy_quota_wait_node::in_wait_queue'),
            ('vy_range::slices', 'vy_slice::::in_range'),
            ('vy_range_recovery_info::slices', 'vy_slice_recovery_info::in_range'),
            ('vy_recovery::lsms', 'vy_lsm_recovery_info::in_recovery'),
            ('vy_tx::on_destroy', 'trigger::link'),
            ('vy_tx_manager::read_views', 'vy_read_view::in_read_views'),
            ('vy_tx_manager::writers', 'vy_tx::in_writers'),
            ('vy_write_iterator::src_list', 'vy_write_src::in_src_list'),
            ('wal_writer::watchers', 'wal_watcher::next'),
            ('watchable::pending_watchers', 'watcher::in_idle_or_pending'),
            ('watchable_node::all_watchers', 'watcher::in_all_watchers'),
            ('watchable_node::idle_watchers', 'watcher::in_idle_or_pending'),
        )
        for container, entry in rlist_containers:
            container_type, _, container_field = container.rpartition('::')
            container_lists = ret.setdefault(container_type, {})
            try:
                container_lists[container_field] = TtListEntryInfo(entry)
            except Exception as e:
                logger.debug(str(e))
        return ret

    list_variables_map = build_list_variables_map()
    list_containers_map = build_list_containers_map()

    Info = namedtuple('Info', ['head', 'entry_info'])

    symbol_regexp = '<(\w+(?:\+\d+)?)>'
    symbol_re = re.compile(symbol_regexp)

    @classmethod
    def lookup_list_entry_info(cls, item):
        # Ancient gdb versions don't have 'format_string' in gdb.Value
        if hasattr(item, 'format_string'):
            address = item.format_string(address=False, symbols=True)
        else:
            address = str(item)

        symbol_match = cls.symbol_re.search(address)
        if symbol_match is None:
            return None

        symbol, _, offset = symbol_match.group(1).partition('+')
        offset = 0 if len(offset) == 0 else int(offset)

        symbol_val = gdb.parse_and_eval(symbol)
        entry_info = None
        if equal_types(symbol_val.type, TtList.rlist_type):
            entry_info = cls.list_variables_map[symbol]
        elif symbol_val.type.code == gdb.TYPE_CODE_STRUCT:
            for field in symbol_val.type.fields():
                if field.bitpos/8 == offset:
                    entry_info = cls.list_containers_map[symbol_val.type.tag][field.name]
                    break

        return entry_info

    @classmethod
    def lookup_list_info(cls, item):
        """Try to identify list head and type of list entries from its item."""
        assert equal_types(item.type.target(), TtList.rlist_type)
        item_index = -1
        item_sentinel = item['next']
        while True:
            entry_info = cls.lookup_list_entry_info(item)
            if entry_info is not None:
                return cls.Info(item, entry_info), item_index
            if item == item_sentinel:
                break
            item = item['prev']
            item_index += 1
        return None, item_index + 1

class TtList(object):
    rlist_type = gdb.lookup_type('rlist')

    @classmethod
    def resolve_item(cls, item, head=None, entry_info=None):
        assert equal_types(item.type.target(), cls.rlist_type)

        list_info, index_or_len = TtListsLut.lookup_list_info(item)
        assert index_or_len is not None, 'index_or_len is not None'
        if list_info is None:
            missing_parts = []
            if head is None:
                missing_parts.append('head')
            if entry_info is None:
                missing_parts.append('entry_info')
            if len(missing_parts) > 0:
                gdb.write("Warning: failed to identify list ({})\n".format(
                    ', '.join(missing_parts)), gdb.STDERR)
            return cls(head, entry_info, index_or_len), None

        if head is None:
            head = list_info.head
        elif head != list_info.head:
            gdb.write(
                "Warning: specified head ({}) doesn't match"
                " the predefined one ({})\n".format(head,
                                                    list_info.head))
        if entry_info is None:
            entry_info = list_info.entry_info
        elif entry_info != list_info.entry_info:
            gdb.write(
                "Warning: specified entry info ({}) doesn't match"
                " the predefined one ({})\n".format(entry_info,
                                                    list_info.entry_info))
        return cls(head, entry_info), index_or_len

    def __init__(self, head=None, entry_info=None, len=None):
        self.__head = head
        self.__entry_info = entry_info
        self.__len = len

    @property
    def head(self):
        return self.__head

    @property
    def entry_info(self):
        return self.__entry_info

    def __iter__(self):
        item = self.__head['next']
        while item != self.__head:
            yield item
            item = item['next']

    def __reversed__(self):
        item = self.__head['prev']
        while item != self.__head:
            yield item
            item = item['prev']

    def __len__(self):
        if self.__len is None:
            self.__len = sum(1 for _ in self)
        return self.__len

    def index(self, item):
        if item == self.__head:
            return None
        return next((i for i, it in enumerate(self) if it == item), None)

class TtPrintListEntryParameter(gdb.Parameter):
    name = 'print tt-list-entry'

    set_doc = 'Set printing of actual rlist entries'
    show_doc = 'Show printing of actual rlist entries'

    def __init__(self):
        super(TtPrintListEntryParameter, self).__init__(self.name,
            gdb.COMMAND_DATA, gdb.PARAM_BOOLEAN)
        self.value = True

    def get_set_string(self):
        return ''

    def get_show_string(self, svalue):
        return 'Printing of actual rlist entries is ' + svalue

TtPrintListEntryParameter()


class TtListPrinter(object):
    """
Pretty-printer for rlist

To avoid recursive printing of rlist (each entry has 'rlist' field that is used
as the entry anchor) only one instance of this printer is allowed.
This limitation is managed with '__instance_exists' class variable.

This script holds the table of 'rlist' references used in tarantool (see class
TtListsLut). When one tries to print 'rlist' first the script tries to identify
the head of the list by traversing along the list and checking against the
reference table. Once it succeed (the most likely scenario) we have both
the list head and the actual type of the list entries. Then if the specified
expression directly refers to the head of the list, the entire list is
displayed entry-by-entry. If the expression refers to the certain entry then
this only entry is displayed (along with its index in the list).
This default behavior can be altered with the class variables (see below).

predicate
  Default is 'None'.
  When it's not 'None' entries are filtered by predicate.
  See '-filter' option of 'tt-list' command

reverse
  Default is 'False'.
  When it's 'True' it forces to iterate the list in reverse direction.
  Displayed indices are affected accordingly.
  See '-reverse' option of 'tt-list' command

entry_info
  Default is 'None'.
  When it's not 'None' it overrides the one discovered with the reference
  table. It defines how to convert abstract rlist item into actual entry.
  See '-entry-info' option of 'tt-list' command

head
  Default is 'None'.
  When it's not 'None' it overrides the one discovered with the reference
  table. Special value '' (empty string) means that printed value itself
  is the head of the list.
  See '-head' option of 'tt-list' command
    """

    __instance_exists = False
    __type_uint = gdb.lookup_type('uint')

    # Initialization of config with default values is deferred so it can be
    # done in a single place to avoid duplication of default constants
    __config = None

    @classmethod
    def reset_config(cls,
                    entry_info=None,
                    head=None,
                    predicate=None,
                    reverse=False):
        cls.__config = dict(
            entry_info=entry_info,
            head=head,
            predicate=predicate,
            reverse=reverse,
        )

    def __new__(cls, val):
        # Deferred initialization of config
        if cls.__config is None:
            cls.reset_config()
        # Don't create multiple instances to avoid recursive printing
        if cls.__instance_exists:
            return None
        cls.__instance_exists = True
        return super(TtListPrinter, cls).__new__(cls)

    def __init__(self, val):
        assert self.__class__.__instance_exists
        if not equal_types(val.type, TtList.rlist_type):
            raise gdb.GdbError("expression doesn't evaluate to rlist")

        super(TtListPrinter, self).__init__()

        self.val = val

        self.print_entry = gdb.parameter(TtPrintListEntryParameter.name)

        # Pull configuration from class variables into the instance for
        # convenience
        config = self.__class__.__config
        assert config is not None
        self.entry_info = config['entry_info']
        self.head = config['head']
        self.predicate = config['predicate']
        self.reverse = config['reverse']

        # Turn entry_info into TtListEntryInfo
        if self.entry_info is not None:
            self.entry_info = TtListEntryInfo(self.entry_info)

        # Turn head into gdb.Value
        if self.head is not None:
            if self.head == '':
                self.head = val.address
            else:
                self.head = '({}*){}'.format(TtList.rlist_type.tag, self.head)
                self.head = gdb.parse_and_eval(self.head)

        # Try to identify the list item belongs to along with the item's index
        self.list, self.item_index =\
            TtList.resolve_item(val.address, self.head, self.entry_info)

    def __del__(self):
        assert self.__class__.__instance_exists
        self.__class__.__instance_exists = False

    def to_string(self):
        entry_info = '???' if self.list.entry_info is None\
            else self.list.entry_info
        s = 'rlist<{}> of length {}'.format(entry_info, len(self.list))
        if self.list.head is not None:
            s += ', head=*({}*){:#x}'.format(
                TtList.rlist_type.tag, int(self.list.head.cast(self.__type_uint)))
        return s

    def child(self, item_index, item):
        if item_index is None:
            item_index = '?'
        if not self.print_entry or self.list.entry_info is None:
            return '[{}]'.format(item_index), item
        entry_ptr = self.list.entry_info.entry_from_item(item)
        child_name = '[{}] (({}*){})'.format(item_index,
            entry_ptr.type.target().tag, entry_ptr)
        return child_name, entry_ptr.dereference()

    def children(self):
        # Display single item if it is requested
        if self.val.address != self.list.head and self.predicate is None:
            item_index = self.item_index
            if self.reverse and item_index is not None:
                item_index = len(self.list) - 1 - self.item_index
            yield self.child(item_index, self.val.address)
            return

        # Get items iterator considering direction
        items = iter(self.list) if not self.reverse else reversed(self.list)

        # Add sequence numbers
        indexed_items = enumerate(items)

        # If the head of the list is specified the entire list is iterated,
        # if the concrete item is specified iteration is started from it till
        # the end of the list (with respect to direction, see above)
        if self.item_index is not None and self.item_index > 0:
            indexed_items = slice(indexed_items, self.item_index, None)

        # Filter items with predicate, if any
        if self.predicate is not None:
            entry_info = self.list.entry_info
            entry_type = entry_info.entry_type
            # Adjustment that is not item-specific need to be applied only once
            predicate = self.predicate\
                .replace('$item', '(({}*)$item)'.format(TtList.rlist_type.tag))\
                .replace('$entry', '(({}*)$entry)'.format(entry_type.tag))
            # This function applies item-specific substitutions and is used as
            # a predicate for standard filter function. Its argument is a tuple
            # that contains item and its index
            def subst_and_eval(indexed_item):
                item_index, item = indexed_item
                return gdb.parse_and_eval(predicate\
                    .replace('$index', str(item_index))
                    .replace('$item', str(item))
                    .replace('$entry', str(entry_info.entry_from_item(item)))
                )
            indexed_items = filter(subst_and_eval, indexed_items)

        # Finally display items
        for item_index, item in indexed_items:
            yield self.child(item_index, item)

pp.add_printer('rlist', '^rlist$', TtListPrinter)


class TtListSelect(gdb.Command):
    """
tt-list
Display list entry RLIST_EXP refers to or the entire list if RLIST_EXP refers
to the list head.

Usage: tt-list [[OPTION]... --] RLIST_EXP

Options:
  -filter PREDICATE
    Filter entries with PREDICATE. It is a boolean expression similar to
    the condition expression in 'condition' or 'break' command. It is evaluated
    for each list item, which may be referred within the expression with the
    following placeholders:
      $index - item index
      $item - pointer to the item (rlist anchor)
      $entry - pointer to the actual entry
    Note, that with this option if RLIST_EXP refers to the single item the
    iteration starts from this item, i.e. items before the specified one are
    not checked. If you need to check the entire list either check in reverse
    direction as well (with -r option) or use the list head as RLIST_EXP.

  -pre EXP
  -post EXP
    Evaluate expression EXP before/after the command (see examples).

  -reverse
    Iterate list in reverse direction.

  -entry-info entry_info
    Normally entry info (entry type and anchor field) is identified
    automatically, but if failed it can be specified explicitly with
    this option, where entry_info should have format 'type::field'
    (type -- entry type, field -- anchor field)
    Please, note that the fact that you need this option indicates that most
    likely the list referenced by RLIST_EXP is missing in the internal table
    and thus the table needs to be updated.

  -head [HEAD]
    In most cases the head of the list is identified automatically, but if not
    it can be specified explicitly with this option. If HEAD argument omitted
    it means RLIST_EXP should be treated as it refers to the head.

  Any print option (see 'help print').

Examples:

# Display all engines ('engines' is a global variable)
(gdb) tt-list engines

# Display the first engine
(gdb) tt-list engines->next

# Display the engine of the name 'memtx'
(gdb) tt-list -f '$_streq($entry->name, "memtx")' -- engines

# Display engines which names start with 's'
(gdb) tt-list -f '$_memeq($entry->name, "s", 1)' -- engines

# Walk the engines (repeat the last command)
(gdb) set $i=0
(gdb) tt-list -f '$index==$i' -post '$i++' -- engines

# Walk the engines in reverse direction
(gdb) set $i=0
(gdb) tt-list -f '$index==$i' -post '$i++' -r -- engines
    """

    def __init__(self):
        super(TtListSelect, self).__init__('tt-list', gdb.COMMAND_DATA)

    def invoke(self, arg, from_tty):
        parser = argparse.ArgumentParser(add_help=False)
        parser.add_argument('-entry-info', action='store')
        parser.add_argument('-filter', action='store')
        parser.add_argument('-head', action='store',
            nargs='?', const='', default=None)
        parser.add_argument('-reverse', action='store_true')
        parser.add_argument('-pre', action='store')
        parser.add_argument('-post', action='store')

        # It is assumed that unknown arguments are for 'print' command
        args, print_args = parser.parse_known_args(gdb.string_to_argv(arg))

        # Configure TtListPrinter according to the command arguments
        # Considering that the only one instance of this printer is allowed
        # at a time it looks quite safe to pass additional parameters to the
        # printer over the class variables.
        # It works as follows:
        # 1. Initially all the mentioned class variables have default values
        #    It corresponds to default behavior of the printer as it works
        #    with 'print' command
        # 2. Setup class variables in 'tt-list' command and execute 'print'
        # 3. Restore default printer configuration, so subsequent 'print'
        #    works the same
        TtListPrinter.reset_config(
            entry_info = args.entry_info,
            predicate = args.filter,
            head = args.head,
            reverse = args.reverse,
        )

        try:
            args.pre and gdb.parse_and_eval(args.pre)
            gdb.execute('print {}'.format(' '.join(print_args)), from_tty)
            args.post and gdb.parse_and_eval(args.post)

        except Exception as e:
            raise e

        finally:
            TtListPrinter.reset_config()

TtListSelect()

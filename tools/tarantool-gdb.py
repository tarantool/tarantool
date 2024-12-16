"""
GDB extension for Tarantool post-mortem analysis.
To use, just put 'source <path-to-this-file>' in gdb.
"""

import gdb
import argparse
import base64
import logging
import struct
import itertools
import re
import sys

if sys.version_info[0] == 2:
    map = itertools.imap
    filter = itertools.ifilter
    zip = itertools.izip
elif sys.version_info[0] == 3:
    unicode = str

logger = logging.getLogger('gdb.tarantool')
logger.setLevel(logging.WARNING)

def dump_type(type):
    return 'tag={} code={} sizeof={}'.format(type.tag, type.code, type.sizeof)

def equal_types(type1, type2):
    return type1.code == type2.code and type1.tag == type2.tag

def equal_to_any_types(type, types):
    for t in types:
        if equal_types(type, t):
            return True
    return False

def int_from_address(address):
    return int(address.cast(gdb.lookup_type('uint64_t')))

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

    @classmethod
    def to_string_data(cls, data, depth, expected_type=None):
        s = str()
        mp_type = cls.typeof(data)
        if expected_type is not None and mp_type != expected_type:
            gdb.write('Invalid msgpack: unexpected type {actual_type} at pos {data_pos} (expected {expected_type})\n'.format(
                actual_type = mp_type,
                data_pos = data.pos,
                expected_type = expected_type,
            ))
            cls.next(data)
            return '!error'

        if mp_type == cls.MP_NIL:
            cls.decode_nil(data)
            s += 'null'

        elif mp_type == cls.MP_UINT:
            s += str(cls.decode_uint(data)) + 'U'

        elif mp_type == cls.MP_INT:
            s += str(cls.decode_int(data))

        elif mp_type == cls.MP_STR:
            len = cls.decode_strl(data)
            val = bytes(data.read(len))
            # MP_STR may actually contain a binary string, in which case
            # we encode the value in base64, like we do for MP_BIN.
            is_bin = False
            try:
                val = unicode(val, 'utf-8')
            except UnicodeDecodeError:
                is_bin = True
            if not is_bin:
                s += '"'
                # Escape backslash and double quotes.
                s += val.replace('\\', '\\\\').replace('"', '\\"')
                s += '"'
            else:
                s += 'str:'
                s += unicode(base64.b64encode(val), 'utf-8')

        elif mp_type == cls.MP_BIN:
            len = cls.decode_binl(data)
            val = bytes(data.read(len))
            s += 'bin:'
            s += unicode(base64.b64encode(val), 'utf-8')

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
    MP_TUPLE = find_value('MP_TUPLE')

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
    def to_string_tuple(cls, data, depth):
        format_id = cls.to_string_data(data, depth, cls.MP_UINT)
        payload = cls.to_string_data(data, depth, cls.MP_ARRAY)
        return 'tuple(format_id={}):{}'.format(format_id, payload)

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
        elif ext_type == cls.MP_TUPLE:
            s = cls.to_string_tuple(data, depth)
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


class ContainerFieldInfo(object):
    @classmethod
    def __find_field(cls, container_type, field_name):
        assert field_name, "unexpected empty field name"
        if gdb.types.has_field(container_type, field_name):
            field = container_type[field_name]
            return field, field.bitpos // 8
        for f in container_type.fields():
            if not f.name:
                field, offset = cls.__find_field(f.type, field_name)
                if field:
                    return field, f.bitpos // 8 + offset
        return None

    def __init__(self, field_info):
        self.__str = field_info
        container_type, _, fields = field_info.partition('::')
        self.__container_type = gdb.lookup_type('struct ' + container_type)
        self.__offset = 0
        container_type = self.__container_type
        for field_name in fields.split('::'):
            field, offset = self.__find_field(container_type, field_name)
            assert field, "field {} is missing unexpectedly".format(field_name)
            self.__offset += offset
            container_type = field.type

    def __eq__(self, other):
        return self.__str == other.__str

    def __hash__(self):
        return hash(str(self))

    def __str__(self):
        return self.__str

    @property
    def offset(self):
        return self.__offset

    @property
    def container_type(self):
        return self.__container_type

    def container_from_field(self, field_ptr):
        return cast_ptr(self.__container_type, field_ptr, -self.__offset)


class ListLut(object):
    @staticmethod
    def __build_symbols_map(symbols):
        ret = {}
        for sym, entry in symbols:
            try:
                ret[sym] = ContainerFieldInfo(entry)
            except Exception as e:
                logger.debug(str(e))
        return ret

    @staticmethod
    def __build_containers_map(containers):
        ret = {}
        for container, entry in containers:
            try:
                field_info = ContainerFieldInfo(container)
                container_entries = ret.setdefault(field_info.container_type.tag, {})
                container_entries[field_info.offset] = ContainerFieldInfo(entry)
            except Exception as e:
                logger.debug(str(e))
        return ret

    @classmethod
    def _init(cls):
        if not hasattr(cls, '_symbols_map'):
            cls._symbols_map = cls.__build_symbols_map(cls._symbols)
        if not hasattr(cls, '_containers_map'):
            cls._containers_map = cls.__build_containers_map(cls._containers)

    __symbol_re = re.compile('(\w+)(?:\s*\+\s*(\d+))?')

    @classmethod
    def lookup_entry_info(cls, address):
        """Try to identify the type of the list entries by the list head."""
        cls._init()

        address = int_from_address(address)
        symbol_info = gdb.execute('info symbol {:#x}'.format(address), False, True)
        if symbol_info.startswith('No symbol matches'):
            return None

        symbol_match = cls.__symbol_re.match(symbol_info)
        if symbol_match is None:
            logger.warning("Symbol is missing in '{}'".format(symbol_info))
            return None

        symbol = symbol_match.group(1)
        offset = int(symbol_match.group(2)) if symbol_match.lastindex == 2 else 0

        symbol_val = gdb.parse_and_eval(symbol)
        entry_info = None
        if equal_types(symbol_val.type, cls._list_type):
            entry_info = cls._symbols_map.get(symbol)
        elif symbol_val.type.code == gdb.TYPE_CODE_STRUCT:
            container = cls._containers_map.get(symbol_val.type.tag)
            entry_info = container.get(offset) if container is not None else None

        return entry_info

    @classmethod
    def lookup_entry_info_by_container(cls, container_info):
        cls._init()
        container = cls._containers_map.get(container_info.container_type.tag)
        return container.get(container_info.offset) if container is not None else None


class Rlist(object):
    gdb_type = gdb.lookup_type('rlist')
    item_gdb_type = gdb_type

    @classmethod
    def lookup_head(cls, item):
        # Try to identify the list head by the list item
        assert item.type.code == gdb.TYPE_CODE_PTR,\
            'lookup_head: unexpected item type (code={})'.format(item.type.code)
        assert equal_types(item.type.target(), cls.gdb_type),\
            'lookup_head: unexpected item type ({})'.format(dump_type(item.type.target()))
        entry_info = RlistLut.lookup_entry_info(item)
        if entry_info is not None:
            return item
        items = cls(item)
        for item in reversed(items):
            entry_info = RlistLut.lookup_entry_info(item)
            if entry_info is not None:
                return item
        return None

    @classmethod
    def len(cls, rlist):
        return len(cls(rlist))

    def __init__(self, val, is_item=False):
        self.__val = val
        self.__is_item = is_item
        self.__len = None

    @property
    def address(self):
        return None if self.__is_item else self.__val

    def title(self, entry_info):
        return "{list_type}<{entry_info}> of length {list_len}".format(
            list_type = self.gdb_type.tag,
            entry_info = str(entry_info) if entry_info is not None else '?',
            list_len = len(self),
        )

    def __iter__(self):
        item = self.__val if self.__is_item else self.__val['next']
        stop_item = item['prev']
        while item != stop_item:
            yield item
            item = item['next']

    def __reversed__(self):
        item = self.__val if self.__is_item else self.__val['prev']
        stop_item = item['next']
        while item != stop_item:
            yield item
            item = item['prev']

    def __len__(self):
        if self.__len is None:
            self.__len = sum(1 for _ in self)
        return self.__len

class RlistLut(ListLut):
    _list_type = Rlist.gdb_type
    _symbols = (
        ('box_on_call', 'trigger::link'),
        ('box_on_select', 'trigger::link'),
        ('box_on_shutdown_trigger_list', 'trigger::link'),
        ('box_raft_on_broadcast', 'trigger::link'),
        ('engines', 'engine::link'),
        ('log_rotate_list', 'log::in_log_list'),
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
        ('txns', 'txn::in_txns'),
    )
    _containers = (
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
        ('vy_range::slices', 'vy_slice::in_range'),
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


class Stailq(object):
    gdb_type = gdb.lookup_type('stailq')
    item_gdb_type = gdb.lookup_type('stailq_entry')
    entry_ptr_gdb_type = find_type('stailq_entry_ptr')

    if entry_ptr_gdb_type is None:
        @staticmethod
        def __entry(val):
            return val
    else:
        @staticmethod
        def __entry(val):
            return val['value']

    def __init__(self, val):
        if equal_types(val.type.target(), self.gdb_type):
            self.__address = val
            self.__start_entry = self.__entry(val['first'])
        elif equal_types(val.type.target(), self.item_gdb_type):
            self.__address = None
            self.__start_entry = val
        else:
            raise gdb.GdbError("unexpected type: {}".format(dump_type(val.type.target())))
        self.__len = None

    @property
    def address(self):
        return self.__address

    def title(self, entry_info):
        if self.address is not None:
            length = "of length {}".format(len(self))
        else:
            length = "of unknown length (at least {})".format(len(self))
        return "{list_type}<{entry_info}> {length}".format(
            list_type = self.gdb_type.tag,
            entry_info = str(entry_info) if entry_info is not None else '?',
            length = length,
        )

    def __iter__(self):
        entry = self.__start_entry
        while entry != 0:
            yield entry
            entry = self.__entry(entry['next'])

    def __reversed__(self):
        entries = list(self)
        for entry in entries[::-1]:
            yield entry

    def __len__(self):
        if self.__len is None:
            self.__len = sum(1 for _ in self)
        return self.__len


class StailqLut(ListLut):
    _list_type = Stailq.gdb_type
    _symbols = (
        ('swim_task_pool', 'swim_task::in_pool'),
        ('txn_cache', 'txn::in_txn_cache'),
    )
    _containers = (
        ('applier_data_msg::txs', 'applier_tx::next'),
        ('applier_tx::rows', 'applier_tx_row::next'),
        ('cbus_endpoint::output', 'cmsg_poison::msg::fifo'),
        ('cpipe::input', 'cmsg::fifo'),
        ('fiber_pool::output', 'cmsg::fifo'),
        ('iproto_stream::pending_requests', 'iproto_msg::in_stream'),
        ('MemtxAllocator<Allocator>::gc', 'memtx_tuple::in_gc'),
        ('memtx_engine::gc_queue', 'memtx_gc_task::link'),
        ('memtx_tuple_rv_list::tuples', 'memtx_tuple::in_gc'),
        ('relay::pending_gc', 'relay_gc_msg::in_pending'),
        ('swim::event_queue', 'swim_member::in_event_queue'),
        ('txn::stmts', 'txn_stmt::next'),
        ('Vdbe::autoinc_id_list', 'autoinc_id_entry::link'),
        ('vy_log_tx::records', 'vy_log_record::in_tx'),
        ('vy_log::pending_tx', 'vy_log_tx::in_pending'),
        ('vy_scheduler::processed_tasks', 'vy_task::in_processed'),
        ('vy_tx::log', 'txv::next_in_log'),
        ('vy_worker_pool::idle_workers', 'vy_worker::in_idle'),
        ('wal_msg::commit', 'journal_entry::fifo'),
        ('wal_msg::rollback', 'journal_entry::fifo'),
        ('wal_writer::rollback', 'journal_entry::fifo'),
        ('xrow_update_field::map::items', 'xrow_update_map_item::in_items'),
    )


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


class FieldsPrinter(gdb.printing.PrettyPrinter):
    class FieldsSubprinter(gdb.printing.SubPrettyPrinter):
        def __init__(self, val, fields):
            super(FieldsPrinter.FieldsSubprinter, self).__init__(val.type.tag)
            self.val = val
            self.fields = fields

        def children(self):
            for field in self.fields:
                yield field, self.val[field]

    container_type = None
    fields = None

    def __init__(self):
        super(FieldsPrinter, self).__init__("FieldsPrinter")

    def __call__(self, val):
        if self.container_type is None or not equal_types(val.type, self.container_type):
            return None
        return self.FieldsSubprinter(val, self.fields)


class TtListPrinter(object):
    """
Pretty-printer for rlist

To avoid recursive printing of rlist (each entry has 'rlist' field that is used
as the entry anchor) only one instance of this printer is allowed.
This limitation is managed with '__instance_exists' class variable.

This script holds the table of predefined 'rlist' references used in tarantool
(see class ListLut). When one tries to print 'rlist' first the script tries to
identify the head of the list by traversing along the list and checking against
the reference table. Once it succeed we have both the list head and the actual
type of the list entries. Then if the specified expression directly refers to
the head of the list, the entire list is displayed entry-by-entry. If
the expression refers to the certain entry then this only entry is displayed
(along with its index in the list).
This default behavior can be altered with the class variables (see below).

walk_mode
  Default is 'False'.
  If 'True' printer takes the next step in list walking.

predicate
  Default is 'None'.
  When it's not 'None' entries are filtered by predicate.
  See '-predicate' option of 'tt-list' command

fields
  Default is 'None'.
  By default all entry's fields are printed, but it can be restricted with this
  variable (string of comma separated fields).
  See '-fields' option of 'tt-list' command

reverse
  Default is 'False'.
  When it's 'True' it forces to iterate the list in reverse direction.
  Displayed indices are affected accordingly.
  See '-reverse' option of 'tt-list' command

entry_info
  Default is 'None'.
  When it's not 'None' it overrides the one discovered with the predefined
  reference table. It defines how to convert abstract rlist item into actual
  entry.
  See '-entry-info' option of 'tt-list' command

head
  Default is 'None'.
  When it's not 'None' it overrides the one discovered with the predefined
  reference table.
  See '-head' option of 'tt-list' command

is_item
  Default is 'False'
  'True' means that printed value refers to the item of the list, rather than
  to the list itself.
  See '-item' option of 'tt-list' command

from_tt_list
  Default is 'False'.
  It is set to 'True' when the printer is used from the 'tt-list' command
    """

    entry_printer = FieldsPrinter()
    gdb.printing.register_pretty_printer(gdb.current_objfile(), entry_printer, True)

    __instance_exists = False

    class WalkOrigin(object):
        def __init__(self, val, is_item, head, reverse, predicate):
            self.val = val
            self.is_item = is_item
            self.head = head
            self.reverse = reverse
            self.predicate = predicate

        def __eq__(self, other):
            return other is not None and \
                self.val == other.val and \
                self.is_item == other.is_item and \
                self.head == other.head and \
                self.reverse == other.reverse and \
                self.predicate == other.predicate and \
                True

    # Walk state
    __walk_origin = None
    __walk_items = None
    __walk_next_item = None
    __walk_entry_info = None

    @classmethod
    def __set_walk_origin(cls, origin):
        cls.__walk_origin = origin
        cls.__set_walk_data(None, None)

    @classmethod
    def __set_walk_data(cls, items, entry_info):
        cls.__walk_items = items
        cls.__walk_next_item = None
        cls.__walk_entry_info = entry_info

    @classmethod
    def __walk_next(cls):
        if cls.__walk_items is not None:
            cls.__walk_next_item = next(cls.__walk_items)

    @classmethod
    def reset_walk(cls):
        cls.__set_walk_origin(None)

    # Initialization of config with default values is deferred so it can be
    # done in a single place (namely in 'reset_config') to avoid duplication
    # of default constants
    __config = None

    @classmethod
    def reset_config(cls,
                    walk_mode=False,
                    entry_info=None,
                    head=None,
                    is_item=False,
                    predicate=None,
                    fields=None,
                    reverse=False,
                    from_tt_list=False,
                    print_args=None,
                ):
        cls.__config = dict(
            walk_mode=walk_mode,
            entry_info=entry_info,
            head=head,
            is_item=is_item,
            predicate=predicate,
            fields=fields,
            reverse=reverse,
            from_tt_list=from_tt_list,
            print_args=print_args,
        )
        if walk_mode:
            # Some walking manipulations
            val = gdb.parse_and_eval(cls.get_print_exp(print_args))
            walk_origin = cls.WalkOrigin(
                val.address,
                is_item,
                head,
                reverse,
                predicate,
            )
            if walk_origin == cls.__walk_origin:
                cls.__walk_next()
            else:
                cls.__set_walk_origin(walk_origin)

    def __new__(cls, val):
        # Deferred initialization of config
        if cls.__config is None:
            cls.reset_config()
        # Don't create multiple instances to avoid recursive printing
        if cls.__instance_exists:
            return None
        cls.__instance_exists = True
        return super(TtListPrinter, cls).__new__(cls)

    @staticmethod
    def resolve_value_with_predefined(value_title, value, predefined):
        if value is None:
            return predefined
        if predefined is not None and value != predefined:
            gdb.write(
                "Warning: the predefined {value_title} ({predefined})"
                " doesn't match the specified one ({value})\n"
                "The latter will be used, but please check,"
                " either one or both are incorrect.\n".format(
                        value_title=value_title,
                        value=value,
                        predefined=predefined,
                    )
            )
        return value

    @staticmethod
    def get_print_exp(print_args):
        if '--' in print_args:
            exp_args = print_args[print_args.index('--')+1:]
        else:
            exp_args = print_args
        return ' '.join(exp_args)

    @staticmethod
    def lookup_entry_info_from_list_exp(lut, list_exp):
        def split_last_field(expr):
            arrow_pos = expr.rfind('->')
            dot_pos = expr.rfind('.')
            field_sep = '->' if arrow_pos > dot_pos else '.'
            container, _, field = expr.rpartition(field_sep)
            return container, field

        container, field = split_last_field(list_exp)
        if container:
            container = gdb.parse_and_eval(container)
            if container.type.code == gdb.TYPE_CODE_PTR:
                container = container.dereference()
            container_info = '{}::{}'.format(container.type.tag, field)
            container_info = ContainerFieldInfo(container_info)

        return lut.lookup_entry_info_by_container(container_info)

    @classmethod
    def resolve_entry_info(cls, config, lut, lst):
        # Turn configured entry_info (if any) into ContainerFieldInfo
        entry_info_config = config['entry_info']
        if entry_info_config is not None:
            entry_info_config = ContainerFieldInfo(entry_info_config)

        # Try to find predefined entry info for the list
        entry_info_lut = None
        if lst.address is not None:
            entry_info_lut = lut.lookup_entry_info(lst.address)

        # Check if the configured entry info conflicts with the predefined one
        entry_info = cls.resolve_value_with_predefined(
            'entry info',
            entry_info_config,
            entry_info_lut,
        )

        # If still no entry info, then the last chance to identify it is
        # parsing the list expression that was specified in 'print' command
        if entry_info is None:
            print_args = config['print_args']
            head_exp = config['head']
            list_exp = head_exp if head_exp is not None else \
                    cls.get_print_exp(print_args) if print_args is not None else \
                    None
            if list_exp is not None:
                entry_info = cls.lookup_entry_info_from_list_exp(lut, list_exp)

        # Display hint if failed to identify the type of the list entries
        if entry_info is None:
            msg = "Warning: failed to identify the type of the list entries.\n"
            if config['from_tt_list']:
                msg += "Please, specify entry info explicitly with -e option.\n"
            else:
                msg += "Please, try 'tt-list' or 'tt-list-walk' command.\n"
            gdb.write("\n" + msg + "\n", gdb.STDERR)

        return entry_info

    @staticmethod
    def prepare_list_title(list, entry_info):
        s = list.title(entry_info)
        if list.address is not None:
            s += ", ref=*({}*){:#x}".format(
                list.gdb_type.tag,
                int_from_address(list.address),
            )
        return s

    @staticmethod
    def prepare_sequence(config, lst, val, entry_info):
        def create_predicate(gdb_condition, entry_info):
            substitutions = (
                ('$index', lambda item: str(item[0])),
                ('$item', lambda item: '(({}*){:#x})'.format(
                                lst.item_gdb_type.tag,
                                int_from_address(item[1])
                            )),
                ('$entry', lambda item: '(({}*){:#x})'.format(
                                entry_info.container_type.tag,
                                int_from_address(entry_info.container_from_field(item[1]))
                            )),
            )
            substitutions = filter(lambda s: gdb_condition.find(s[0]) != -1, substitutions)
            substitutions = dict(substitutions)

            # Check that the predicate expression can be evaluated
            if '$index' in substitutions and lst.address is None:
                raise gdb.GdbError("\n"
                    "Can't use $index placeholder when the head of the list is unknown.\n"
                    "Please, use -h option to specify it explicitly if it is not identified automatically.\n"
                    "\n"
                )
            if '$entry' in substitutions and entry_info is None:
                raise gdb.GdbError("\n"
                    "Can't use $entry placeholder when the type of entry is unknown.\n"
                    "Please, use -e option to specify it explicitly if it is not identified automatically.\n"
                    "\n"
                )

            # This function applies item-specific substitutions and is used as
            # a predicate for standard filter function. Its argument is a tuple
            # that contains item and its index
            def predicate(item):
                item_condition = gdb_condition
                for placeholder in substitutions.keys():
                    item_condition = item_condition.replace(
                        placeholder, substitutions[placeholder](item))
                return gdb.parse_and_eval(item_condition)
            return predicate

        # Prepare items and indices
        if config['reverse']:
            items = reversed(lst)
            indices = itertools.count(len(lst)-1, -1)
        else:
            items = iter(lst)
            indices = itertools.count()
        # Decorate indices with '?' if the head of the list is unknown
        if lst.address is None:
            indices = map(lambda index: '?+{}'.format(index) if index > 0 else '?', indices)
        # Link them
        items = zip(indices, items)

        # If the head of the list is specified the entire list is considered,
        # if the concrete item is specified only items started with the specified
        # one is considered (up to the specified one in case of reverse direction)
        if not equal_types(val.type, lst.gdb_type) or val.address != lst.address:
            items = itertools.dropwhile(lambda item: item[1] != val.address, items)

        # Deal with predicate
        if config['predicate'] is not None:
            # Create python function that could be used as a filter predicate
            predicate = create_predicate(
                config['predicate'],
                entry_info,
            )
            # Filter items with predicate
            items = filter(predicate, items)

        return items

    def __init__(self, val):
        assert self.__class__.__instance_exists, "__instance_exists must be True"
        assert equal_to_any_types(val.type, (
                Rlist.gdb_type,
                Stailq.gdb_type,
                Stailq.item_gdb_type,
                Stailq.entry_ptr_gdb_type,
            )), "expression doesn't refer to list (type: {})".format(dump_type(val.type))

        super(TtListPrinter, self).__init__()

        config = self.__class__.__config

        self.val = val
        self.title = None
        self.items = None
        self.entry_info = None

        if config['walk_mode'] and self.__class__.__walk_next_item is not None:
            # This means that walk state has been set up already and now we are
            # moving through the items
            self.items = [self.__class__.__walk_next_item]
            self.entry_info = self.__class__.__walk_entry_info
            return

        # Turn configured head (if any) into gdb.Value
        head = config['head']
        if head is not None:
            head = gdb.parse_and_eval(head)
            if head.type.code == gdb.TYPE_CODE_INT:
                if equal_to_any_types(val.type, (Rlist.gdb_type, Stailq.gdb_type)):
                    head_type = val.type.pointer()
                elif equal_to_any_types(val.type, (Stailq.item_gdb_type, Stailq.entry_ptr_gdb_type)):
                    head_type = Stailq.gdb_type.pointer()
                else:
                    raise gdb.GdbError("unexpected type: {}".format(dump_type(val.type)))
                head = head.cast(head_type)
            elif equal_to_any_types(head.type, (Rlist.gdb_type, Stailq.gdb_type)):
                head = head.address
            else:
                raise gdb.GdbError("unexpected head type {}".format(dump_type(head.type)))

        # Create corresponding python iterable
        lst = None
        if equal_types(val.type, Rlist.gdb_type):
            lut = RlistLut
            # Try to find the head of the list
            head_lut = Rlist.lookup_head(val.address)
            head = self.resolve_value_with_predefined(
                'list head',
                head,
                head_lut,
            )
            if head is not None:
                lst = Rlist(head)
            else:
                lst = Rlist(val.address, config['is_item'])

        elif equal_types(val.type, Stailq.gdb_type):
            if head is not None and head != val.address:
                raise gdb.GdbError("Inconsistent arguments: '-head' is to be used"
                                " only when LIST_EXP refers to the single"
                                " list item rather than the list itself.")
            lut = StailqLut
            lst = Stailq(val.address)

        elif equal_to_any_types(val.type, (Stailq.item_gdb_type, Stailq.entry_ptr_gdb_type)):
            if equal_types(val.type, Stailq.entry_ptr_gdb_type):
                self.val = val['value'].dereference()
            lut = StailqLut
            lst = Stailq(head if head is not None else self.val.address)

        else:
            raise gdb.GdbError("TtListPrinter.__init__: "
                "unreachable code: unexpected type {}".format(dump_type(val.type)))

        # Deal with entry_info
        self.entry_info = self.resolve_entry_info(config, lut, lst)

        # Prepare title of the list
        self.title = self.prepare_list_title(lst, self.entry_info)

        # Prepare items sequence
        self.items = self.prepare_sequence(config, lst, self.val, self.entry_info)

        if config['walk_mode']:
            # This means that this is the first 'walk' invocation so we need
            # to setup 'walk' state
            self.__class__.__set_walk_data(self.items, self.entry_info)
            self.items = []

    def __del__(self):
        assert self.__class__.__instance_exists
        self.__class__.__instance_exists = False

    def to_string(self):
        return self.title

    @staticmethod
    def child(entry_info, item):
        item_index, item_ptr = item
        if entry_info is None:
            return '[{}]'.format(item_index), item_ptr
        entry_ptr = entry_info.container_from_field(item_ptr)
        child_name = '[{}] (({}*){})'.format(item_index,
            entry_ptr.type.target().tag, entry_ptr)
        return child_name, entry_ptr.dereference()

    def children(self):
        print_entry = gdb.parameter(TtPrintListEntryParameter.name)
        entry_info = self.entry_info if print_entry else None

        # Setup entry printer (if required)
        config_fields = self.__config['fields']
        if entry_info is not None and config_fields is not None:
            self.entry_printer.container_type = entry_info.container_type
            self.entry_printer.fields = config_fields.split(',')
            self.entry_printer.enabled = True

        # Items already prepared, just yield'em all
        for item in self.items:
            yield self.child(entry_info, item)

        self.entry_printer.enabled = False

pp.add_printer('rlist', '^rlist$', TtListPrinter)
pp.add_printer('stailq', '^stailq$', TtListPrinter)
pp.add_printer('stailq_entry', '^stailq_entry$', TtListPrinter)
if Stailq.entry_ptr_gdb_type:
    pp.add_printer('stailq_entry_ptr', '^stailq_entry_ptr$', TtListPrinter)


def create_list_argument_parser():
    """
Create parser that knows the arguments used both in tt-list and
tt-list-walk commands
    """
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument('-entry-info', action='store')
    parser.add_argument('-predicate', action='store')
    parser.add_argument('-fields', action='store')
    parser.add_argument('-head', action='store')
    parser.add_argument('-item', action='store_true')
    parser.add_argument('-reverse', action='store_true')
    return parser


class TtListSelect(gdb.Command):
    """
tt-list
Display entries of the LIST_EXP.
If LIST_EXP refers to the concrete item rather than the entire list, only items
started with the specified one are displayed (up to the specified one in case
of reverse direction).

Usage: tt-list [[OPTION]... --] LIST_EXP

Options:
  -predicate PREDICATE
    Filter entries with PREDICATE. It is a boolean expression similar to
    the condition expression in 'condition' or 'break' command. It is evaluated
    for each list item, which may be referred within the expression with the
    following placeholders:
      $index - item index
      $item - pointer to the item
      $entry - pointer to the actual entry
    Note, that with this option if LIST_EXP refers to the single item the
    iteration starts from this item, i.e. items before the specified one are
    not checked. If you need to check the entire list either check in reverse
    direction as well (with -r option) or use the list head as LIST_EXP.

  -fields FIELDS
    Comma separated entry fields that should be printed.
    If omitted then all fields are printed.

  -reverse
    Iterate list in reverse direction.

  -entry-info entry_info
    Normally entry info (entry type and anchor field) is identified
    automatically, but if failed it can be specified explicitly with
    this option, where entry_info should have format 'type::field'
    (type -- entry type, field -- anchor field)
    Please, note that the fact that you need this option might indicate
    that the list referenced by LIST_EXP is missing in the predefined table
    and need to be checked.

  -head HEAD
    If the head of the list is not detected automatically, it should be
    specified explicitly with this option.

  -item
    Explicitly specify that LIST_EXP refers to the item of the list, rather
    than the list itself. Normally it is identified automatically, but in some
    cases there maybe lack of information to figure it out and this option
    might help.

  Any print option (see 'help print').

Examples:

# Display all engines ('engines' is a global variable)
(gdb) tt-list engines

# Display the first engine
(gdb) tt-list engines.next

# Display the engine of the name 'memtx'
(gdb) tt-list -p '$_streq($entry->name, "memtx")' -- engines

# Display engines which names start with 's'
(gdb) tt-list -p '$_memeq($entry->name, "s", 1)' -- engines

# Display fibers ids and names
(gdb) tt-list -pretty -fields fid,name -- main_cord.alive
    """

    cmd_name = 'tt-list'

    def __init__(self):
        super(TtListSelect, self).__init__(self.cmd_name, gdb.COMMAND_DATA)

    def invoke(self, arg, from_tty):
        parser = create_list_argument_parser()

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
            walk_mode = False,
            entry_info = args.entry_info,
            predicate = args.predicate,
            fields = args.fields,
            head = args.head,
            is_item = args.item,
            reverse = args.reverse,
            from_tt_list = True,
            print_args = print_args,
        )

        try:
            gdb.execute('print {}'.format(' '.join(print_args)), from_tty)

        except Exception as e:
            raise e

        finally:
            TtListPrinter.reset_config()

TtListSelect()


class TtListWalk(gdb.Command):
    """
tt-list-walk
This command implements 'walk' functionality, that is it iterates
through the list but unlike 'tt-list' it displays one entry per
invocation (sometimes it may be more convenient to see entries
one-by-one instead of all-at-once). To move through the list you just
need to repeat continuously the last command with the same arguments
(that is just enter blank line) until the list is exhausted and message
'No items left' is displayed.
There can be only one active walk at a time. Once LIST_EXP or any option
that affects entries sequence is changed, current active walk is dropped
and the new one is started automatically.

Usage: tt-list-walk [[OPTION]... --] LIST_EXP

Options:
  Any option of 'tt-list' (see 'help tt-list')

  -new
    Start new walk.
    Note, that the new walk starts automatically if any of the options
    that affects sequence or LIST_EXP itself is changed. This option is
    to be used if you need to restart the walk on the same sequence.

Examples (repeat the command until the list is exhausted):

# Walk through the engines
(gdb) tt-list-walk engines

# Walk through the fibers with 'fid' greater than 110
(gdb) tt-list-walk -p '$entry->fid > 110' -- main_cord.alive
    """

    cmd_name = 'tt-list-walk'

    def __init__(self):
        super(TtListWalk, self).__init__(self.cmd_name, gdb.COMMAND_DATA)

    def invoke(self, arg, from_tty):
        parser = create_list_argument_parser()
        parser.add_argument('-new', action='store_true')

        # It is assumed that unknown arguments are for 'print' command
        args, print_args = parser.parse_known_args(gdb.string_to_argv(arg))

        args.new and TtListPrinter.reset_walk()

        try:
            TtListPrinter.reset_config(
                walk_mode = True,
                entry_info = args.entry_info,
                predicate = args.predicate,
                fields = args.fields,
                head = args.head,
                is_item = args.item,
                reverse = args.reverse,
                from_tt_list = True,
                print_args = print_args,
            )
            gdb.execute('print {}'.format(' '.join(print_args)))

        except StopIteration:
            TtListPrinter.reset_walk()
            raise gdb.GdbError("No items left.")

        finally:
            TtListPrinter.reset_config()

TtListWalk()


def cord():
    return gdb.parse_and_eval('cord_ptr')

def fiber():
    return cord()['fiber']


class Cord(object):
    __main_cord_fibers = gdb.parse_and_eval('main_cord.alive')
    __list_entry_info = RlistLut.lookup_entry_info(__main_cord_fibers.address)

    def __init__(self):
        self.__cord_ptr = cord()

    def fibers(self):
        fibers = self.__cord_ptr['alive']
        fibers = Rlist(fibers.address)
        fibers = map(lambda x: self.__class__.__list_entry_info.container_from_field(x), fibers)
        return itertools.chain(fibers, [self.__cord_ptr['sched'].address])

    def fiber(self, fid):
        return next((f for f in self.fibers() if f['fid'] == fid), None)


try:
    import gdb.unwinder
    support_unwinders = True

except ImportError as e:
    support_unwinders = False
    msg_cant_explore_fiber_stack = "Exploring stack of non-current fiber is not supported with this version of GDB."
    gdb.write("WARNING: " + msg_cant_explore_fiber_stack + '\n')


if support_unwinders:

    class FiberUnwinderFrameFilter(object):
        def __init__(self):
            self.name = "TtFiberUnwinderFrameFilter"
            self.priority = 100
            self.enabled = True
            self.skip_frame_sp = None
            gdb.current_progspace().frame_filters[self.name] = self

        def filter(self, frame_iter):
            if self.skip_frame_sp is None:
                return frame_iter
            reg, val = self.skip_frame_sp
            return filter(lambda f: f.inferior_frame().read_register(reg) != val, frame_iter)


    class FrameId(object):
        def __init__(self, sp, pc):
            self.sp = sp
            self.pc = pc

    class FiberUnwinder(gdb.unwinder.Unwinder):
        __instance = None

        @classmethod
        def instance(cls):
            if cls.__instance is None:
                cls.__instance = cls()
            return cls.__instance

        def __init__(self):
            super(FiberUnwinder, self).__init__('TtFiberUnwinder')
            self.reset_fiber()
            self.__frame_filter = None

            # Initialize architecture specific parameters of coro context
            arch = gdb.selected_inferior().architecture().name().lower()
            if arch.find('x86-64') != -1:
                self.__coro_ctx_regs = [
                    'r15',
                    'r14',
                    'r13',
                    'r12',
                    'rbx',
                    'rbp',
                    'rip',
                ]
                self.__ctx_offs_lr = self.__coro_ctx_regs.index('rip')
                self.__reg_pc = 'rip'
                self.__reg_sp = 'rsp'

            elif arch.find('aarch64') != -1:
                self.__coro_ctx_regs = [
                    'x19', 'x20',
                    'x21', 'x22',
                    'x23', 'x24',
                    'x25', 'x26',
                    'x27', 'x28',
                    'x29', 'x30',
                    'd8',  'd9',
                    'd10', 'd11',
                    'd12', 'd13',
                    'd14', 'd15',
                ]
                self.__ctx_offs_lr = self.__coro_ctx_regs.index('x30')
                self.__reg_pc = 'pc'
                self.__reg_sp = 'sp'

            else:
                raise gdb.GdbError("FiberUnwinder: architecture '{}' is not supported".format(arch))

            if support_unwinders:
                gdb.unwinder.register_unwinder(gdb.current_progspace(), self, True)

        def fiber(self):
            return self.__fiber if self.__cord == cord() else fiber()

        def set_fiber(self, f):
            self.__fiber = f
            self.__cord = cord()

        def reset_fiber(self):
            self.set_fiber(fiber())

        def __call__(self, pending_frame):
            # Reset fiber if the unwinder is called due to thread (and hence cord) switching
            if self.__cord != cord():
                self.reset_fiber()

            # Make sure unwinder frame filter is initialized
            if self.__frame_filter is None:
                self.__frame_filter = FiberUnwinderFrameFilter()

            # For the currently running fiber use the default unwinder and don't filter frames
            if self.fiber() == fiber():
                self.__frame_filter.skip_frame_sp = None
                return None

            orig_sp = pending_frame.read_register(self.__reg_sp)
            orig_pc = pending_frame.read_register(self.__reg_pc)

            # Register 'sp' is used to identify that the outermost frame of the fiber has been
            # injected already. After that proceed with the default unwinder.
            reg_sp_exp = '${}'.format(self.__reg_sp)
            if orig_sp != gdb.parse_and_eval(reg_sp_exp):
                return None

            # Frame matching actual stack pointer should be skipped as it refers to
            # the running fiber
            self.__frame_filter.skip_frame_sp = (self.__reg_sp, orig_sp)

            # Get fiber stack
            sp = self.fiber()['ctx']['sp']

            # Create UnwindInfo. Usually the frame is identified by the stack
            # pointer and the program counter.
            unwind_info = pending_frame.create_unwind_info(FrameId(sp, orig_pc))

            # Find the values of the registers in the caller's frame and
            # save them in the result:
            pc = (sp + self.__ctx_offs_lr).dereference()
            for reg in self.__coro_ctx_regs:
                unwind_info.add_saved_register(reg, sp.dereference())
                sp += 1
            unwind_info.add_saved_register(self.__reg_pc, pc)
            unwind_info.add_saved_register(self.__reg_sp, sp)

            # Return the result:
            return unwind_info


class FibersInfo(gdb.Command):
    """Display currently known tarantool fibers of the current cord.
Usage: info tt-fibers [ID]...
If ID is given, it is a space-separated list of IDs of fibers to display.
Otherwise, all fibers of the current cord are displayed."""

    def __init__(self):
        super(FibersInfo, self).__init__("info tt-fibers", gdb.COMMAND_STATUS)

    def invoke(self, arg, from_tty):
        # Prepare sequence of fibers
        fibers = Cord().fibers()
        ids = arg.split()
        if len(ids) > 0:
            ids = set(map(lambda x: int(x), ids))
            fibers = filter(lambda f: int(f['fid']) in ids, fibers)

        fiber_cur = fiber()
        fiber_to_unwind = FiberUnwinder.instance().fiber() if support_unwinders else fiber_cur
        gdb.write('{marker_cur} {id:6} {target:8} {name:32} {marker_unwind} {stack:18} {func}\n'.format(
            marker_cur=' ',
            marker_unwind=' ',
            id='Id',
            target='Target',
            name='Name',
            stack='Stack',
            func='Function',
        ))
        for f in fibers:
            gdb.write('{marker_cur} {id:6} {target:8} {name:32} {marker_unwind} {stack:18} {func}\n'.format(
                marker_cur='*' if f == fiber_cur else ' ',
                marker_unwind='*' if f == fiber_to_unwind else ' ',
                id=str(f['fid']),
                target='TtFiber',
                name='"' + f['name'].string() + '"',
                stack=str(f['ctx']['sp']),
                func=f['f'],
            ))

FibersInfo()


class Fiber(gdb.Command):
    """tt-fiber [FIBER_ID]
Use this command to select fiber which stack need to be explored.
FIBER_ID must be currently known, if omitted displays current fiber info.

Please, be aware that the outermost frame (level #0) is filtered out for
any fiber other than the one that is currently running, so backtrace in
this case starts with level #1 frame. It's because GDB always starts frame
sequence with a frame that matches actual value of the stack pointer register,
but this frame only makes sense for the currently running fiber.

Please, note that this command does NOT change currently running fiber,
it just selects stack to explore w/o changing any register/data."""

    def __init__(self):
        super(Fiber, self).__init__("tt-fiber", gdb.COMMAND_RUNNING)

    def invoke(self, arg, from_tty):
        if not support_unwinders:
            raise gdb.GdbError(msg_cant_explore_fiber_stack)

        if not arg:
            f = FiberUnwinder.instance().fiber()
            gdb.write('Current fiber is {id} "{name}" {func}\n'.format(
                id=f['fid'],
                name=f['name'].string(),
                func=f['f'],
            ))
            return

        argv = gdb.string_to_argv(arg)
        try:
            fid = int(argv[0])
        except Exception as e:
            gdb.write('Invalid fiber ID: {}\n'.format(argv[0]))
            return

        f = Cord().fiber(fid)
        if f is None:
            gdb.write("Unknown fiber {}.\n".format(argv[0]))
            return

        FiberUnwinder.instance().set_fiber(f)
        gdb.invalidate_cached_frames()

        gdb.execute('frame {}'.format(0 if f == fiber() else 1))

Fiber()

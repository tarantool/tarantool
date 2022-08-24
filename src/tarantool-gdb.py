"""
GDB extension for Tarantool post-mortem analysis.
To use, just put 'source <path-to-this-file>' in gdb.
"""

import gdb.printing
import struct

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

def equal_types(type1, type2):
    return type1.code == type2.code and type1.tag == type2.tag

def dump_type(type):
    return 'tag={} code={}'.format(type.tag, type.code)

def container_of(ptr, container_type, field):
    return (ptr.cast(gdb.lookup_type('char').pointer()) - container_type[field].bitpos // 8).cast(container_type.pointer()).dereference()

def type_has_field(type, field_name):
    for field in type.fields():
        if field.name == field_name:
            return True
    return False

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
                k -= l;
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

        elif mp_type == cls.MP_STR or mp_type == cls.MP_BIN:
            len = cls.decode_strl(data) if mp_type == cls.MP_STR else cls.decode_binl(data)
            if mp_type == cls.MP_BIN:
                s += 'bin'
            s += '"'
            for i in range(0, len):
                c = data.read_u8()
                if c < 128 and cls.mp_char2escape[c] != 0:
                    s += cls.mp_char2escape[c].string()
                else:
                    s += chr(c)
            s += '"'

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

    def to_string(self, depth=-1, maxlen=-1):
        s = self.to_string_data(InputStream(self.val), depth)
        return s if maxlen < 0 else s[:maxlen]

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
Usage: tt-mp EXP [DEPTH [MAXLENGTH]]
    """
    def __init__(self):
        super(MsgPackPrint, self).__init__('tt-mp', gdb.COMMAND_DATA)

    def invoke(self, arg, from_tty):
        argv = gdb.string_to_argv(arg)
        argc = len(argv)
        if argc < 1:
            raise gdb.GdbError("MsgPack is missing")
        mp = TtMsgPack(gdb.parse_and_eval(argv[0]))
        if argc > 2:
            s = mp.to_string(int(argv[1]), int(argv[2]))
        elif argc > 1:
            s = mp.to_string(int(argv[1]))
        else:
            s = mp.to_string()
        gdb.write(s + '\n')


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


class TuplePrinter:
    """Print a tuple object."""

    tuple_type = gdb.lookup_type('struct tuple')
    support_compact = type_has_field(tuple_type, 'data_offset_bsize_raw')

    tuple_formats_sym = gdb.lookup_global_symbol('tuple_formats')
    if not tuple_formats_sym:
        raise NameError('tuple_formats is missing')
    tuple_formats = tuple_formats_sym.value()

    ptr_char = gdb.lookup_type('char').pointer()
    ptr_int32 = gdb.lookup_type('int32_t').pointer()
    ptr_uint32 = gdb.lookup_type('uint32_t').pointer()
    slot_extent_t = find_type('struct field_map_builder_slot_extent')

    # Printer configuration.
    mp_depth = -1
    mp_maxlen = -1

    def __init__(self, val):
        if not equal_types(val.type, self.tuple_type):
            raise gdb.GdbError("expression doesn't evaluate to tuple")
        self.val = val

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
        yield 'data', TtMsgPack(self.data()).to_string(self.mp_depth, self.mp_maxlen)

pp.add_printer('Tuple', '^tuple$', TuplePrinter)


class TuplePrint(gdb.Command):
    """
Decode and print tuple referred by EXP
Usage: tt-tuple EXP [MSGPACK_DEPTH [MSGPACK_MAXLENGTH]]
    """
    def __init__(self):
        super(TuplePrint, self).__init__('tt-tuple', gdb.COMMAND_DATA)

    def invoke(self, arg, from_tty):
        argv = gdb.string_to_argv(arg)
        argc = len(argv)
        if argc < 1:
            raise gdb.GdbError("tuple is missing")

        val = gdb.parse_and_eval(argv[0])
        if equal_types(val.type, TuplePrinter.tuple_type):
            exp_modifier = ''
        elif val.type.code == gdb.TYPE_CODE_PTR and equal_types(val.type.target(), TuplePrinter.tuple_type):
            exp_modifier = '*'
        else:
            raise gdb.GdbError("'{}' doesn't refer to tuple".format(argv[0]))

        try:
            if argc > 1:
                TuplePrinter.mp_depth = int(argv[1])
            if argc > 2:
                TuplePrinter.mp_maxlen = int(argv[2])
            gdb.execute('print {}{}'.format(exp_modifier, argv[0]), False)

        finally:
            TuplePrinter.mp_depth = -1
            TuplePrinter.mp_maxlen = -1


MsgPackPrint()
TuplePrint()

"""
To run tests, just put 'source <path-to-this-file>' in gdb.
"""
import unittest
import gdb
import base64
import itertools
import struct
import sys
from collections import namedtuple

if sys.version_info[0] == 2:
    map = itertools.imap
    filter = itertools.ifilter
    zip = itertools.izip
elif sys.version_info[0] == 3:
    unicode = str

if not hasattr(unittest.TestCase, 'assertRegex'):
    unittest.TestCase.assertRegex = unittest.TestCase.assertRegexpMatches

def log2(msg):
    # gdb.write(msg + '\n')
    pass


class MsgPackEnc(object):
    # types
    enumauto = itertools.count().__next__
    TYPE_INVALID = enumauto()
    TYPE_NIL = enumauto()
    TYPE_BOOL = enumauto()
    TYPE_INT = enumauto()
    TYPE_UINT = enumauto()
    TYPE_FLOAT = enumauto()
    TYPE_STR = enumauto()
    TYPE_BIN = enumauto()
    TYPE_ARRAY = enumauto()
    TYPE_MAP = enumauto()
    TYPE_EXT = enumauto()

    # number types
    enumauto = itertools.count().__next__
    NUM_INT = enumauto()
    NUM_UINT = enumauto()
    NUM_FLOAT = enumauto()

    class __Invalid(object):
        def __repr__(self): return 'INVALID'

    INVALID = __Invalid()

    __exts = {}

    @classmethod
    def register_ext(cls, ext_type, val_type, val_encoder):
        cls.__exts[val_type] = (ext_type, val_encoder)

    @classmethod
    def get_ext(cls, val):
        return cls.__exts.get(type(val))

    @classmethod
    def mp_type(cls, val, mp_type=None):
        return mp_type if mp_type is not None else \
                cls.TYPE_INVALID if val is cls.INVALID else \
                cls.TYPE_NIL if val is None else \
                cls.TYPE_BOOL if isinstance(val, bool) else \
                cls.TYPE_INT if isinstance(val, int) and val < 0 else \
                cls.TYPE_UINT if isinstance(val, int) else \
                cls.TYPE_FLOAT if isinstance(val, float) else \
                cls.TYPE_BIN if isinstance(val, (bytes, bytearray)) else \
                cls.TYPE_STR if isinstance(val, str) else \
                cls.TYPE_ARRAY if isinstance(val, list) else \
                cls.TYPE_MAP if isinstance(val, dict) else \
                cls.TYPE_EXT if cls.get_ext(val) is not None else \
                None

    @classmethod
    def type_len(cls, val, mp_type, type_len=None):
        if type_len is not None:
            pass
        elif mp_type in (cls.TYPE_INVALID, cls.TYPE_NIL, cls.TYPE_BOOL):
            type_len = 0
        elif mp_type == cls.TYPE_UINT:
            type_len = 0 if val < 0x80 else \
                        1 if val < 0x100 else \
                        2 if val < 0x10000 else \
                        4 if val < 0x100000000 else \
                        8 if val < 0x10000000000000000 else \
                        None
        elif mp_type == cls.TYPE_INT:
            if val < 0:
                type_len = 0 if val >= -0x20 else \
                            1 if val >= -0x100 else \
                            2 if val >= -0x10000 else \
                            4 if val >= -0x100000000 else \
                            8 if val >= -0x10000000000000000 else \
                            None
            else:
                type_len = 1 if val < 0x80 else \
                            2 if val < 0x8000 else \
                            4 if val < 0x80000000 else \
                            8 if val < 0x8000000000000000 else \
                            None
        elif mp_type == cls.TYPE_FLOAT:
            type_len = 4
        elif mp_type == cls.TYPE_BIN:
            type_len = 1 if len(val) < 0x100 else \
                        2 if len(val) < 0x10000 else \
                        4 if len(val) < 0x10000000000 else \
                        None
        elif mp_type == cls.TYPE_STR:
            type_len = 0 if len(val) < 0x20 else \
                        1 if len(val) < 0x100 else \
                        2 if len(val) < 0x10000 else \
                        4 if len(val) < 0x10000000000 else \
                        None
        elif mp_type in (cls.TYPE_ARRAY, cls.TYPE_MAP):
            type_len = 0 if len(val) < 0x10 else \
                        2 if len(val) < 0x10000 else \
                        4 if len(val) < 0x10000000000 else \
                        None
        elif mp_type == cls.TYPE_EXT:
            type_len = 1 if len(val) < 0x100 else \
                        2 if len(val) < 0x10000 else \
                        4 if len(val) < 0x10000000000 else \
                        None
        else:
            raise RuntimeError("Unknown type {}".format(mp_type))
        if type_len is None:
            raise RuntimeError("{} doesn't fit into type {}".format(val, mp_type))
        return type_len

    @classmethod
    def encode_invalid(cls, val=None, type_len=0):
        return b'\xc1'

    @classmethod
    def encode_nil(cls, val=None, type_len=0):
        return b'\xc0'

    @classmethod
    def encode_bool(cls, val, type_len=0):
        return b'\xc3' if val else b'\xc2'

    @classmethod
    def encode_num(cls, val, num_type, num_len, big_endian=True):
        if num_len == 0:
            return bytearray()
        packfmt = {
            cls.NUM_UINT: {
                1: 'B',
                2: 'H',
                4: 'I',
                8: 'Q',
            },
            cls.NUM_INT: {
                1: 'b',
                2: 'h',
                4: 'i',
                8: 'q',
            },
            cls.NUM_FLOAT: {
                4: 'f',
                8: 'd',
            },
        }[num_type][num_len]
        data = bytearray(num_len)
        struct.pack_into(('>' if big_endian else '<') + packfmt, data, 0, val)
        return data

    @classmethod
    def encode_uint(cls, val, type_len):
        data = bytearray(1)
        data[0] = {
            0: val,
            1: 0xcc,
            2: 0xcd,
            4: 0xce,
            8: 0xcf,
        }[type_len]
        return data + cls.encode_num(val, cls.NUM_UINT, type_len)

    @classmethod
    def encode_int(cls, val, type_len):
        data = bytearray(1)
        data[0] = {
            0: val + 0x100,
            1: 0xd0,
            2: 0xd1,
            4: 0xd2,
            8: 0xd3,
        }[type_len]
        return data + cls.encode_num(val, cls.NUM_INT, type_len)

    @classmethod
    def encode_float(cls, val, type_len):
        data = bytearray(1)
        data[0] = {
            4: 0xca,
            8: 0xcb,
        }[type_len]
        return data + cls.encode_num(val, cls.NUM_FLOAT, type_len)

    @classmethod
    def encode_bin(cls, val, type_len):
        data = bytearray(1)
        data[0] = {
            1: 0xc4,
            2: 0xc5,
            4: 0xc6,
        }[type_len]
        data += cls.encode_num(len(val), cls.NUM_UINT, type_len)
        for b in val:
            data.append(b)
        return data

    @classmethod
    def encode_str(cls, val, type_len):
        data = bytearray(1)
        data[0] = {
            0: 0xa0 + len(val),
            1: 0xd9,
            2: 0xda,
            4: 0xdb,
        }[type_len]
        data += cls.encode_num(len(val), cls.NUM_UINT, type_len)
        for b in val:
            data.append(ord(b))
        return data

    @classmethod
    def encode_array(cls, val, type_len):
        fmt = {
            0: 0x90 + len(val),
            2: 0xdc,
            4: 0xdd,
        }[type_len]
        data = bytearray(1)
        data[0] = fmt
        data += cls.encode_num(len(val), cls.NUM_UINT, type_len)
        for item in val:
            data += cls.encode(item)
        return data

    @classmethod
    def encode_map(cls, val, type_len):
        fmt = {
            0: 0x80 + len(val),
            2: 0xde,
            4: 0xdf,
        }[type_len]
        data = bytearray(1)
        data[0] = fmt
        data += cls.encode_num(len(val), cls.NUM_UINT, type_len)
        for k, v in val.items():
            data += cls.encode(k)
            data += cls.encode(v)
        return data

    @classmethod
    def encode_ext(cls, val, type_len, len_spoiler=0, **encode_kwargs):
        ext_type, ext_encoder = cls.get_ext(val)
        fmt = {
            1: 0xc7,
            2: 0xc8,
            4: 0xc9,
        }[type_len]
        ext_data = ext_encoder(val, **encode_kwargs)
        if len_spoiler > 0:
            ext_data += bytearray(len_spoiler)
        elif len_spoiler < 0:
            del ext_data[len_spoiler:]
        data = bytearray(1)
        data[0] = fmt
        data += cls.encode_num(len(ext_data), cls.NUM_UINT, type_len)
        data += cls.encode_num(ext_type, cls.NUM_INT, 1)
        data += ext_data
        return data

    __encoders = {
        TYPE_INVALID: encode_invalid,
        TYPE_NIL: encode_nil,
        TYPE_BOOL: encode_bool,
        TYPE_INT: encode_int,
        TYPE_UINT: encode_uint,
        TYPE_FLOAT: encode_float,
        TYPE_STR: encode_str,
        TYPE_BIN: encode_bin,
        TYPE_ARRAY: encode_array,
        TYPE_MAP: encode_map,
        TYPE_EXT: encode_ext,
    }

    @classmethod
    def encode(cls, val, type_len=None, mp_type=None, **encode_kwargs):
        mp_type = cls.mp_type(val, mp_type)
        if mp_type is None:
            raise RuntimeError("Failed to identify mp_type of value '{}'".format(val))
        type_len = cls.type_len(val, mp_type, type_len)
        return cls.__encoders[mp_type].__func__(cls, val, type_len, **encode_kwargs)


class MsgPackTest(unittest.TestCase):
    @staticmethod
    def print(val, **encode_kwargs):
        log2('print: val={}'.format(val))
        mp = MsgPackEnc.encode(val, **encode_kwargs)
        mp_expr = ''
        for byte in mp:
            mp_expr += '\\{:03o}'.format(byte)
        log2('tt-mp "{}"'.format(mp_expr))
        return gdb.execute('tt-mp "{}"'.format(mp_expr), False, True).partition('=')[2].strip()

    def sample_ref(self, sample):
        raise NotImplementedError("{}.sample_ref not implemented".format(self.__class__.__name__))

    def check_equal(self, ref, sample, **encode_kwargs):
        self.assertEqual(ref, self.print(sample, **encode_kwargs))

    def check_sample(self, sample, **encode_kwargs):
        ref = self.sample_ref(sample)
        self.check_equal(ref, sample, **encode_kwargs)

    def check_samples(self, samples, **encode_kwargs):
        for sample in samples:
            self.check_sample(sample, **encode_kwargs)

    def invalid_sample_regex(self, samplec):
        raise NotImplementedError("{}.invalid_sample_regex not implemented".format(self.__class__.__name__))

    def check_invalid_sample(self, sample, **encode_kwargs):
        out = self.print(sample, **encode_kwargs)
        self.assertRegex(out, self.invalid_sample_regex(sample, **encode_kwargs))

    def check_invalid_samples(self, samples, **encode_kwargs):
        for sample in samples:
            self.check_invalid_sample(sample, **encode_kwargs)


class MsgPackSimpleTest(MsgPackTest):
    def test_nil(self):
        self.check_equal('null', None)

    def test_false(self):
        self.check_equal('false', False)

    def test_true(self):
        self.check_equal('true', True)


class MsgPackUIntTest(MsgPackTest):
    sample_max = lambda type_len: 2**type_len-1

    samples_fix = [i for i in range(0x80)]
    samples_0 = [0, 1, 42]
    samples_8 = samples_0 + [sample_max(8)]
    samples_16 = samples_8 + [sample_max(16)]
    samples_32 = samples_16 + [sample_max(32)]
    samples_64 = samples_32 + [sample_max(64)]

    def sample_ref(self, sample):
        return str(sample) + 'U'

    def test_fixuint(self): self.check_samples(self.samples_fix, type_len=0)
    def test_uint8(self): self.check_samples(self.samples_8, type_len=1)
    def test_uint16(self): self.check_samples(self.samples_16, type_len=2)
    def test_uint32(self): self.check_samples(self.samples_32, type_len=4)
    def test_uint64(self): self.check_samples(self.samples_64, type_len=8)


class MsgPackIntTest(MsgPackTest):
    sample_min = lambda type_len: -(2**(type_len-1))
    sample_max = lambda type_len: 2**(type_len-1)-1

    samples_fix = [i-0x20 for i in range(0x20)]
    samples_0 = [-42, -1, 0, 1, 74]
    samples_8 = [sample_min(8)] + samples_0 + [sample_max(8)]
    samples_16 = [sample_min(16)] + samples_8 + [sample_max(16)]
    samples_32 = [sample_min(32)] + samples_16 + [sample_max(32)]
    samples_64 = [sample_min(64)] + samples_32 + [sample_max(64)]

    def sample_ref(self, sample):
        return str(sample)

    def test_fixint(self): self.check_samples(self.samples_fix, type_len=0, mp_type=MsgPackEnc.TYPE_INT)
    def test_int8(self): self.check_samples(self.samples_8, type_len=1, mp_type=MsgPackEnc.TYPE_INT)
    def test_int16(self): self.check_samples(self.samples_16, type_len=2, mp_type=MsgPackEnc.TYPE_INT)
    def test_int32(self): self.check_samples(self.samples_32, type_len=4, mp_type=MsgPackEnc.TYPE_INT)
    def test_int64(self): self.check_samples(self.samples_64, type_len=8, mp_type=MsgPackEnc.TYPE_INT)


class MsgPackFloatTest(MsgPackTest):
    samples = [
        0.0,
        1.0,
        2.0,
        4.0,
        32.0,
        1024.0,
        1.5,
        3.0,
        6.0,
        0.75,
        0.375,
    ]

    def sample_ref(self, sample):
        return str(sample)

    def test_float32(self): self.check_samples(self.samples, type_len=4)
    def test_float64(self): self.check_samples(self.samples, type_len=8)

class MsgPackBinTest(MsgPackTest):
    samples = [
        b'\xaa',
        b'\xaa\xbb',
        b'\xaa\xbb\xcc',
    ]

    def sample_ref(self, sample):
        return 'bin:' + unicode(base64.b64encode(sample), 'utf-8')

    def test_bin8(self): self.check_samples(self.samples, type_len=1)
    def test_bin16(self): self.check_samples(self.samples, type_len=2)
    def test_bin32(self): self.check_samples(self.samples, type_len=4)


class MsgPackStrTest(MsgPackTest):
    samples = [
        '',
        'abc',
        'yet another string',
    ]

    def sample_ref(self, sample):
        return '"{}"'.format(sample)

    def test_fixstr(self): self.check_samples(self.samples, type_len=0)
    def test_str8(self): self.check_samples(self.samples, type_len=1)
    def test_str16(self): self.check_samples(self.samples, type_len=2)
    def test_str32(self): self.check_samples(self.samples, type_len=4)


class MsgPackExtTest(MsgPackTest):
    @staticmethod
    def regex_length_mismatch(ext_name):
        return "MsgPack: Ext:{} at [^:]+: decoded length \d+ doesn\'t match the expected".format(ext_name)

    @staticmethod
    def regex_unexpected_type(ext_name):
        return "MsgPack: Ext:{} at [^:]+: got type [^ ]+ at offset \d+ \(expected - [^)]+\)".format(ext_name)


MP_EXT_DECIMAL = globals()['MP_DECIMAL']
if MP_EXT_DECIMAL is not None:
    class MsgPackExtDecimalTest(MsgPackExtTest):
        ExtType = namedtuple('ExtType', [
            'ref',
            'scale',
            'bcd',
        ])

        @staticmethod
        def encode_ext(val):
            return MsgPackEnc.encode(val.scale) + val.bcd

        MsgPackEnc.register_ext(int(MP_EXT_DECIMAL), ExtType, encode_ext)

        samples = [
            ExtType('1', 0, b'\x1c'),
            ExtType('1000', -3, b'\x1c'),
            ExtType('0.1', 1, b'\x1c'),
            ExtType('-1', 0, b'\x1d'),
            ExtType('-0.1', 1, b'\x1d'),
            ExtType('12', 0, b'\x01\x2c'),
            ExtType('1.2', 1, b'\x01\x2c'),
            ExtType('0.12', 2, b'\x01\x2c'),
            ExtType('0.012', 3, b'\x01\x2c'),
            ExtType('-1', 0, b'\x1d'),
            ExtType('-12', 0, b'\x01\x2d'),
            ExtType('-1.2', 1, b'\x01\x2d'),
            ExtType('-0.12', 2, b'\x01\x2d'),
            ExtType('-0.012', 3, b'\x01\x2d'),
            ExtType('123', 0, b'\x12\x3c'),
            ExtType('1234', 0, b'\x01\x23\x4c'),
            ExtType('-12345', 0, b'\x12\x34\x5d'),
            ExtType('12345', 0, b'\x12\x34\x5c'),
            ExtType('1234500', -2, b'\x12\x34\x5c'),
            ExtType('123.456789', 6, b'\x12\x34\x56\x78\x9c'),
            ExtType('-123.456789', 6, b'\x12\x34\x56\x78\x9d'),
        ]

        invalid_samples = [
            ExtType(None, None, b'\x1c'),
            ExtType(None, True, b'\x1c'),
            ExtType(None, 3.1415926, b'\x1c'),
            ExtType(None, '3.1415926', b'\x1c'),
        ]

        def sample_ref(self, sample):
            return 'dec:' + sample.ref

        def test_ext8_decimal(self): self.check_samples(self.samples, type_len=1)
        def test_ext16_decimal(self): self.check_samples(self.samples, type_len=2)
        def test_ext32_decimal(self): self.check_samples(self.samples, type_len=4)

        def invalid_sample_regex(self, sample, **encode_kwargs):
            return self.regex_unexpected_type('DECIMAL')

        def test_ext8_decimal_invalid(self):
            self.check_invalid_samples(self.invalid_samples, type_len=1)
        def test_ext16_decimal_invalid(self):
            self.check_invalid_samples(self.invalid_samples, type_len=2)
        def test_ext32_decimal_invalid(self):
            self.check_invalid_samples(self.invalid_samples, type_len=4)


MP_EXT_UUID = globals()['MP_UUID']
if MP_EXT_UUID is not None:
    class MsgPackExtUuidTest(MsgPackExtTest):
        # MsgPack encoder check Python value type and pick the corresponding
        # encoder function so this separate type is needed to distinguish
        # EXT_TYPE_UUID from TYPE_BIN
        ExtType = namedtuple('ExtType', [
            'bytes',
        ])

        @staticmethod
        def encode_ext(val):
            return val.bytes

        MsgPackEnc.register_ext(int(MP_EXT_UUID), ExtType, encode_ext)

        samples = [
            ExtType(b'\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00'),
            ExtType(b'\x00\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f'),
            ExtType(b'\x00\x10\x20\x30\x40\x50\x60\x70\x80\x90\xa0\xb0\xc0\xd0\xe0\xf0'),
            ExtType(b'\x01\x23\x45\x67\x89\xab\xcd\xef\x10\x32\x54\x76\x98\xba\xdc\xfe'),
            ExtType(b'\xbe\xe0\xbe\xe1\xbe\xe2\xbe\xe3\xbe\xe4\xbe\xe5\xbe\xe6\xbe\xe7'),
        ]

        invalid_samples = [
            ExtType(b''),
            ExtType(b'\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00'),
            ExtType(b'\x00\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f\x10'),
        ]

        def sample_ref(self, sample):
            buf = sample.bytes
            return '{:08x}-{:04x}-{:04x}-{:02x}{:02x}-{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}'.format(
                struct.unpack_from('>I', buf, 0)[0],
                struct.unpack_from('>H', buf, 4)[0],
                struct.unpack_from('>H', buf, 6)[0],
                buf[8], buf[9],
                buf[10], buf[11], buf[12], buf[13], buf[14], buf[15],
            )

        def test_ext8_uuid(self): self.check_samples(self.samples, type_len=1)
        def test_ext16_uuid(self): self.check_samples(self.samples, type_len=2)
        def test_ext32_uuid(self): self.check_samples(self.samples, type_len=4)

        def invalid_sample_regex(self, sample, **encode_kwargs):
            return self.regex_length_mismatch('UUID')

        def test_ext8_uuid_invalid(self):
            self.check_invalid_samples(self.invalid_samples, type_len=1)
        def test_ext16_uuid_invalid(self):
            self.check_invalid_samples(self.invalid_samples, type_len=2)
        def test_ext32_uuid_invalid(self):
            self.check_invalid_samples(self.invalid_samples, type_len=4)


MP_EXT_DATETIME = globals()['MP_DATETIME']
if MP_EXT_DATETIME is not None:
    class MsgPackExtDatetimeTest(MsgPackExtTest):
        ExtType = namedtuple('ExtType', [
            'ref',
            'epoch',
            'nsec',
            'tzoffset',
            'tzindex',
        ])

        @staticmethod
        def encode_ext(val):
            mp = MsgPackEnc.encode_num(val.epoch, MsgPackEnc.NUM_INT, 8, False)
            if val.nsec != 0 or val.tzoffset != 0 or val.tzindex != 0:
                mp += MsgPackEnc.encode_num(val.nsec, MsgPackEnc.NUM_INT, 4, False)
                mp += MsgPackEnc.encode_num(val.tzoffset, MsgPackEnc.NUM_INT, 2, False)
                mp += MsgPackEnc.encode_num(val.tzindex, MsgPackEnc.NUM_INT, 2, False)
            return mp

        MsgPackEnc.register_ext(int(MP_EXT_DATETIME), ExtType, encode_ext)

        samples = [
            ExtType('1970-01-01T00:00:00Z', 0, 0, 0, 0),
            ExtType('1970-01-01T02:46:40Z', 10000, 0, 0, 0),
            ExtType('1970-01-01T02:46:40.000000123Z', 10000, 123, 0, 0),
            ExtType('1970-01-01T03:46:40.000000123+0100', 10000, 123, 1*60, 0),
            ExtType('1970-01-01T04:16:40.000000123+0130', 10000, 123, 1*60+30, 0),
            ExtType('1970-01-01T04:16:40.000000123D', 10000, 123, 1*60+30, 4),
            ExtType('1969-12-31T23:46:40.000000123-0300', 10000, 123, -3*60, 0),
            ExtType('1970-01-01T00:01:40.000000123-0245', 10000, 123, -3*60+15, 0),
        ]

        def sample_ref(self, sample):
            return sample.ref

        def test_ext8_datetime(self): self.check_samples(self.samples, type_len=1)
        def test_ext16_datetime(self): self.check_samples(self.samples, type_len=2)
        def test_ext32_datetime(self): self.check_samples(self.samples, type_len=4)

        def invalid_sample_regex(self, sample, **encode_kwargs):
            return self.regex_length_mismatch('DATETIME')

        def test_ext8_datetime_invalid(self):
            self.check_invalid_samples(self.samples, type_len=1, len_spoiler=5)
            self.check_invalid_samples(self.samples, type_len=1, len_spoiler=-3)
        def test_ext16_datetime_invalid(self):
            self.check_invalid_samples(self.samples, type_len=2, len_spoiler=4)
            self.check_invalid_samples(self.samples, type_len=2, len_spoiler=-2)
        def test_ext32_datetime_invalid(self):
            self.check_invalid_samples(self.samples, type_len=4, len_spoiler=3)
            self.check_invalid_samples(self.samples, type_len=4, len_spoiler=-1)


MP_EXT_ERROR = globals()['MP_ERROR']
if MP_EXT_ERROR is not None:
    class MsgPackExtErrorTest(MsgPackExtTest):
        ExtType = namedtuple('ExtType', [
            'value',
        ])

        @staticmethod
        def encode_ext(val):
            return MsgPackEnc.encode(val.value)

        MsgPackEnc.register_ext(int(MP_EXT_ERROR), ExtType, encode_ext)

        KEY_STACK = int(gdb.parse_and_eval('MP_ERROR_STACK'))

        KEY_TYPE = int(gdb.parse_and_eval('MP_ERROR_TYPE'))
        KEY_FILE = int(gdb.parse_and_eval('MP_ERROR_FILE'))
        KEY_LINE = int(gdb.parse_and_eval('MP_ERROR_LINE'))
        KEY_MESSAGE = int(gdb.parse_and_eval('MP_ERROR_MESSAGE'))
        KEY_ERRNO = int(gdb.parse_and_eval('MP_ERROR_ERRNO'))
        KEY_CODE = int(gdb.parse_and_eval('MP_ERROR_CODE'))
        KEY_FIELDS = int(gdb.parse_and_eval('MP_ERROR_FIELDS'))

        errors = [
            {
                KEY_TYPE: 'type1',
                KEY_FILE: 'file1',
                KEY_LINE: 3,
                KEY_MESSAGE: 'some message 1',
                KEY_ERRNO: 3,
                KEY_CODE: 45,
                KEY_FIELDS: None,
            },
            {
                KEY_TYPE: 'type2',
                KEY_FILE: 'file2',
                KEY_LINE: 23,
                KEY_MESSAGE: 'some message 2',
                KEY_ERRNO: 4,
                KEY_CODE: 56,
                KEY_FIELDS: None,
            },
            {
                KEY_TYPE: 'type3',
                KEY_FILE: 'file3',
                KEY_LINE: 123,
                KEY_MESSAGE: 'some message 3',
                KEY_ERRNO: 5,
                KEY_CODE: 67,
                KEY_FIELDS: None,
            },
        ]

        samples = [
            ExtType({ KEY_STACK: [errors[0]] }),
            ExtType({ KEY_STACK: [errors[0], errors[1]] }),
            ExtType({ KEY_STACK: [errors[0], errors[1], errors[2]] }),
        ]

        def sample_ref(self, sample):
            def ref_map(func, val):
                def identity_func(item):
                    return item
                mapped_items = map(identity_func if func is None else func, val.items())
                return '{{{}}}'.format(', '.join(map(lambda item: '["{}"] = {}'.format(*item), mapped_items)))

            def ref_array(func, val):
                return '{{{}}}'.format(', '.join(map(func, val)))

            def ref_single_error(error):
                def map_error(item):
                    ref_keys = {
                        self.KEY_TYPE: 'type',
                        self.KEY_FILE: 'file',
                        self.KEY_LINE: 'line',
                        self.KEY_MESSAGE: 'message',
                        self.KEY_ERRNO: 'errno',
                        self.KEY_CODE: 'code',
                        self.KEY_FIELDS: 'fields',
                    }
                    k, v = item
                    return ref_keys[k], self.print(v)
                return ref_map(map_error, error)

            ref_stack = ref_array(ref_single_error, sample.value[self.KEY_STACK])
            return ref_map(None, {'stack': ref_stack})

        def test_ext8_error(self): self.check_samples(self.samples, type_len=1)
        def test_ext16_error(self): self.check_samples(self.samples, type_len=2)
        def test_ext32_error(self): self.check_samples(self.samples, type_len=4)

        def invalid_sample_regex(self, sample, **encode_kwargs):
            return self.regex_length_mismatch('ERROR')

        def test_ext8_error_invalid(self):
            self.check_invalid_samples(self.samples, type_len=1, len_spoiler=5)
            self.check_invalid_samples(self.samples, type_len=1, len_spoiler=-3)
        def test_ext16_error_invalid(self):
            self.check_invalid_samples(self.samples, type_len=2, len_spoiler=4)
            self.check_invalid_samples(self.samples, type_len=2, len_spoiler=-2)
        def test_ext32_error_invalid(self):
            self.check_invalid_samples(self.samples, type_len=4, len_spoiler=3)
            self.check_invalid_samples(self.samples, type_len=4, len_spoiler=-1)


MP_EXT_COMPRESSION = globals()['MP_COMPRESSION']
if MP_EXT_COMPRESSION is not None:
    class MsgPackExtCompressionTest(MsgPackExtTest):
        ExtType = namedtuple('ExtType', [
            'type',
            'raw_size',
            'size'
        ])

        @staticmethod
        def encode_ext(val):
            mp = bytearray()
            mp += MsgPackEnc.encode(val.type)
            mp += MsgPackEnc.encode(val.raw_size)
            mp += bytearray(val.size)
            return mp

        MsgPackEnc.register_ext(int(MP_EXT_COMPRESSION), ExtType, encode_ext)

        TYPE_NONE = int(gdb.parse_and_eval('COMPRESSION_TYPE_NONE'))
        TYPE_MAX = int(gdb.parse_and_eval('compression_type_MAX'))
        type_strs = gdb.parse_and_eval('compression_type_strs')

        samples = [
            ExtType(TYPE_MAX + 0, 2**5 + 1, 2*5),
            ExtType(TYPE_MAX + 1, 2**7 + 1, 2*7),
            ExtType(TYPE_MAX + 2, 2**8 + 1, 2*8),
            ExtType(TYPE_MAX + 3, 2**16 + 1, 2*16),
            ExtType(TYPE_MAX + 4, 2**32 + 1, 2*32),
            ExtType(TYPE_MAX + 5, 22, 30),
        ]

        samples_in_use = []
        for i in range(TYPE_NONE, TYPE_MAX):
            samples.append(ExtType(i, i + 74, i + 42))
            samples.append(ExtType(i, i + 42, i + 74))

        def sample_ref(self, sample):
            if self.TYPE_NONE <= sample.type < self.TYPE_MAX:
                type_str = self.type_strs[sample.type].string()
            else:
                type_str = str(sample.type)
            return 'compression({}):[{}]->[{}]'.format(type_str, sample.raw_size, sample.size)

        def test_ext8_compression(self): self.check_samples(self.samples, type_len=1)
        def test_ext16_compression(self): self.check_samples(self.samples, type_len=2)
        def test_ext32_compression(self): self.check_samples(self.samples, type_len=4)

        def test_ext8_compression_in_use(self): self.check_samples(self.samples_in_use, type_len=1)
        def test_ext16_compression_in_use(self): self.check_samples(self.samples_in_use, type_len=2)
        def test_ext32_compression_in_use(self): self.check_samples(self.samples_in_use, type_len=4)

        invalid_samples = [
            # Samples below are produced by replacing some (or all) field
            # of the valid sample with the invalid one(s)
            # ExtType(TYPE_MAX, 20, 10) <= valid
            ExtType(-TYPE_MAX, 20, 10),
            ExtType(TYPE_MAX, -20, 10),
            ExtType(-TYPE_MAX, -20, 10),
        ]

        def invalid_sample_regex(self, sample, **encode_kwargs):
            return self.regex_unexpected_type('COMPRESSION')

        def test_ext8_compression_invalid(self):
            self.check_invalid_samples(self.invalid_samples, type_len=1)
        def test_ext16_compression_invalid(self):
            self.check_invalid_samples(self.invalid_samples, type_len=2)
        def test_ext32_compression_invalid(self):
            self.check_invalid_samples(self.invalid_samples, type_len=4)


MP_EXT_INTERVAL = globals()['MP_INTERVAL']
if MP_EXT_INTERVAL is not None:
    class MsgPackExtIntervalTest(MsgPackExtTest):
        ExtType = namedtuple('ExtType', [
            'value',
        ])

        @staticmethod
        def encode_ext(val):
            mp = bytearray()
            num_fields = 0
            for k, v in val.value.items():
                if v is not None:
                    num_fields += 1
                    mp += MsgPackEnc.encode_num(k, MsgPackEnc.NUM_UINT, 1)
                    mp += MsgPackEnc.encode(v)
            return MsgPackEnc.encode_num(num_fields, MsgPackEnc.NUM_UINT, 1) + mp

        MsgPackEnc.register_ext(int(MP_EXT_INTERVAL), ExtType, encode_ext)

        KEY_YEAR = int(gdb.parse_and_eval('FIELD_YEAR'))
        KEY_MONTH = int(gdb.parse_and_eval('FIELD_MONTH'))
        KEY_WEEK = int(gdb.parse_and_eval('FIELD_WEEK'))
        KEY_DAY = int(gdb.parse_and_eval('FIELD_DAY'))
        KEY_HOUR = int(gdb.parse_and_eval('FIELD_HOUR'))
        KEY_MINUTE = int(gdb.parse_and_eval('FIELD_MINUTE'))
        KEY_SECOND = int(gdb.parse_and_eval('FIELD_SECOND'))
        KEY_NANOSECOND = int(gdb.parse_and_eval('FIELD_NANOSECOND'))
        KEY_ADJUST = int(gdb.parse_and_eval('FIELD_ADJUST'))

        DT_EXCESS = int(gdb.parse_and_eval('DT_EXCESS'))
        DT_LIMIT = int(gdb.parse_and_eval('DT_LIMIT'))
        DT_SNAP = int(gdb.parse_and_eval('DT_SNAP'))

        keys = (
            KEY_YEAR,
            KEY_MONTH,
            KEY_WEEK,
            KEY_DAY,
            KEY_HOUR,
            KEY_MINUTE,
            KEY_SECOND,
            KEY_NANOSECOND,
            KEY_ADJUST,
        )

        samples = []
        for adjust in None, DT_EXCESS, DT_LIMIT, DT_SNAP:
            samples.extend([
                ExtType(dict(zip(keys, (1, None, None, None, None, None, None, None, adjust)))),
                ExtType(dict(zip(keys, (None, 1, None, None, None, None, None, None, adjust)))),
                ExtType(dict(zip(keys, (None, None, 1, None, None, None, None, None, adjust)))),
                ExtType(dict(zip(keys, (None, None, None, 1, None, None, None, None, adjust)))),
                ExtType(dict(zip(keys, (None, None, None, None, 1, None, None, None, adjust)))),
                ExtType(dict(zip(keys, (None, None, None, None, None, 1, None, None, adjust)))),
                ExtType(dict(zip(keys, (None, None, None, None, None, None, 1, None, adjust)))),
                ExtType(dict(zip(keys, (None, None, None, None, None, None, None, 1, adjust)))),
                ExtType(dict(zip(keys, (1, None, 2, None, 3, None, 4, None, adjust)))),
                ExtType(dict(zip(keys, (1, 2, 3, 4, 5, 6, 7, 8, adjust)))),
                ExtType(dict(zip(keys, (-1, None, None, None, None, None, None, None, adjust)))),
                ExtType(dict(zip(keys, (None, -1, None, None, None, None, None, None, adjust)))),
                ExtType(dict(zip(keys, (None, None, -1, None, None, None, None, None, adjust)))),
                ExtType(dict(zip(keys, (None, None, None, -1, None, None, None, None, adjust)))),
                ExtType(dict(zip(keys, (None, None, None, None, -1, None, None, None, adjust)))),
                ExtType(dict(zip(keys, (None, None, None, None, None, -1, None, None, adjust)))),
                ExtType(dict(zip(keys, (None, None, None, None, None, None, -1, None, adjust)))),
                ExtType(dict(zip(keys, (None, None, None, None, None, None, None, -1, adjust)))),
                ExtType(dict(zip(keys, (1, None, -2, None, 3, None, -4, None, adjust)))),
                ExtType(dict(zip(keys, (-1, 2, -3, 4, -5, 6, -7, 8, adjust)))),
            ])

        def sample_ref(self, sample):
            ref_units = {
                self.KEY_YEAR: "years",
                self.KEY_MONTH: "months",
                self.KEY_WEEK: "weeks",
                self.KEY_DAY: "days",
                self.KEY_HOUR: "hours",
                self.KEY_MINUTE: "minutes",
                self.KEY_SECOND: "seconds",
                self.KEY_NANOSECOND: "nanoseconds",
            }

            ref = []
            field_fmt = '{:+} {}'
            for k in filter(lambda k: k != self.KEY_ADJUST, self.keys):
                v = sample.value[k]
                if v != None:
                    ref.append(field_fmt.format(v, ref_units[k]))
                    field_fmt = '{} {}'
            if len(ref) == 0:
                ref.append(field_fmt.format(0, ref_units[self.KEY_SECOND]))
            if sample.value[self.KEY_ADJUST] is not None:
                adjust = {
                    self.DT_EXCESS: 'excess',
                    self.DT_LIMIT: 'none',
                    self.DT_SNAP: 'last',
                }.get(sample.value[self.KEY_ADJUST], 'UNKNOWN')
                ref.append('({})'.format(adjust))
            return ' '.join(ref)

        def test_ext8_interval(self): self.check_samples(self.samples, type_len=1)
        def test_ext16_interval(self): self.check_samples(self.samples, type_len=2)
        def test_ext32_interval(self): self.check_samples(self.samples, type_len=4)

        invalid_samples = [
            # Samples below are produced by replacing some (or all) field
            # of the valid sample with the invalid one(s)
            # ExtType(dict(zip(keys, (-1, 2, -3, 4, -5, 6, -7, 8, DT_LIMIT)))) <= valid
            ExtType(dict(zip(keys, (True, 2, -3, 4, -5, 6, -7, 8, DT_LIMIT)))),
            ExtType(dict(zip(keys, (-1, True, -3, 4, -5, 6, -7, 8, DT_LIMIT)))),
            ExtType(dict(zip(keys, (-1, 2, True, 4, -5, 6, -7, 8, DT_LIMIT)))),
            ExtType(dict(zip(keys, (-1, 2, -3, True, -5, 6, -7, 8, DT_LIMIT)))),
            ExtType(dict(zip(keys, (-1, 2, -3, 4, True, 6, -7, 8, DT_LIMIT)))),
            ExtType(dict(zip(keys, (-1, 2, -3, 4, -5, True, -7, 8, DT_LIMIT)))),
            ExtType(dict(zip(keys, (-1, 2, -3, 4, -5, 6, True, 8, DT_LIMIT)))),
            ExtType(dict(zip(keys, (-1, 2, -3, 4, -5, 6, -7, True, DT_LIMIT)))),
            ExtType(dict(zip(keys, (-1, 2, -3, 4, -5, 6, -7, 8, True)))),
            ExtType(dict(zip(keys, (True, True, True, True, True, True, True, True, True)))),
        ]

        def invalid_sample_regex(self, sample, **encode_kwargs):
            return self.regex_unexpected_type('INTERVAL')

        def test_ext8_interval_invalid(self):
            self.check_invalid_samples(self.invalid_samples, type_len=1)
        def test_ext16_interval_invalid(self):
            self.check_invalid_samples(self.invalid_samples, type_len=2)
        def test_ext32_interval_invalid(self):
            self.check_invalid_samples(self.invalid_samples, type_len=4)


MP_EXT_TUPLE = globals()['MP_TUPLE']
if MP_EXT_TUPLE is not None:
    class MsgPackExtTupleTest(MsgPackExtTest):
        ExtType = namedtuple('ExtType', [
            'format_id',
            'payload',
        ])

        @staticmethod
        def encode_ext(val):
            mp = bytearray()
            mp += MsgPackEnc.encode(val.format_id)
            mp += MsgPackEnc.encode(val.payload)
            return mp

        MsgPackEnc.register_ext(int(MP_EXT_TUPLE), ExtType, encode_ext)

        samples = []
        for format_id in [0, 42]:
            for payload in [
                [],
                [0, 1, 2, 3],
                list(map(str, [5, 6, 7, 8])),
                [1, -77, 123.456, True, None, 'some string', b'\x12\x34\x56'],
            ]:
                samples.append(ExtType(format_id, payload))

        def sample_ref(self, sample):
            return '{{[format_id] = {}, [payload] = {}}}'.format(
                self.print(sample.format_id),
                self.print(sample.payload),
            )

        def test_ext8_tuple(self): self.check_samples(self.samples, type_len=1)
        def test_ext16_tuple(self): self.check_samples(self.samples, type_len=2)
        def test_ext32_tuple(self): self.check_samples(self.samples, type_len=4)

        invalid_samples = [
            # Samples below are produced by replacing some (or all) field
            # of the valid sample with the invalid one(s)
            # ExtType(42, [-2, -1, 0, 1, 2]) <= valid
            ExtType(-42, [-2, -1, 0, 1, 2]),
            ExtType(42, {'one': 1, 'two': 2}),
            ExtType(-42, {'one': 1, 'two': 2}),
        ]

        def invalid_sample_regex(self, sample, **encode_kwargs):
            return self.regex_unexpected_type('TUPLE')

        def test_ext8_tuple_invalid(self):
            self.check_invalid_samples(self.invalid_samples, type_len=1)
        def test_ext16_tuple_invalid(self):
            self.check_invalid_samples(self.invalid_samples, type_len=2)
        def test_ext32_tuple_invalid(self):
            self.check_invalid_samples(self.invalid_samples, type_len=4)


class MsgPackArrayTest(MsgPackTest):
    samples = [
        [],
        [0],
        [0, 1],
        [0, 1, 2],
        [-1],
        [-1, -2],
        [0, -1, 2],
        [-1, 2, 2.25],
        [1, -77, 123.456, True, None, 'some string', b'\x12\34\56'],
    ]

    samples_nested = [
        [samples[0]],
        [42, samples[1]],
        [samples],
    ]

    samples_nested2 = [
        [samples_nested[0]],
        [333, samples_nested[1]],
        [samples_nested[2], samples[1], samples_nested[0]],
        [samples_nested],
    ]

    ext_classes = [
        MsgPackExtDecimalTest,
        MsgPackExtUuidTest,
        MsgPackExtDatetimeTest,
        MsgPackExtErrorTest,
        MsgPackExtCompressionTest,
        MsgPackExtIntervalTest,
        MsgPackExtTupleTest,
    ]

    samples_with_ext = []
    for ext_class in ext_classes:
        samples_with_ext.append([ ext_class.samples[0] ])
        samples_with_ext.append([ 'simple object before', ext_class.samples[1] ])
        samples_with_ext.append([ ext_class.samples[2], 'simple object before' ])
    samples_with_ext.append([ext_class.samples[0] for ext_class in ext_classes])

    samples_with_ext_nested = [
        [samples_with_ext[0]],
        [42, samples_with_ext[1]],
        [samples_with_ext],
    ]

    samples_with_ext_nested2 = [
        [samples_with_ext_nested[0]],
        [333, samples_with_ext_nested[1]],
        [samples_with_ext_nested[2], samples_with_ext[1], samples_with_ext_nested[0]],
        [samples_with_ext_nested],
    ]

    def sample_ref(self, sample):
        return '' if len(sample) == 0 else \
            '{{{}}}'.format(', '.join([self.print(item) for item in sample]))

    def test_fixarray(self): self.check_samples(self.samples, type_len=0)
    def test_array16(self): self.check_samples(self.samples, type_len=2)
    def test_array32(self): self.check_samples(self.samples, type_len=4)
    def test_fixarray_nested(self): self.check_samples(self.samples_nested, type_len=0)
    def test_array16_nested(self): self.check_samples(self.samples_nested, type_len=2)
    def test_array32_nested(self): self.check_samples(self.samples_nested, type_len=4)
    def test_fixarray_nested2(self): self.check_samples(self.samples_nested2, type_len=0)
    def test_array16_nested2(self): self.check_samples(self.samples_nested2, type_len=2)
    def test_array32_nested2(self): self.check_samples(self.samples_nested2, type_len=4)

    def test_fixarray_with_ext(self): self.check_samples(self.samples_with_ext, type_len=0)
    def test_array16_with_ext(self): self.check_samples(self.samples_with_ext, type_len=2)
    def test_array32_with_ext(self): self.check_samples(self.samples_with_ext, type_len=4)
    def test_fixarray_with_ext_nested(self): self.check_samples(self.samples_with_ext_nested, type_len=0)
    def test_array16_with_ext_nested(self): self.check_samples(self.samples_with_ext_nested, type_len=2)
    def test_array32_with_ext_nested(self): self.check_samples(self.samples_with_ext_nested, type_len=4)
    def test_fixarray_with_ext_nested2(self): self.check_samples(self.samples_with_ext_nested2, type_len=0)
    def test_array16_with_ext_nested2(self): self.check_samples(self.samples_with_ext_nested2, type_len=2)
    def test_array32_with_ext_nested2(self): self.check_samples(self.samples_with_ext_nested2, type_len=4)


class MsgPackMapTest(MsgPackTest):
    samples = [
        {},
        { 0: 111 },
        { 77: 'some value' },
        { 'some key': 111 },
        { 'some key': 'some value' },
        { 77: 'some value', 'some key': 111, 'some key': 'some value' },
        { 8: 2**8-1, 16: 2**16-1 },
        { 8: 2**8-1, 16: 2**16-1, 32: 2**32-1 },
    ]

    samples_nested = [
        { 0: samples[0] },
        { 0: samples[1], 8: 2**8-1 },
        { 8: 2**8-1, 42: samples[2] },
        { 0: samples[1], 8: 2**8-1, 42: samples[2] },
        { 0: samples[1], 8: samples[2], 42: samples[3] },
    ]

    samples_nested2 = [
        { 0: samples_nested[0] },
        { 0: samples_nested[1], 1: 2**8-1 },
        { 1: 2**8-1, 42: samples_nested[2] },
        { 0: samples_nested[1], 1: 2**8-1, 42: samples_nested[2] },
        { 0: samples_nested[1], 1: samples_nested[2], 42: samples_nested[3] },
        { 0: samples_nested[1], 1: 2**8-1, 8: samples[2], 42: samples_nested[3] },
    ]

    ext_classes = [
        MsgPackExtDecimalTest,
        MsgPackExtUuidTest,
        MsgPackExtDatetimeTest,
        MsgPackExtErrorTest,
        MsgPackExtCompressionTest,
        MsgPackExtIntervalTest,
        MsgPackExtTupleTest,
    ]

    @staticmethod
    def ext_key(ext_class):
        return 'key of the extension from {}'.format(ext_class.__name__)

    samples_with_ext = []
    for ext_class in ext_classes:
        samples_with_ext.append({ ext_key(ext_class): ext_class.samples[0] })
        samples_with_ext.append({ 'some key': 111, ext_key(ext_class): ext_class.samples[1] })
        samples_with_ext.append({ ext_key(ext_class): ext_class.samples[2], 77: 'some value' })
    samples_with_ext.append(dict(zip(
        map(ext_key, ext_classes),
        map(lambda ext_class: ext_class.samples[0], ext_classes),
    )))

    samples_with_ext_nested = [
        { 0: samples_with_ext[0] },
        { 0: samples_with_ext[1], 8: 2**8-1 },
        { 8: 2**8-1, 42: samples_with_ext[2] },
        { 0: samples_with_ext[1], 8: 2**8-1, 42: samples_with_ext[2] },
        { 0: samples_with_ext[1], 8: samples_with_ext[2], 42: samples_with_ext[3] },
    ]

    samples_with_ext_nested2 = [
        { 0: samples_with_ext_nested[0] },
        { 0: samples_with_ext_nested[1], 1: 2**8-1 },
        { 1: 2**8-1, 42: samples_with_ext_nested[2] },
        { 0: samples_with_ext_nested[1], 1: 2**8-1, 42: samples_with_ext_nested[2] },
        { 0: samples_with_ext_nested[1], 1: samples_with_ext_nested[2], 42: samples_with_ext_nested[3] },
        { 0: samples_with_ext_nested[1], 1: 2**8-1, 8: samples_with_ext[2], 42: samples_with_ext_nested[3] },
    ]

    def sample_ref(self, sample):
        return '' if len(sample) == 0 else \
            '{{{}}}'.format(', '.join(['[{}] = {}'.format(self.print(k), self.print(v)) for k,v in sample.items()]))

    def test_fixmap(self): self.check_samples(self.samples, type_len=0)
    def test_map16(self): self.check_samples(self.samples, type_len=2)
    def test_map32(self): self.check_samples(self.samples, type_len=4)
    def test_fixmap_nested(self): self.check_samples(self.samples_nested, type_len=0)
    def test_map16_nested(self): self.check_samples(self.samples_nested, type_len=2)
    def test_map32_nested(self): self.check_samples(self.samples_nested, type_len=4)
    def test_fixmap_nested2(self): self.check_samples(self.samples_nested2, type_len=0)
    def test_map16_nested2(self): self.check_samples(self.samples_nested2, type_len=2)
    def test_map32_nested2(self): self.check_samples(self.samples_nested2, type_len=4)

    def test_fixmap_with_ext(self): self.check_samples(self.samples_with_ext, type_len=0)
    def test_map16_with_ext(self): self.check_samples(self.samples_with_ext, type_len=2)
    def test_map32_with_ext(self): self.check_samples(self.samples_with_ext, type_len=4)
    def test_fixmap_with_ext_nested(self): self.check_samples(self.samples_with_ext_nested, type_len=0)
    def test_map16_with_ext_nested(self): self.check_samples(self.samples_with_ext_nested, type_len=2)
    def test_map32_with_ext_nested(self): self.check_samples(self.samples_with_ext_nested, type_len=4)
    def test_fixmap_with_ext_nested2(self): self.check_samples(self.samples_with_ext_nested2, type_len=0)
    def test_map16_with_ext_nested2(self): self.check_samples(self.samples_with_ext_nested2, type_len=2)
    def test_map32_with_ext_nested2(self): self.check_samples(self.samples_with_ext_nested2, type_len=4)


class MsgPackInvalidTest(MsgPackTest):
    samples_aux = [True, 33, -55, 123.456, 'abc', b'\x12\x23\x34']

    samples = [MsgPackEnc.INVALID]

    samples_arrays = []
    for i in range(len(samples_aux) + 1):
        samples_arrays.append(list(samples_aux))
        samples_arrays[i].insert(i, MsgPackEnc.INVALID)

    samples_map_keys = [{MsgPackEnc.INVALID: x} for x in samples_aux]
    samples_map_vals = [{x: MsgPackEnc.INVALID} for x in samples_aux]

    def invalid_sample_regex(self, sample):
        return 'MsgPack: invalid format at '

    def test_invalid(self): self.check_invalid_samples(self.samples)
    def test_invalid_arrays(self): self.check_invalid_samples(self.samples_arrays)
    def test_invalid_map_keys(self): self.check_invalid_samples(self.samples_map_keys)
    def test_invalid_map_vals(self): self.check_invalid_samples(self.samples_map_vals)


unittest.main(exit=False, verbosity=2)

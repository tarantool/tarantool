import struct
import re
import ctypes

# IPROTO header is always 3 4-byte ints:
# command code, length, request id
INT_FIELD_LEN = 4
INT_BER_MAX_LEN = 5
IPROTO_HEADER_LEN = 3*INT_FIELD_LEN
INSERT_REQUEST_FIXED_LEN = 2*INT_FIELD_LEN
UPDATE_REQUEST_FIXED_LEN = 2*INT_FIELD_LEN
DELETE_REQUEST_FIXED_LEN = 2*INT_FIELD_LEN
SELECT_REQUEST_FIXED_LEN = 5*INT_FIELD_LEN
PACKET_BUF_LEN = 2048

UPDATE_SET_FIELD_OPCODE = 0

# command code in IPROTO header

INSERT_REQUEST_TYPE = 13
SELECT_REQUEST_TYPE = 17
UPDATE_REQUEST_TYPE = 19
DELETE_REQUEST_TYPE = 21
CALL_REQUEST_TYPE = 22
PING_REQUEST_TYPE = 65280

ER = {
    0: "ER_OK"                  ,
    1: "ER_NONMASTER"           ,
    2: "ER_ILLEGAL_PARAMS"      ,
    3: "ER_UNUSED3"             ,
    4: "ER_TUPLE_IS_RO"         ,
    5: "ER_UNUSED5"             ,
    6: "ER_UNUSED6"             ,
    7: "ER_MEMORY_ISSUE"        ,
    8: "ER_UNUSED8"             ,
    9: "ER_INJECTION"           ,
   10: "ER_UNSUPPORTED"         ,
   11: "ER_RESERVED11"          ,
   12: "ER_RESERVED12"          ,
   13: "ER_RESERVED13"          ,
   14: "ER_RESERVED14"          ,
   15: "ER_RESERVED15"          ,
   16: "ER_RESERVED16"          ,
   17: "ER_RESERVED17"          ,
   18: "ER_RESERVED18"          ,
   19: "ER_RESERVED19"          ,
   20: "ER_RESERVED20"          ,
   21: "ER_RESERVED21"          ,
   22: "ER_RESERVED22"          ,
   23: "ER_RESERVED23"          ,
   24: "ER_UNUSED24"            ,
   25: "ER_TUPLE_IS_EMPTY"      ,
   26: "ER_UNUSED26"            ,
   27: "ER_UNUSED27"            ,
   28: "ER_UNUSED28"            ,
   29: "ER_UNUSED29"            ,
   30: "ER_UNUSED30"            ,
   31: "ER_UNUSED31"            ,
   32: "ER_UNUSED32"            ,
   33: "ER_UNUSED33"            ,
   34: "ER_UNUSED34"            ,
   35: "ER_UNUSED35"            ,
   36: "ER_UNUSED36"            ,
   37: "ER_UNUSED37"            ,
   38: "ER_KEY_FIELD_TYPE"      ,
   39: "ER_WAL_IO"              ,
   40: "ER_FIELD_TYPE"          ,
   41: "ER_ARG_TYPE"            ,
   42: "ER_SPLICE"              ,
   43: "ER_TUPLE_IS_TOO_LONG"   ,
   44: "ER_UNKNOWN_UPDATE_OP"   ,
   45: "ER_EXACT_MATCH"         ,
   46: "ER_UNUSED46"            ,
   47: "ER_KEY_PART_COUNT"      ,
   48: "ER_PROC_RET"            ,
   49: "ER_TUPLE_NOT_FOUND"     ,
   50: "ER_NO_SUCH_PROC"        ,
   51: "ER_PROC_LUA"            ,
   52: "ER_SPACE_DISABLED"      ,
   53: "ER_NO_SUCH_INDEX"       ,
   54: "ER_NO_SUCH_FIELD"       ,
   55: "ER_TUPLE_FOUND"         ,
   56: "ER_INDEX_VIOLATION"     ,
   57: "ER_NO_SUCH_SPACE"
}

def format_error(return_code, response):
    return "An error occurred: {0}, '{1}'".format(ER[return_code >> 8],
                                                  response[4:-1])


def save_varint32(value):
    """Implement Perl pack's 'w' option, aka base 128 encoding."""
    res = ''
    if value >= 1 << 7:
        if value >= 1 << 14:
            if value >= 1 << 21:
                if value >= 1 << 28:
                    res += chr(value >> 28 & 0xff | 0x80)
                res += chr(value >> 21 & 0xff | 0x80)
            res += chr(value >> 14 & 0xff | 0x80)
        res += chr(value >> 7 & 0xff | 0x80)
    res += chr(value & 0x7F)

    return res


def read_varint32(varint, offset):
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


def opt_resize_buf(buf, newsize):
    if len(buf) < newsize:
        return ctypes.create_string_buffer(buf.value, max(2*len, newsize))
    return buf


def pack_field(value, buf, offset):
    if type(value) is int or type(value) is long:
        if value > 0xffffffff:
            raise RuntimeError("Integer value is too big")
        buf = opt_resize_buf(buf, offset + INT_FIELD_LEN)
        struct.pack_into("<cL", buf, offset, chr(INT_FIELD_LEN), value)
        offset += INT_FIELD_LEN + 1
    elif type(value) is str:
        opt_resize_buf(buf, offset + INT_BER_MAX_LEN + len(value))
        value_len_ber = save_varint32(len(value))
        struct.pack_into("{0}s{1}s".format(len(value_len_ber), len(value)),
                         buf, offset, value_len_ber, value)
        offset += len(value_len_ber) + len(value)
    else:
        raise RuntimeError("Unsupported value type in value list")
    return (buf, offset)


def pack_tuple(value_list, buf, offset):
    """Represents <tuple> rule in tarantool protocol description.
    Pack tuple into a binary representation.
    buf and offset are in-out parameters, offset is advanced
    to the amount of bytes that it took to pack the tuple"""

    # length of int field: 1 byte - field len (is always 4), 4 bytes - data
    # max length of compressed integer
    cardinality = len(value_list)
    struct.pack_into("<L", buf, offset, cardinality)
    offset += INT_FIELD_LEN
    for value in value_list:
        (buf, offset) = pack_field(value, buf, offset)

    return buf, offset


def pack_operation_list(update_list, buf, offset):
    buf = opt_resize_buf(buf, offset + INT_FIELD_LEN)
    struct.pack_into("<L", buf, offset, len(update_list))
    offset += INT_FIELD_LEN
    for update in update_list:
        opt_resize_buf(buf, offset + INT_FIELD_LEN + 1)
        struct.pack_into("<Lc", buf, offset,
                         update[0],
                         chr(UPDATE_SET_FIELD_OPCODE))
        offset += INT_FIELD_LEN + 1
        (buf, offset) = pack_field(update[1], buf, offset)

    return (buf, offset)


def unpack_tuple(response, offset):
    (size, cardinality) = struct.unpack("<LL", response[offset:offset + 8])
    offset += 8
    res = []
    while len(res) < cardinality:
        (data_len, offset) = read_varint32(response, offset)
        data = response[offset:offset+data_len]
        offset += data_len
        if data_len == 4:
            (data,) = struct.unpack("<L", data)
            res.append((str(data)))
        else:
            res.append("'" + data + "'")

    return '[' + ', '.join(res) + ']', offset


class StatementPing:
    reqeust_type = PING_REQUEST_TYPE
    def pack(self):
        return ""

    def unpack(self, response):
        return "ok\n---"

class StatementInsert(StatementPing):
    reqeust_type = INSERT_REQUEST_TYPE

    def __init__(self, table_name, value_list):
        self.space_no = table_name
        self.flags = 0x02 # ADD
        self.value_list = value_list

    def pack(self):
        buf = ctypes.create_string_buffer(PACKET_BUF_LEN)
        (buf, offset) = pack_tuple(self.value_list, buf, INSERT_REQUEST_FIXED_LEN)
        struct.pack_into("<LL", buf, 0, self.space_no, self.flags)
        return buf[:offset]

    def unpack(self, response):
        (return_code,) = struct.unpack("<L", response[:4])
        if return_code:
            return format_error(return_code, response)
        (tuple_count,) = struct.unpack("<L", response[4:8])
        return "Insert OK, {0} row affected".format(tuple_count)

class StatementReplace(StatementPing):
    reqeust_type = INSERT_REQUEST_TYPE

    def __init__(self, table_name, value_list):
        self.space_no = table_name
        self.flags = 0x04 # REPLACE
        self.value_list = value_list

    def pack(self):
        buf = ctypes.create_string_buffer(PACKET_BUF_LEN)
        (buf, offset) = pack_tuple(self.value_list, buf, INSERT_REQUEST_FIXED_LEN)
        struct.pack_into("<LL", buf, 0, self.space_no, self.flags)
        return buf[:offset]

    def unpack(self, response):
        (return_code,) = struct.unpack("<L", response[:4])
        if return_code:
            return format_error(return_code, response)
        (tuple_count,) = struct.unpack("<L", response[4:8])
        return "Replace OK, {0} row affected".format(tuple_count)

class StatementUpdate(StatementPing):
    reqeust_type = UPDATE_REQUEST_TYPE

    def __init__(self, table_name, update_list, where):
        self.space_no = table_name
        self.flags = 0
        key_no = where[0]
        if key_no != 0:
            raise RuntimeError("UPDATE can only be made by the primary key (#0)")
        self.value_list = where[1:]
        self.update_list = update_list

    def pack(self):
        buf = ctypes.create_string_buffer(PACKET_BUF_LEN)
        struct.pack_into("<LL", buf, 0, self.space_no, self.flags)
        (buf, offset) = pack_tuple(self.value_list, buf, UPDATE_REQUEST_FIXED_LEN)
        (buf, offset) = pack_operation_list(self.update_list, buf, offset)
        return buf[:offset]

    def unpack(self, response):
        (return_code,) = struct.unpack("<L", response[:4])
        if return_code:
            return format_error(return_code, response)
        (tuple_count,) = struct.unpack("<L", response[4:8])
        return "Update OK, {0} row affected".format(tuple_count)

class StatementDelete(StatementPing):
    reqeust_type = DELETE_REQUEST_TYPE

    def __init__(self, table_name, where):
        self.space_no = table_name
        self.flags = 0
        key_no = where[0]
        if key_no != 0:
            raise RuntimeError("DELETE can only be made by the primary key (#0)")
        self.value_list = where[1:]

    def pack(self):
        buf = ctypes.create_string_buffer(PACKET_BUF_LEN)
        (buf, offset) = pack_tuple(self.value_list, buf, DELETE_REQUEST_FIXED_LEN)
        struct.pack_into("<LL", buf, 0, self.space_no, self.flags)
        return buf[:offset]

    def unpack(self, response):
        (return_code,) = struct.unpack("<L", response[:4])
        if return_code:
            return format_error(return_code, response)
        (tuple_count,) = struct.unpack("<L", response[4:8])
        return "Delete OK, {0} row affected".format(tuple_count)

class StatementSelect(StatementPing):
    reqeust_type = SELECT_REQUEST_TYPE

    def __init__(self, table_name, where, limit):
        self.space_no = table_name
        self.index_no = None
        self.key_list = []
        if not where:
            self.index_no = 0
            self.key_list = ["",]
        else:
            for (index_no, key) in where:
                self.key_list.append(key)
                if self.index_no == None:
                    self.index_no = index_no
                elif self.index_no != index_no:
                    raise RuntimeError("All key values in a disjunction must refer to the same index")
        self.offset = 0
        self.limit = limit

    def pack(self):
        buf = ctypes.create_string_buffer(PACKET_BUF_LEN)
        struct.pack_into("<LLLLL", buf, 0,
                         self.space_no,
                         self.index_no,
                         self.offset,
                         self.limit,
                         len(self.key_list))
        offset = SELECT_REQUEST_FIXED_LEN

        for key in self.key_list:
            (buf, offset) = pack_tuple([key], buf, offset)

        return buf[:offset]

    def unpack(self, response):
        (return_code,) = struct.unpack("<L", response[:4])
        if return_code:
            return format_error(return_code, response)
        (tuple_count,) = struct.unpack("<L", response[4:8])
        tuples = []
        offset = 8
        while len(tuples) < tuple_count:
            (next_tuple, offset) = unpack_tuple(response, offset)
            tuples.append(next_tuple)
        if self.sort:
            tuples.sort()
        if tuple_count == 0:
            return "No match"
        elif tuple_count == 1:
            return "Found 1 tuple:\n" + tuples[0]
        else:
            return "Found {0} tuples:\n".format(tuple_count) + "\n".join(tuples)

class StatementCall(StatementSelect):
    reqeust_type = CALL_REQUEST_TYPE

    def __init__(self, proc_name, value_list):
        self.proc_name = proc_name
# the binary protocol passes everything into procedure as strings
# convert input to strings to avoid data mangling by the protocol
        self.value_list = map(lambda val: str(val), value_list)

    def pack(self):
        buf = ctypes.create_string_buffer(PACKET_BUF_LEN)
        offset = 0
        struct.pack_into("<L", buf, offset, 0) # flags
        offset += INT_FIELD_LEN
        (buf, offset) = pack_field(self.proc_name, buf, offset)
        (buf, offset) = pack_tuple(self.value_list, buf, offset)
        return buf[:offset]

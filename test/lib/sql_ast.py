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
DELETE_REQUEST_FIXED_LEN = INT_FIELD_LEN
SELECT_REQUEST_FIXED_LEN = 5*INT_FIELD_LEN
PACKET_BUF_LEN = 2048

UPDATE_SET_FIELD_OPCODE = 0

# command code in IPROTO header

INSERT_REQUEST_TYPE = 13
SELECT_REQUEST_TYPE = 17
UPDATE_REQUEST_TYPE = 19
DELETE_REQUEST_TYPE = 20
PING_REQUEST_TYPE = 65280

ER = {
 0x00000000: ("ERR_CODE_OK"                  , "OK")                                      ,
 0x00000102: ("ERR_CODE_NONMASTER"           ,  "Non master connection, but it should be"),
 0x00000202: ("ERR_CODE_ILLEGAL_PARAMS"      ,  "Illegal parameters")                     ,
 0x00000302: ("ERR_CODE_BAD_UID"             ,  "Uid not from this storage range")        ,
 0x00000401: ("ERR_CODE_NODE_IS_RO"          ,  "Node is marked as read-only")            ,
 0x00000501: ("ERR_CODE_NODE_IS_NOT_LOCKED"  ,  "Node isn't locked")                      ,
 0x00000601: ("ERR_CODE_NODE_IS_LOCKED"      ,  "Node is locked")                         ,
 0x00000701: ("ERR_CODE_MEMORY_ISSUE"        ,  "Some memory issues")                     ,
 0x00000802: ("ERR_CODE_BAD_INTEGRITY"       ,  "Bad graph integrity")                    ,
 0x00000a02: ("ERR_CODE_UNSUPPORTED_COMMAND" ,  "Unsupported command")                    ,
 0x00001801: ("ERR_CODE_CANNOT_REGISTER"     ,  "Can not register new user")              ,
 0x00001a01: ("ERR_CODE_CANNOT_INIT_ALERT_ID",  "Can not generate alert id")              ,
 0x00001b02: ("ERR_CODE_CANNOT_DEL"          ,  "Can\'t del node")                        ,
 0x00001c02: ("ERR_CODE_USER_NOT_REGISTERED" ,  "User isn\'t registered")                 ,
 0x00001d02: ("ERR_CODE_SYNTAX_ERROR"        ,  "Syntax error in query")                  ,
 0x00001e02: ("ERR_CODE_WRONG_FIELD"         ,  "Unknown field")                          ,
 0x00001f02: ("ERR_CODE_WRONG_NUMBER"        ,  "Number value is out of range")           ,
 0x00002002: ("ERR_CODE_DUPLICATE"           ,  "Insert already existing object")         ,
 0x00002202: ("ERR_CODE_UNSUPPORTED_ORDER"   ,  "Can not order result")                   ,
 0x00002302: ("ERR_CODE_MULTIWRITE"          ,  "Multiple to update/delete")              ,
 0x00002400: ("ERR_CODE_NOTHING"             ,  "nothing to do (not an error)")           ,
 0x00002502: ("ERR_CODE_UPDATE_ID"           ,  "id\'s update")                           ,
 0x00002602: ("ERR_CODE_WRONG_VERSION"       ,  "Unsupported version of protocol")        ,
 0x00002702: ("ERR_CODE_UNKNOWN_ERROR"       ,  "")                                       ,
 0x00003102: ("ERR_CODE_NODE_NOT_FOUND"      ,  "")                                       ,
 0x00003702: ("ERR_CODE_NODE_FOUND"          ,  "")                                       ,
 0x00003802: ("ERR_CODE_INDEX_VIOLATION"     ,  "")                                       ,
}

def format_error(return_code):
  return "An error occurred: {0}, \'{1}'".format(ER[return_code][0],
                                                 ER[return_code][1])


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
  if type(value) is int:
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
  (size,cardinality) = struct.unpack("<LL", response[offset:offset + 8])
  offset += 8
  res = []
  while len(res) < cardinality:
    (data_len, offset) = read_varint32(response, offset)
    data = response[offset:offset+data_len]
    offset += data_len
    if data_len == 4:
      (data,) = struct.unpack("<L", data)
    res.append(data)
  return str(res), offset

   
class StatementPing:
  reqeust_type = PING_REQUEST_TYPE
  def pack(self):
    return ""

  def unpack(self, response):
    return "ok\n---"

class StatementInsert(StatementPing):
  reqeust_type = INSERT_REQUEST_TYPE

  def __init__(self, table_name, value_list):
    self.namespace_no = table_name
    self.flags = 0
    self.value_list = value_list

  def pack(self):
    buf = ctypes.create_string_buffer(PACKET_BUF_LEN)
    (buf, offset) = pack_tuple(self.value_list, buf, INSERT_REQUEST_FIXED_LEN)
    struct.pack_into("<LL", buf, 0, self.namespace_no, self.flags)
    return buf[:offset]

  def unpack(self, response):
    (return_code,) = struct.unpack("<L", response[:4])
    if return_code:
      return format_error(return_code)
    (result_code, row_count) = struct.unpack("<LL", response)
    return "Insert OK, {0} row affected".format(row_count)


class StatementUpdate(StatementPing):
  reqeust_type = UPDATE_REQUEST_TYPE

  def __init__(self, table_name, update_list, where):
    self.namespace_no = table_name
    self.flags = 0
    key_no = where[0]
    if key_no != 0:
      raise RuntimeError("UPDATE can only be made by the primary key (#0)")
    self.value_list = where[1:]
    self.update_list = update_list

  def pack(self):
    buf = ctypes.create_string_buffer(PACKET_BUF_LEN)
    struct.pack_into("<LL", buf, 0, self.namespace_no, self.flags)
    (buf, offset) = pack_tuple(self.value_list, buf, UPDATE_REQUEST_FIXED_LEN)
    (buf, offset) = pack_operation_list(self.update_list, buf, offset)
    return buf[:offset]

  def unpack(self, response):
    (return_code,) = struct.unpack("<L", response[:4])
    if return_code:
      return format_error(return_code)
    (result_code, row_count) = struct.unpack("<LL", response)
    return "Update OK, {0} row affected".format(row_count)

class StatementDelete(StatementPing):
  reqeust_type = DELETE_REQUEST_TYPE

  def __init__(self, table_name, where):
    self.namespace_no = table_name
    key_no = where[0]
    if key_no != 0:
      raise RuntimeError("DELETE can only be made by the primary key (#0)")
    self.value_list = where[1:]

  def pack(self):
    buf = ctypes.create_string_buffer(PACKET_BUF_LEN)
    (buf, offset) = pack_tuple(self.value_list, buf, DELETE_REQUEST_FIXED_LEN)
    struct.pack_into("<L", buf, 0, self.namespace_no)
    return buf[:offset]

  def unpack(self, response):
    (return_code,) = struct.unpack("<L", response[:4])
    if return_code:
      return format_error(return_code)
    (result_code, row_count) = struct.unpack("<LL", response)
    return "Delete OK, {0} row affected".format(row_count)

class StatementSelect(StatementPing):
  reqeust_type = SELECT_REQUEST_TYPE

  def __init__(self, table_name, where):
    self.namespace_no = table_name
    if where:
      (self.index_no, key) = where
      self.key = [key]
    else:
      self.index_no = 0
      self.key = [""]
    self.offset = 0
    self.limit = 0xffffffff

  def pack(self):
    buf = ctypes.create_string_buffer(PACKET_BUF_LEN)
    struct.pack_into("<LLLLL", buf, 0,
                     self.namespace_no,
                     self.index_no,
                     self.offset,
                     self.limit,
                     1)
    (buf, offset) = pack_tuple(self.key, buf, SELECT_REQUEST_FIXED_LEN)

    return buf[:offset]

  def unpack(self, response):
    if len(response) == 4:
      (return_code,) = struct.unpack("<L", response[:4])
      return format_error(return_code)
    (tuple_count,) = struct.unpack("<L", response[4:8])
    tuples = []
    offset = 8
    while len(tuples) < tuple_count:
      (next_tuple, offset) = unpack_tuple(response, offset)
      tuples.append(next_tuple)
    if tuple_count == 0:
      return "No match"
    elif tuple_count == 1:
      return "Found 1 tuple:\n" + tuples[0]
    else:
      return "Found {0} tuples:\n".format(tuple_count) + "\n".join(tuples)


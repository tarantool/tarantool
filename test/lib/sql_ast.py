import os
import re
import sys
import ctypes
import struct

from lib.test_suite import check_libs

check_libs()
from tarantool.request import (
        RequestPing,
        RequestInsert,
        RequestSelect,
        RequestCall,
        RequestUpdate,
        RequestDelete,
)

ER = {
     0: "ER_OK"                 ,
     1: "ER_ILLEGAL_PARAMS"     ,
     2: "ER_MEMORY_ISSUE"       ,
     3: "ER_TUPLE_FOUND"        ,
     4: "ER_TUPLE_NOT_FOUND"    ,
     5: "ER_UNSUPPORTED"        ,
     6: "ER_NONMASTER"          ,
     7: "ER_SECONDARY"          ,
     8: "ER_INJECTION"          ,
     9: "ER_CREATE_SPACE"       ,
    10: "ER_SPACE_EXISTS"       ,
    11: "ER_DROP_SPACE"         ,
    12: "ER_ALTER_SPACE"        ,
    13: "ER_INDEX_TYPE"         ,
    14: "ER_MODIFY_INDEX"       ,
    15: "ER_LAST_DROP"          ,
    16: "ER_TUPLE_FORMAT_LIMIT" ,
    17: "ER_DROP_PRIMARY_KEY"   ,
    18: "ER_KEY_FIELD_TYPE"     ,
    19: "ER_EXACT_MATCH"        ,
    20: "ER_INVALID_MSGPACK"    ,
    21: "ER_PROC_RET"           ,
    22: "ER_TUPLE_NOT_ARRAY"    ,
    23: "ER_FIELD_TYPE"         ,
    24: "ER_FIELD_TYPE_MISMATCH",
    25: "ER_SPLICE"             ,
    26: "ER_ARG_TYPE"           ,
    27: "ER_TUPLE_IS_TOO_LONG"  ,
    28: "ER_UNKNOWN_UPDATE_OP"  ,
    29: "ER_UPDATE_FIELD"       ,
    30: "ER_FIBER_STACK"        ,
    31: "ER_KEY_PART_COUNT"     ,
    32: "ER_PROC_LUA"           ,
    33: "ER_NO_SUCH_PROC"       ,
    34: "ER_NO_SUCH_TRIGGER"    ,
    35: "ER_NO_SUCH_INDEX"      ,
    36: "ER_NO_SUCH_SPACE"      ,
    37: "ER_NO_SUCH_FIELD"      ,
    38: "ER_SPACE_ARITY"        ,
    39: "ER_INDEX_ARITY"        ,
    40: "ER_WAL_IO"
}

errstr = """---
- error:
    errcode: {0}
    errmsg: {1}
..."""

def format_error(response):
    return errstr.format(
        ER.get(response.return_code, "ER_UNKNOWN (%d)" % response.return_code),
        response.return_message)

def format_yamllike(response):
    table = ("\n"+"\n".join(["- "+str(list(k)) for k in response])) \
            if len(response) else ""
    return "---{0}\n...".format(table)

class Statement(object):
    def __init__(self):
        pass
    def pack(self, connection):
        pass
    def unpack(self, response):
        pass

class StatementPing(Statement):
    def pack(self, connection):
        return RequestPing(connection)

    def unpack(self, response):
        if response._return_code:
            return format_error(response)
        return "---\n- ok\n..."

class StatementInsert(Statement):
    def __init__(self, table_name, value_list):
        self.space_no = table_name
        self.flags = 0x03 # ADD + RET
        self.value_list = value_list

    def pack(self, connection):
        return RequestInsert(connection, self.space_no, self.value_list,
                self.flags)

    def unpack(self, response):
        if response.return_code:
            return format_error(response)
        return format_yamllike(response)

class StatementReplace(Statement):
    def __init__(self, table_name, value_list):
        self.space_no = table_name
        self.flags = 0x05 # REPLACE + RET
        self.value_list = value_list

    def pack(self, connection):
        return RequestInsert(connection, self.space_no, self.value_list,
                self.flags)

    def unpack(self, response):
        if response.return_code:
            return format_error(response)
        return format_yamllike(response)

class StatementUpdate(Statement):
    def __init__(self, table_name, update_list, where):
        self.space_no = table_name
        self.flags = 0
        self.key_no = where[0]
        if self.key_no != 0:
            raise RuntimeError("UPDATE can only be made by the"
                    " primary key (#0)")
        self.value_list = where[1]
        self.update_list = [(pair[0], '=', pair[1]) for pair in update_list]

    def pack(self, connection):
        return RequestUpdate(connection, self.space_no, self.value_list,
                self.update_list, True)

    def unpack(self, response):
        if response.return_code:
            return format_error(response)
        return format_yamllike(response)

class StatementDelete(Statement):
    def __init__(self, table_name, where):
        self.space_no = table_name
        self.flags = 0
        key_no = where[0]
        if key_no != 0:
            raise RuntimeError("DELETE can only be made by the "
                    "primary key (#0)")
        self.value_list = where[1]

    def pack(self, connection):
        return RequestDelete(connection, self.space_no, self.value_list, True)

    def unpack(self, response):
        if response.return_code:
            return format_error(response)
        return format_yamllike(response)

class StatementSelect(Statement):
    def __init__(self, table_name, where, limit):
        self.space_no = table_name
        self.index_no = None
        self.key_list = []
        if not where:
            self.index_no = 0
            self.key_list = [[]]
        else:
            for (index_no, key) in where:
                self.key_list.append([key, ])
                if self.index_no == None:
                    self.index_no = index_no
                elif self.index_no != index_no:
                    raise RuntimeError("All key values in a disjunction must "
                            "refer to the same index")
        self.offset = 0
        self.limit = limit

    def pack(self, connection):
        return RequestSelect(connection, self.space_no, self.index_no,
                self.key_list , self.offset, self.limit)

    def unpack(self, response):
        if response.return_code:
            return format_error(response)
        if self.sort:
            response = sorted(response[0:])
        return format_yamllike(response)

class StatementCall(StatementSelect):
    def __init__(self, proc_name, value_list):
        self.proc_name = proc_name
        self.value_list = value_list

    def pack(self, connection):
        return RequestCall(connection, self.proc_name, self.value_list, True)

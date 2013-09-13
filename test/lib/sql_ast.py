import os
import re
import sys
import ctypes
import struct

try:
    tnt_py = os.path.dirname(os.path.abspath(__file__))
    tnt_py = os.path.join(tnt_py, 'tarantool-python/src')
    sys.path.append(tnt_py)
    from tarantool.request import (
            RequestPing,
            RequestInsert,
            RequestSelect,
            RequestCall,
            RequestUpdate,
            RequestDelete,
    )
except ImportError:
    sys.stderr.write("\n\nNo tarantool-python library found\n")
    sys.exit(1)

ER = {
    0: "ER_OK"                  ,
    1: "ER_NONMASTER"           ,
    2: "ER_ILLEGAL_PARAMS"      ,
    3: "ER_SECONDARY"           ,
    4: "ER_TUPLE_IS_RO"         ,
    5: "ER_INDEX_TYPE"          ,
    6: "ER_SPACE_EXISTS"        ,
    7: "ER_MEMORY_ISSUE"        ,
    8: "ER_CREATE_SPACE"        ,
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
   24: "ER_DROP_SPACE"          ,
   25: "ER_ALTER_SPACE"         ,
   26: "ER_FIBER_STACK"         ,
   27: "ER_MODIFY_INDEX"        ,
   28: "ER_TUPLE_FORMAT_LIMIT"  ,
   29: "ER_LAST_DROP"           ,
   30: "ER_DROP_PRIMARY_KEY"    ,
   31: "ER_SPACE_ARITY"         ,
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
   46: "ER_FIELD_TYPE_MISMATCH" ,
   47: "ER_KEY_PART_COUNT"      ,
   48: "ER_PROC_RET"            ,
   49: "ER_TUPLE_NOT_FOUND"     ,
   50: "ER_NO_SUCH_PROC"        ,
   51: "ER_PROC_LUA"            ,
   52: "ER_SPACE_DISABLED"      ,
   53: "ER_NO_SUCH_INDEX"       ,
   54: "ER_NO_SUCH_FIELD"       ,
   55: "ER_TUPLE_FOUND"         ,
   56: "ER_UNUSED"              ,
   57: "ER_NO_SUCH_SPACE"
}

def format_error(response):
    return "---\n- error: '{1}'\n...".format(ER[response.return_code],
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
# the binary protocol passes everything into procedure as strings
# convert input to strings to avoid data mangling by the protocol
        self.value_list = map(lambda val: str(val), value_list)

    def pack(self, connection):
        return RequestCall(connection, self.proc_name, self.value_list, True)

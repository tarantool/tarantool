local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

local is_enterprise = t.tarantool.is_enterprise_package()

g.before_all(function(cg)
    cg.server = server:new{
        alias   = 'dflt',
    }
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

local reference_table = {
    -- `IPROTO_FLAGS` bitfield constants enumeration.
    flag = {
        COMMIT = 0x01,
        WAIT_SYNC = 0x02,
        WAIT_ACK = 0x04,
    },

    -- `iproto_key` enumeration.
    key = {
        REQUEST_TYPE = 0x00,
        SYNC = 0x01,
        REPLICA_ID = 0x02,
        LSN = 0x03,
        TIMESTAMP = 0x04,
        SCHEMA_VERSION = 0x05,
        SERVER_VERSION = 0x06,
        GROUP_ID = 0x07,
        TSN = 0x08,
        FLAGS = 0x09,
        STREAM_ID = 0x0a,
        SPACE_ID = 0x10,
        INDEX_ID = 0x11,
        LIMIT = 0x12,
        OFFSET = 0x13,
        ITERATOR = 0x14,
        INDEX_BASE = 0x15,
        FETCH_POSITION = 0x1f,
        KEY = 0x20,
        TUPLE = 0x21,
        FUNCTION_NAME = 0x22,
        USER_NAME = 0x23,
        INSTANCE_UUID = 0x24,
        REPLICASET_UUID = 0x25,
        VCLOCK = 0x26,
        EXPR = 0x27,
        OPS = 0x28,
        BALLOT = 0x29,
        TUPLE_META = 0x2a,
        OPTIONS = 0x2b,
        OLD_TUPLE = 0x2c,
        NEW_TUPLE = 0x2d,
        AFTER_POSITION = 0x2e,
        AFTER_TUPLE = 0x2f,
        DATA = 0x30,
        ERROR_24 = 0x31,
        METADATA = 0x32,
        BIND_METADATA = 0x33,
        BIND_COUNT = 0x34,
        POSITION = 0x35,
        SQL_TEXT = 0x40,
        SQL_BIND = 0x41,
        SQL_INFO = 0x42,
        STMT_ID = 0x43,
        REPLICA_ANON = 0x50,
        ID_FILTER = 0x51,
        ERROR = 0x52,
        TERM = 0x53,
        VERSION = 0x54,
        FEATURES = 0x55,
        TIMEOUT = 0x56,
        EVENT_KEY = 0x57,
        EVENT_DATA = 0x58,
        TXN_ISOLATION = 0x59,
        VCLOCK_SYNC = 0x5a,
        AUTH_TYPE = 0x5b,
        REPLICASET_NAME = 0x5c,
        INSTANCE_NAME = 0x5d,
        SPACE_NAME = 0x5e,
        INDEX_NAME = 0x5f,
        TUPLE_FORMATS = 0x60,
        IS_SYNC = 0x61,
        IS_CHECKPOINT_JOIN = 0x62,
        CHECKPOINT_VCLOCK = 0x63,
        CHECKPOINT_LSN = 0x64,
        PREV_TERM = 0x71,
        WAIT_ACK = 0x72,
    },

    -- `iproto_metadata_key` enumeration.
    metadata_key = {
        NAME = 0,
        TYPE = 1,
        COLL = 2,
        IS_NULLABLE = 3,
        IS_AUTOINCREMENT = 4,
        SPAN = 5,
    },

    -- `iproto_ballot_key` enumeration.
    ballot_key = {
        IS_RO_CFG = 0x01,
        VCLOCK = 0x02,
        GC_VCLOCK = 0x03,
        IS_RO = 0x04,
        IS_ANON = 0x05,
        IS_BOOTED = 0x06,
        CAN_LEAD = 0x07,
        BOOTSTRAP_LEADER_UUID = 0x08,
        REGISTERED_REPLICA_UUIDS = 0x09,
        INSTANCE_NAME = 0x0a,
    },

    -- `iproto_type` enumeration.
    type = {
        OK = 0,
        SELECT = 1,
        INSERT = 2,
        REPLACE = 3,
        UPDATE = 4,
        DELETE = 5,
        CALL_16 = 6,
        AUTH = 7,
        EVAL = 8,
        UPSERT = 9,
        CALL = 10,
        EXECUTE = 11,
        NOP = 12,
        PREPARE = 13,
        BEGIN = 14,
        COMMIT = 15,
        ROLLBACK = 16,
        RAFT = 30,
        RAFT_PROMOTE = 31,
        RAFT_DEMOTE = 32,
        RAFT_CONFIRM = 40,
        RAFT_ROLLBACK = 41,
        PING = 64,
        JOIN = 65,
        SUBSCRIBE = 66,
        VOTE_DEPRECATED = 67,
        VOTE = 68,
        FETCH_SNAPSHOT = 69,
        REGISTER = 70,
        JOIN_META = 71,
        JOIN_SNAPSHOT = 72,
        ID = 73,
        WATCH = 74,
        UNWATCH = 75,
        EVENT = 76,
        WATCH_ONCE = 77,
        CHUNK = 128,
        TYPE_ERROR = bit.lshift(1, 15),
        UNKNOWN = -1,
    },

    -- `iproto_raft_keys` enumeration
    raft_key = {
        TERM = 0,
        VOTE = 1,
        STATE = 2,
        VCLOCK = 3,
        LEADER_ID = 4,
        IS_LEADER_SEEN = 5,
    },

    -- `IPROTO_CURRENT_VERSION` constant
    protocol_version = 9,

    -- `feature_id` enumeration
    protocol_features = {
        streams = true,
        transactions = true,
        error_extension = true,
        watchers = true,
        pagination = true,
        space_and_index_names = true,
        watch_once = true,
        dml_tuple_extension = true,
        call_ret_tuple_extension = true,
        call_arg_tuple_extension = true,
        fetch_snapshot_cursor = is_enterprise and true or nil,
        is_sync = true,
    },
    feature = {
        streams = 0,
        transactions = 1,
        error_extension = 2,
        watchers = 3,
        pagination = 4,
        space_and_index_names = 5,
        watch_once = 6,
        dml_tuple_extension = 7,
        call_ret_tuple_extension = 8,
        call_arg_tuple_extension = 9,
        fetch_snapshot_cursor = 10,
        is_sync = 11,
    },
}

-- Checks that IPROTO constants and features are exported correctly.
g.test_iproto_constants_and_features_export = function(cg)
    cg.server:exec(function(reference_table)
        for k, v in pairs(reference_table) do
            t.assert_equals(box.iproto[k], v)
        end
    end, {reference_table})
end

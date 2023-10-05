#!/usr/bin/env tarantool
local os = require('os')
local tarantool = require('tarantool')

local msgpack = require('msgpack')

box.cfg{
    listen              = os.getenv("LISTEN"),
    memtx_memory        = 107374182,
    pid_file            = "tarantool.pid",
    memtx_allocator     = os.getenv("MEMTX_ALLOCATOR")
}

require('console').listen(os.getenv('ADMIN'))

local _hide = {
    pid_file=1, log=1, listen=1, vinyl_dir=1,
    memtx_dir=1, wal_dir=1,
    memtx_max_tuple_size=1, memtx_min_tuple_size=1,
    replication_sync_timeout=1, memtx_allocator=1
}

local _enterprise_keys = {
    audit_log = true,
    audit_nonblock = true,
    audit_format = true,
    audit_filter = true,
    audit_spaces = true,
    audit_extract_key = true,
    flightrec_enabled = true,
    flightrec_logs_size = true,
    flightrec_logs_max_msg_size = true,
    flightrec_logs_log_level = true,
    flightrec_metrics_interval = true,
    flightrec_metrics_period = true,
    flightrec_requests_size = true,
    flightrec_requests_max_req_size = true,
    flightrec_requests_max_res_size = true,
    auth_delay = true,
    auth_retries = true,
    disable_guest = true,
    password_lifetime_days = true,
    password_min_length = true,
    password_enforce_uppercase = true,
    password_enforce_lowercase = true,
    password_enforce_digits = true,
    password_enforce_specialchars = true,
    password_history_length = true,
    wal_ext = true,
}

function cfg_filter(data)
    if type(data)~='table' then return data end
    local keys = {}
    for k in pairs(data) do
        table.insert(keys, k)
    end
    table.sort(keys)
    local result = {}
    for _,k in pairs(keys) do
        -- Hide Tarantool Enterprise Edition configuration keys if tests
        -- are run by a Tarantool Enterprise binary so that common tests
        -- have the same output between Community and Enterprise editions.
        if tarantool.package ~= 'Tarantool Enterprise' or
           not _enterprise_keys[k] then
            table.insert(result, {k, _hide[k] and '<hidden>' or
                                  cfg_filter(data[k])})
        end
    end
    return result
end

local function compare(a,b)
    return a[1] < b[1]
end

function sorted(data)
    table.sort(data, compare)
    return data
end

function iproto_request(socket, query_header, query_body)
    local header = msgpack.encode(query_header)
    local body = msgpack.encode(query_body)
    local size = msgpack.encode(header:len() + body:len())
    assert(socket:write(size .. header .. body) ~= nil,
           'Failed to send request')
    size = socket:read(5)
    assert(size ~= nil, 'Failed to read response')
    size = msgpack.decode(size)
    local response = socket:read(size)
    local header, header_len = msgpack.decode(response)
    body = msgpack.decode(response:sub(header_len))
    return {
        ['header'] = header,
        ['body'] = body,
    }
end

_G.protected_globals = {'cfg_filter', 'sorted', 'iproto_request'}

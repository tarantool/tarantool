#!/usr/bin/env tarantool
local os = require('os')

local msgpack = require('msgpack')

box.cfg{
    listen              = os.getenv("LISTEN"),
    memtx_memory        = 107374182,
    pid_file            = "tarantool.pid",
}

require('console').listen(os.getenv('ADMIN'))

local _hide = {
    pid_file=1, log=1, listen=1, vinyl_dir=1,
    memtx_dir=1, wal_dir=1,
    memtx_max_tuple_size=1, memtx_min_tuple_size=1,
    replication_sync_timeout=1
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
        table.insert(result, {k, _hide[k] and '<hidden>' or cfg_filter(data[k])})
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

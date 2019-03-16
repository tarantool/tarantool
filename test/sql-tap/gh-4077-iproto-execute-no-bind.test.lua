#!/usr/bin/env tarantool

local tap = require('tap')
local net_box = require('net.box')
local urilib = require('uri')
local msgpack = require('msgpack')

local IPROTO_REQUEST_TYPE       = 0x00
local IPROTO_EXECUTE            = 0x0b
local IPROTO_SYNC               = 0x01
local IPROTO_SQL_TEXT           = 0x40
local IPROTO_SQL_INFO           = 0x42
local IPROTO_SQL_INFO_ROW_COUNT = 0x00
local IPROTO_OK                 = 0x00
local IPROTO_SCHEMA_VERSION     = 0x05
local IPROTO_STATUS_KEY         = 0x00

box.cfg({
    listen = os.getenv('LISTEN') or 'localhost:3301',
})

box.schema.user.grant('guest', 'read,write,execute', 'universe')
box.execute('create table T(ID int primary key)')

local test = tap.test('gh-4077-iproto-execute-no-bind')
test:plan(3)

local uri = urilib.parse(box.cfg.listen)
local sock = net_box.establish_connection(uri.host, uri.service)

-- Send request w/o SQL_BIND field in body.
local next_request_id = 16
local header = msgpack.encode({
    [IPROTO_REQUEST_TYPE] = IPROTO_EXECUTE,
    [IPROTO_SYNC] = next_request_id,
})
local body = msgpack.encode({
    [IPROTO_SQL_TEXT] = 'insert into T values (1)',
})
local size = msgpack.encode(header:len() + body:len())
sock:write(size .. header .. body)

-- Read response.
local size = msgpack.decode(sock:read(5))
local header_body = sock:read(size)
local header, header_len = msgpack.decode(header_body)
local body = msgpack.decode(header_body:sub(header_len))
sock:close()

-- Verify response.
header[IPROTO_SCHEMA_VERSION] = nil -- expect any
local exp_header = {
    [IPROTO_STATUS_KEY] = IPROTO_OK,
    [IPROTO_SYNC] = next_request_id,
}
local exp_body = {
    [IPROTO_SQL_INFO] = {
        [IPROTO_SQL_INFO_ROW_COUNT] = 1,
    }
}
test:is_deeply(exp_header, header, 'verify response header')
test:is_deeply(exp_body, body, 'verify response body')

-- Verify space data.
local exp_res = {{1}}
local res = box.space.T:pairs():map(box.tuple.totable):totable()
test:is_deeply(res, exp_res, 'verify inserted data')

box.execute('drop table T')
box.schema.user.revoke('guest', 'read,write,execute', 'universe')

os.exit(test:check() == true and 0 or 1)

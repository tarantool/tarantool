#!/usr/bin/env tarantool

local session = box.session
local fiber = require('fiber')
local tap = require('tap')
local netbox = require('net.box')
local urilib = require('uri')

box.cfg {
    listen = os.getenv('LISTEN');
    log="tarantool.log";
    memtx_memory=100*1024*1024;
}
local uri = urilib.parse(box.cfg.listen)
local HOST, PORT = uri.host or 'localhost', uri.service

local test = tap.test("auth")
test:plan(27)

local space = box.schema.space.create('tweedledum')
local index = space:create_index('primary', { type = 'hash' })
box.schema.user.create('test', {password='pass'})
box.schema.user.grant('test', 'read,write,execute', 'universe')
box.schema.user.create('test2', {password=''})
box.schema.user.grant('test2', 'read,write,execute', 'universe')
box.schema.user.grant('guest', 'read,write,execute', 'universe')

-- check how authentication trigger work
local msg, counter
function auth_trigger(user_name)
    counter = counter + 1
end
-- get user name as argument
function auth_trigger2(user_name)
    msg = 'user ' .. user_name .. ' is there'
end

-- set trigger
local handle = session.on_auth(auth_trigger)
-- check handle
test:is(type(handle), "function", "handle is a function")
-- check triggers list
test:is(#session.on_auth(), 1, "the number of triggers")
local handle2 = session.on_auth(auth_trigger2)
test:is(type(handle2), "function", "handle is a function")
test:is(#session.on_auth(), 2, "the number of triggers")

-- check connection with authentication(counter must be incremented)
counter = 0
local conn = netbox.connect('test:pass@' .. HOST .. ':' .. PORT)
while counter < 1 do fiber.sleep(0.001) end
test:is(counter, 1, "on_auth has been fired once")
test:is(msg, "user test is there", "on_auth username param")
conn:close()
conn = nil

counter = 0
local conn = netbox.connect('test2:@' .. HOST .. ':' .. PORT)
while counter < 1 do fiber.sleep(0.001) end
test:is(counter, 1, "on_auth has been fired once")
test:is(msg, "user test2 is there", "on_auth username param")
conn:close()
conn = nil

counter = 0
local conn = netbox.connect('test2@' .. HOST .. ':' .. PORT)
while counter < 1 do fiber.sleep(0.001) end
test:is(counter, 1, "on_auth has been fired once")
test:is(msg, "user test2 is there", "on_auth username param")
conn:close()
conn = nil

counter = 0
local conn = netbox.connect(HOST, PORT, {user='test2'})
while counter < 1 do fiber.sleep(0.001) end
test:is(counter, 1, "on_auth has been fired once")
test:is(msg, "user test2 is there", "on_auth username param")
conn:close()
conn = nil

counter = 0
local conn = netbox.connect('guest@' .. HOST .. ':' .. PORT)
while counter < 1 do fiber.sleep(0.001) end
test:is(counter, 1, "on_auth has been fired once")
test:is(msg, "user guest is there", "on_auth username param")
conn:close()
conn = nil

counter = 0
local conn = netbox.connect('guest:@' .. HOST .. ':' .. PORT)
while counter < 1 do fiber.sleep(0.001) end
test:is(counter, 1, "on_auth has been fired once")
test:is(msg, "user guest is there", "on_auth username param")
conn:close()
conn = nil

counter = 0
conn = netbox.connect(HOST, PORT, {user='guest', password=''})
while counter < 1 do fiber.sleep(0.001) end
test:is(counter, 1, "on_auth has been fired once")
test:is(msg, "user guest is there", "on_auth username param")
conn:close()
conn = nil

counter = 0
local conn = netbox.connect(HOST, PORT, {user='guest'})
while counter < 1 do fiber.sleep(0.001) end
test:is(counter, 1, "on_auth has been fired once")
test:is(msg, "user guest is there", "on_auth username param")
conn:close()
conn = nil

-- check guest connection without authentication(no increment)
counter = 0
conn = netbox.connect(HOST, PORT)
conn:ping()
test:is(counter, 0, "on_auth hasn't been fired")
conn:close()
conn = nil

test:isnil(session.on_auth(nil, auth_trigger), "removal returns nil")
test:isnil(session.on_auth(nil, auth_trigger2), "removal returns nil")
test:is(#session.on_auth(), 0, "the number of triggers");
test:is(session.uid(), 1, "box.session.uid()")
test:is(session.user(), "admin", "box.session.user()")
test:is(session.sync(), 0, "box.session.sync()")

-- cleanup
space:drop()
box.schema.user.revoke('guest', 'read,write,execute', 'universe')
box.schema.user.revoke('test', 'read,write,execute', 'universe')
box.schema.user.drop('test', { if_exists = true})
box.schema.user.drop("test2", { if_exists = true})

os.exit(test:check() == true and 0 or -1)

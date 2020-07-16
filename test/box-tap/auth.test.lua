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
test:plan(42)

local space = box.schema.space.create('tweedledum')
space:create_index('primary', { type = 'hash' })
box.schema.user.create('test', {password='pass'})
box.schema.user.create('test2', {password=''})

-- check how authentication trigger work
local msg, counter, succeeded
local function auth_trigger(user_name) -- luacheck: no unused args
    counter = counter + 1
end
-- get user name as argument
local function auth_trigger2(user_name) -- luacheck: no unused args
    msg = 'user ' .. user_name .. ' is there'
end
-- get user name and result of authentication as arguments
local function auth_trigger3(user_name, success) -- luacheck: no unused args
    succeeded = success
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
local handle3 = session.on_auth(auth_trigger3)
test:is(type(handle3), "function", "handle is a function")
test:is(#session.on_auth(), 3, "the number of triggers")

-- check connection with authentication(counter must be incremented)
counter = 0
succeeded = false
local conn = netbox.connect('test:pass@' .. HOST .. ':' .. PORT)
while counter < 1 do fiber.sleep(0.001) end
test:is(counter, 1, "on_auth has been fired once")
test:is(msg, "user test is there", "on_auth username param")
test:ok(succeeded, "on_auth success param")
conn:close()

-- check failing authentication
counter = 0
succeeded = true
local conn = netbox.connect('test:pas@' .. HOST .. ':' .. PORT)
while counter < 1 do fiber.sleep(0.001) end
test:is(counter, 1, "on_auth has been fired once")
test:is(msg, "user test is there", "on_auth username param")
test:ok(not succeeded, "on_auth success param")
conn:close()

counter = 0
succeeded = false
local conn = netbox.connect('test2:@' .. HOST .. ':' .. PORT)
while counter < 1 do fiber.sleep(0.001) end
test:is(counter, 1, "on_auth has been fired once")
test:is(msg, "user test2 is there", "on_auth username param")
test:ok(succeeded, "on_auth success param")
conn:close()

counter = 0
succeeded = false
local conn = netbox.connect('test2@' .. HOST .. ':' .. PORT)
while counter < 1 do fiber.sleep(0.001) end
test:is(counter, 1, "on_auth has been fired once")
test:is(msg, "user test2 is there", "on_auth username param")
test:ok(succeeded, "on_auth success param")
conn:close()

counter = 0
succeeded = false
local conn = netbox.connect(HOST, PORT, {user='test2'})
while counter < 1 do fiber.sleep(0.001) end
test:is(counter, 1, "on_auth has been fired once")
test:is(msg, "user test2 is there", "on_auth username param")
test:ok(succeeded, "on_auth success param")
conn:close()

counter = 0
succeeded = false
local conn = netbox.connect('guest@' .. HOST .. ':' .. PORT)
while counter < 1 do fiber.sleep(0.001) end
test:is(counter, 1, "on_auth has been fired once")
test:is(msg, "user guest is there", "on_auth username param")
test:ok(succeeded, "on_auth success param")
conn:close()

counter = 0
succeeded = false
local conn = netbox.connect('guest:@' .. HOST .. ':' .. PORT)
while counter < 1 do fiber.sleep(0.001) end
test:is(counter, 1, "on_auth has been fired once")
test:is(msg, "user guest is there", "on_auth username param")
test:ok(succeeded, "on_auth success param")
conn:close()

counter = 0
succeeded = false
conn = netbox.connect(HOST, PORT, {user='guest', password=''})
while counter < 1 do fiber.sleep(0.001) end
test:is(counter, 1, "on_auth has been fired once")
test:is(msg, "user guest is there", "on_auth username param")
test:ok(succeeded, "on_auth success param")
conn:close()

counter = 0
succeeded = false
local conn = netbox.connect(HOST, PORT, {user='guest'})
while counter < 1 do fiber.sleep(0.001) end
test:is(counter, 1, "on_auth has been fired once")
test:is(msg, "user guest is there", "on_auth username param")
test:ok(succeeded, "on_auth success param")
conn:close()

-- check guest connection without authentication(no increment)
counter = 0
succeeded = false
conn = netbox.connect(HOST, PORT)
conn:ping()
test:is(counter, 0, "on_auth hasn't been fired")
test:ok(not succeeded, "on_auth not successed param")
conn:close()

test:isnil(session.on_auth(nil, auth_trigger), "removal returns nil")
test:isnil(session.on_auth(nil, auth_trigger2), "removal returns nil")
test:isnil(session.on_auth(nil, auth_trigger3), "removal returns nil")
test:is(#session.on_auth(), 0, "the number of triggers");
test:is(session.uid(), 1, "box.session.uid()")
test:is(session.user(), "admin", "box.session.user()")
test:is(session.sync(), 0, "box.session.sync()")

-- cleanup
space:drop()
box.schema.user.drop('test', { if_exists = true})
box.schema.user.drop("test2", { if_exists = true})

os.exit(test:check() == true and 0 or -1)

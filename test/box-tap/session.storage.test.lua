#!/usr/bin/env tarantool

local tap = require('tap')
local test = tap.test('session')
local net_box = require("net.box")

local test_run = require('test_run')
local inspector = test_run.new()

test:plan(15)

test:is(
    inspector:cmd("create server session_storage with script='box/tiny.lua'\n"),
    true, 'instance created'
)
test:is(
    inspector:cmd('start server session_storage'),
    true, 'instance started'
)

local uri = inspector:eval('session_storage', 'box.cfg.listen')[1]
conn1 = net_box.connect(uri)

conn1:eval("session = box.session")
test:is(conn1:eval("return type(session.id())"), "number", "session.id()")
test:ok(conn1:eval("return session.unknown_field == nil"), "no field")


test:is(conn1:eval("return type(session.storage)"), "table", "storage")
conn1:eval("session.storage.abc = 'cde'")
test:is(conn1:eval("return session.storage.abc"), "cde", "written to storage")

conn1:eval("all = getmetatable(session).aggregate_storage")
test:ok(conn1:eval("return all[session.id()].abc == 'cde'"), "check meta table")


conn2 = net_box.connect(uri)

test:is(conn2:eval("return type(session.storage)"), "table", "storage")
test:isnil(conn2:eval("return type(session.storage.abc)"), "empty storage")
conn2:eval("session.storage.abc = 'def'")
test:ok(conn2:eval("return session.storage.abc == 'def'"), "written to storage")

test:ok(conn1:eval("return session.storage.abc == 'cde'"), "first conn storage")
test:ok(conn1:eval("return all[session.id()].abc == 'cde'"), "check first conn metatable")
test:ok(conn2:eval("return all[session.id()].abc == 'def'"), "check second conn metatable")

tres1 = conn1:eval("t1 = {} for k, v in pairs(all) do table.insert(t1, v.abc) end return t1")

conn1:close()
conn2:close()
conn3 = net_box.connect(uri)
tres2 = conn3:eval("t2 = {} for k, v in pairs(all) do table.insert(t2, v.abc) end return t2")
table.sort(tres1)
table.sort(tres2)
test:is(tres1[1], "cde", "check after closing")
test:is(#tres2, 0, "check after closing")
conn3:close()
inspector:cmd('stop server session_storage with cleanup=1')
os.exit(0)

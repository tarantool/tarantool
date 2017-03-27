print('session.storage')
env = require('test_run')
test_run = env.new('localhost')
session = box.session

type(session.id())
session.unknown_field

type(session.storage)
session.storage.abc = 'cde'
session.storage.abc

all = getmetatable(session).aggregate_storage
all[session.id()].abc

test_run:cmd("create connection second to default")
test_run:cmd("set connection second")

type(session.storage)
type(session.storage.abc)
session.storage.abc = 'def'
session.storage.abc

test_run:cmd("set connection default")

session.storage.abc

test_run:cmd("set connection second")
all[session.id()]

test_run:cmd("set connection default")
all[session.id()]
tres1 = {}
tres2 = {}
for k,v in pairs(all) do table.insert(tres1, v.abc) end

test_run:cmd("drop connection default")
test_run:cmd("drop connection second")
require('fiber').sleep(.01)
for k,v in pairs(all) do table.insert(tres2, v.abc) end

table.sort(tres1)
table.sort(tres2)
tres1
tres2

getmetatable(session).aggregate_storage = {}
session = nil

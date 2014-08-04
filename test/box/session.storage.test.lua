print('session.storage')
session = box.session

dump = function(data) return "'" .. require('json').encode(data) .. "'" end


type(session.id())
session.unknown_field

type(session.storage)
session.storage.abc = 'cde'
session.storage.abc

all = getmetatable(session).aggregate_storage
dump(all)

--# create connection second to default
--# set connection second

type(session.storage)
type(session.storage.abc)
session.storage.abc = 'def'
session.storage.abc

--# set connection default

session.storage.abc

--# set connection second
dump(all[session.id()])

--# set connection default
dump(all[session.id()])
tres1 = {}
tres2 = {}
for k,v in pairs(all) do table.insert(tres1, v.abc) end

--# drop connection second
require('fiber').sleep(.01)
for k,v in pairs(all) do table.insert(tres2, v.abc) end

table.sort(tres1)
table.sort(tres2)
dump(tres1)
dump(tres2)

getmetatable(session).aggregate_storage = {}
session = nil

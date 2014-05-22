print('box.session.storage')
box.session = require('box.session')

dump = function(data) return "'" .. require('box.json').encode(data) .. "'" end


type(box.session.id())
box.session.unknown_field

type(box.session.storage)
box.session.storage.abc = 'cde'
box.session.storage.abc

all = getmetatable(box.session).aggregate_storage
dump(all)

--# create connection second to default
--# set connection second

type(box.session.storage)
type(box.session.storage.abc)
box.session.storage.abc = 'def'
box.session.storage.abc

--# set connection default

box.session.storage.abc

--# set connection second
dump(all[box.session.id()])

--# set connection default
dump(all[box.session.id()])
tres1 = {}
tres2 = {}
for k,v in pairs(all) do table.insert(tres1, v.abc) end

--# drop connection second
require('box.fiber').sleep(.01)
for k,v in pairs(all) do table.insert(tres2, v.abc) end

table.sort(tres1)
table.sort(tres2)
dump(tres1)
dump(tres2)

box.session = nil

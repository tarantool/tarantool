-- clear statistics
env = require('test_run')
test_run = env.new()
test_run:cmd('restart server default with cleanup=1')

box.stat.INSERT.total
box.stat.DELETE.total
box.stat.UPDATE.total
box.stat.REPLACE.total
box.stat.SELECT.total
box.stat.ERROR.total

space = box.schema.space.create('tweedledum')
index = space:create_index('primary', { type = 'hash' })

-- check stat_cleanup
-- add several tuples
for i=1,10 do space:insert{i, 'tuple'..tostring(i)} end
box.stat.INSERT.total
box.stat.DELETE.total
box.stat.UPDATE.total
box.stat.REPLACE.total
box.stat.SELECT.total

-- check exceptions
space:get('Impossible value')
box.stat.ERROR.total

-- select, get, and pairs
space:get(1)
box.stat.SELECT.total
space:get(11)
box.stat.SELECT.total
space:select(5)
box.stat.SELECT.total
space:select(15)
box.stat.SELECT.total
for _ in space:pairs() do end
box.stat.SELECT.total

-- reset
box.stat.reset()
box.stat.INSERT.total
box.stat.DELETE.total
box.stat.UPDATE.total
box.stat.REPLACE.total
box.stat.SELECT.total
box.stat.ERROR.total

test_run:cmd('restart server default')

-- statistics must be zero
box.stat.INSERT.total
box.stat.DELETE.total
box.stat.UPDATE.total
box.stat.REPLACE.total
box.stat.SELECT.total
box.stat.ERROR.total

-- cleanup
box.space.tweedledum:drop()

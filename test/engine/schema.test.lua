env = require('test_run')
test_run = env.new()
engine = test_run:get_cfg('engine')
test_run:cmd("push filter '"..engine.."' to 'engine'")

space = box.schema.space.create('tweedledum', { id = 0, engine = engine })
index = space:create_index('primary')

t = {} for k,v in pairs(box.space.tweedledum) do if type(v) ~= 'table' and type(v) ~= 'function' then table.insert(t, k..': '..tostring(v)) end end
t

space:drop()

id = box.schema.space.create('sp_1266', { engine = engine }).id
s = box.schema.space.create('sp_1266', { engine = engine })
s = box.schema.space.create('sp_1266', { if_not_exists = true, engine = engine })
box.schema.space.drop(id, 'sp_1266')
box.schema.space.drop(id, 'sp_1266', { if_exists = true })
box.schema.space.drop(id, 'sp_1266')

test_run:cmd("clear filter")

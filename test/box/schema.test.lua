space = box.schema.space.create('tweedledum', { id = 0 })
index = space:create_index('primary')

t = {} for k,v in pairs(box.space.tweedledum) do if type(v) ~= 'table' and type(v) ~= 'function' then table.insert(t, k..': '..tostring(v)) end end
t

space:drop()

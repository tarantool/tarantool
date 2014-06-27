space = box.schema.create_space('tweedledum', { id = 0 })
space:create_index('primary', { type = 'hash' })

t = {} for k,v in pairs(box.space[0]) do if type(v) ~= 'table' and type(v) ~= 'function' then table.insert(t, k..': '..tostring(v)) end end
t

space:drop()

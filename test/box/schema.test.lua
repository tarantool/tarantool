space = box.schema.create_space('tweedledum', { id = 0 })
space:create_index('primary', { type = 'hash' })

t = {} for k,v in pairs(box.space[0]) do if type(v) ~= 'table' and type(v) ~= 'function' then table.insert(t, k..': '..tostring(v)) end end
t
box.space[300] = 1
box.index.bind('abc', 'cde')
box.index.bind(1, 2)
box.index.bind(0, 1)
box.index.bind(0, 0)
#box.index.bind(0,0)
#box.space[0].index[0].idx
space:insert{1953719668}
space:insert{1684234849}
#box.index.bind(0,0)
#box.space[0].index[0].idx
space:delete{1953719668}
#box.index.bind(0,0)
space:delete{1684234849}
#box.space[0].index[0].idx
#box.index.bind(0,0)

space:drop()

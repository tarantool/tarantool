--# stop server default
--# start server default
space = box.schema.create_space('tweedledum')
space:create_index('primary', { type = 'hash' })
--# setopt delimiter ';'
i = 1;
while true do
    space:insert{space:len(), string.rep('test', i)}
    i = i + 1
end;
space:len();
i = 1;
while true do
    space:insert{space:len(), string.rep('test', i)}
    i = i + 1
end;
space:len();
i = 1;
while true do
    space:insert{space:len(), string.rep('test', i)}
    i = i + 1
end;
--# setopt delimiter ''
space:len()
space.index['primary']:get{0}
space.index['primary']:get{5}
space.index['primary']:get{9}
space.index['primary']:get{11}
space.index['primary']:get{15}
-- check that iterators work
i = 0
t = {}
--# setopt delimiter ';'
for state, v in space:pairs() do
    table.insert(t, v)
    i = i + 1
    if i == 50 then
        break
    end
end;
--# setopt delimiter ''
t
space:truncate()
space:insert{0, 'test'}
space.index['primary']:get{0}
space:drop()

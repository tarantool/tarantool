box.insert(box.schema.SPACE_ID, 0, 0, 'tweedledum')
box.insert(box.schema.INDEX_ID, 0, 0, 'primary', 'hash', 1, 1, 0, 'num')
space = box.space[0]
--# setopt delimiter ';'
i = 1;
while true do
    space:insert(space:len(), string.rep('test', i))
    i = i + 1
end;
space:len();
i = 1;
while true do
    space:insert(space:len(), string.rep('test', i))
    i = i + 1
end;
space:len();
i = 1;
while true do
    space:insert(space:len(), string.rep('test', i))
    i = i + 1
end;
--# setopt delimiter ''
space:len()
space:select(0, 0)
space:select(0, 5)
space:select(0, 9)
space:select(0, 11)
space:select(0, 15)
-- check that iterators work
i = 0
t = {}
--# setopt delimiter ';'
for k,v in space:pairs() do
    table.insert(t, v)
    i = i + 1
    if i == 50 then
        break
    end
end;
--# setopt delimiter ''
t
space:truncate()
space:insert(0, 'test')
space:select(0, 0)
space:drop()

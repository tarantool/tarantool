--
-- Insert test
--
env = require('test_run')
test_run = env.new()
space = box.schema.space.create('tweedledum')
-- Multipart primary key (sender nickname, receiver nickname, message id)
i1 = space:create_index('primary', { type = 'tree', parts = {1, 'string', 2, 'string', 3, 'unsigned'}, unique = true })

space:insert{'Vincent', 'Jules', 0, 'Do you know what they call a - a - a Quarter Pounder with cheese in Paris?'}
space:insert{'Jules', 'Vincent', 0, 'They don`t call it a Quarter Pounder with cheese?'}
space:insert{'Vincent', 'Jules', 1, 'No man, they got the metric system. They wouldn`t know what the f--k a Quarter Pounder is.'}
space:insert{'Jules', 'Vincent', 1, 'Then what do they call it?'}
space:insert{'Vincent', 'Jules', 2, 'They call it a `Royale` with cheese.'}
space:insert{'Jules', 'Vincent', 2, 'A `Royale` with cheese!'}
space:insert{'Vincent', 'Jules', 3, 'That`s right.'}
space:insert{'Jules', 'Vincent', 3, 'What do they call a Big Mac?'}
space:insert{'Vincent', 'Jules', 4, 'A Big Mac`s a Big Mac, but they call it `Le Big Mac.`'}
space:insert{'Jules', 'Vincent', 4, '`Le Big Mac!`'}
space:insert{'Vincent', 'Jules', 5, 'Ha, ha, ha.'}
space:insert{'Jules', 'Vincent', 5, 'What do they call a `Whopper`?'}
space:insert{'Vincent', 'Jules', 6, 'I dunno, I didn`t go into Burger King.'}

space:insert{'The Wolf!', 'Vincent', 0, 'Jimmie, lead the way. Boys, get to work.'}
space:insert{'Vincent', 'The Wolf!', 0, 'A please would be nice.'}
space:insert{'The Wolf!', 'Vincent', 1, 'Come again?'}
space:insert{'Vincent', 'The Wolf!', 1, 'I said a please would be nice.'}
space:insert{'The Wolf!', 'Vincent', 2, 'Get it straight buster - I`m not here to say please, I`m here to tell you what to do and if self-preservation is an instinct you possess you`d better fucking do it and do it quick. I`m here to help - if my help`s not appreciated then lotsa luck, gentlemen.'}
space:insert{'The Wolf!', 'Vincent', 3, 'I don`t mean any disrespect, I just don`t like people barking orders at me.'}
space:insert{'Vincent', 'The Wolf!', 2, 'If I`m curt with you it`s because time is a factor. I think fast, I talk fast and I need you guys to act fast if you wanna get out of this. So, pretty please... with sugar on top. Clean the fucking car.'}

--
-- Select test
--

-- Select by one entry
space.index['primary']:get{'Vincent', 'Jules', 0}
space.index['primary']:get{'Jules', 'Vincent', 0}
space.index['primary']:get{'Vincent', 'Jules', 1}
space.index['primary']:get{'Jules', 'Vincent', 1}
space.index['primary']:get{'Vincent', 'Jules', 2}
space.index['primary']:get{'Jules', 'Vincent', 2}
space.index['primary']:get{'Vincent', 'Jules', 3}
space.index['primary']:get{'Jules', 'Vincent', 3}
space.index['primary']:get{'Vincent', 'Jules', 4}
space.index['primary']:get{'Jules', 'Vincent', 4}
space.index['primary']:get{'Vincent', 'Jules', 5}
space.index['primary']:get{'Jules', 'Vincent', 5}
space.index['primary']:get{'Vincent', 'Jules', 6}

space.index['primary']:get{'The Wolf!', 'Vincent', 0}
space.index['primary']:get{'Vincent', 'The Wolf!', 0}
space.index['primary']:get{'The Wolf!', 'Vincent', 1}
space.index['primary']:get{'Vincent', 'The Wolf!', 1}
space.index['primary']:get{'The Wolf!', 'Vincent', 2}
space.index['primary']:get{'The Wolf!', 'Vincent', 3}
space.index['primary']:get{'Vincent', 'The Wolf!', 2}

-- Select all messages from Vincent to Jules
space.index['primary']:select({'Vincent', 'Jules'})

-- Select all messages from Jules to Vincent
space.index['primary']:select({'Jules', 'Vincent'})

-- Select all messages from Vincent to The Wolf
space.index['primary']:select({'Vincent', 'The Wolf!'})

-- Select all messages from The Wolf to Vincent
space.index['primary']:select({'The Wolf!', 'Vincent'})

-- Select all Vincent messages
space.index['primary']:select({'Vincent'})

--
-- Delete test
--

-- Delete some messages from the The Wolf and Vincent dialog
space:delete{'The Wolf!', 'Vincent', 0}
space:delete{'The Wolf!', 'Vincent', 3}
space:delete{'Vincent', 'The Wolf!', 0}

space:update({'Vincent', 'The Wolf!', 1}, {{ '=', 1, 'Updated' }, {'=', 5, 'New'}})
space:update({'Updated', 'The Wolf!', 1}, {{ '=', 1, 'Vincent'}, { '#', 5, 1 }})
-- Checking Vincent's last messages
space.index['primary']:select({'Vincent', 'The Wolf!'})
-- Checking The Wolf's last messages
space.index['primary']:select({'The Wolf!', 'Vincent'})

-- try to delete nonexistent message
space:delete{'Vincent', 'The Wolf!', 3}
-- try to delete patrial defined key
space:delete{'Vincent', 'The Wolf!'}
-- try to delete by invalid key
space:delete{'The Wolf!', 'Vincent', 1, 'Come again?'}

--
-- Update test
--
space:update({'The Wolf!', 'Vincent', 1}, {{'=', 4, '<ooops>'}})
space:update({'Vincent', 'The Wolf!', 1}, {{'=', 4, '<ooops>'}})

-- Checking Vincent's last messages
space.index['primary']:select({'Vincent', 'The Wolf!'})
-- Checking The Wolf's last messages
space.index['primary']:select({'The Wolf!', 'Vincent'})

-- try to update a nonexistent message
space:update({'Vincent', 'The Wolf!', 4}, {{'=', 4, '<ooops>'}})
-- try to update patrial defined key
space:update({'Vincent', 'The Wolf!'}, {{'=', 4, '<ooops>'}})
-- try to update by invalid key
space:update({'The Wolf!', 'Vincent', 1, 'Come again?'}, {{'=', 4, '<ooops>'}})
space:len()
space:truncate()
space:len()

-- A test case for Bug#1051006 Tree iterators return garbage
--if an index is modified between calls
--
space.index['primary']:drop()
i1 = space:create_index('primary', { type = 'tree', parts = {1, 'string'}, unique = true })
i2 = space:create_index('second', { type  = 'tree', parts = {2, 'string', 3, 'string'}, unique = true })

space:insert{'a', 'a', 'a'}
space:insert{'d', 'd', 'd'}
space:insert{'e', 'e', 'e'}
space:insert{'b', 'b', 'b'}
space:insert{'c', 'c', 'c'}

t = {}
gen, param, state = space.index['second']:pairs(nil, { iterator = box.index.GE })
test_run:cmd("setopt delimiter ';'")
for i = 1, 2 do
    state, v = gen(param, state)
    table.insert(t, v)
end;
test_run:cmd("setopt delimiter ''");

t
space:truncate()
v
collectgarbage('collect')
v

param, v = gen(param, state)
v
collectgarbage('collect')
v

t = {}
test_run:cmd("setopt delimiter ';'")
for i = 1, 3 do
    param, v = gen(param, state)
    table.insert(t, v)
end;
test_run:cmd("setopt delimiter ''");
t
space:drop()
space = nil
-- Bug #1082356
-- Space #19, https://bugs.launchpad.net/tarantool/+bug/1082356

space = box.schema.space.create('tweedledum')
-- Multipart primary key (sender nickname, receiver nickname, message id)
i1 = space:create_index('primary', { type = 'tree', parts = {1, 'unsigned', 3, 'unsigned'}, unique = true })

space:insert{1, 1}
space:replace{1, 1}

space:drop()

-- test deletion of data one by one
space = box.schema.space.create('test')
i1 = space:create_index('primary', { type = 'tree', parts = {1, 'string'}, unique = true })
i2 = space:create_index('second', { type  = 'tree', parts = {2, 'string', 3, 'string'}, unique = true })
test_run:cmd("setopt delimiter ';'")
for i = 1, 100 do
    v = tostring(i)
    space:insert{v, string.rep(v, 2) , string.rep(v, 3)}
end;
local pk = space.index[0]
while pk:len() > 0 do
    local state, t
    for state, t in pk:pairs() do
        local key = {}
        for _k2, parts in ipairs(pk.parts) do
            table.insert(key, t[parts.fieldno])
        end
        space:delete(key)
    end
end;
test_run:cmd("setopt delimiter ''");
space:drop()

space = nil


-- hints
test_run:cmd("setopt delimiter ';'")
function equal(res1, res2)
    if #res1 ~= #res2 then
        return false
    end
    for k,v in pairs(res1) do
        if res2[k][1] ~= v[1] or res2[k][2] ~= v[2] then
            return false
        end
    end
    return true
end
test_run:cmd("setopt delimiter ''");

-- num num
N1 = 100
t1 = {}
for i = 1,N1*2 do t1[i] = math.random(10000) * 10000 + i end
N2 = 5
t2 = {}
for i = 1,N2*2 do t2[i] = math.random(1000000) end

s1 = box.schema.space.create('test1')
s1:create_index('test', {type = 'tree', parts = {{1, 'num'}, {2, 'num'}}, hint = false}).hint
s2 = box.schema.space.create('test2')
s2:create_index('test', {type = 'tree', parts = {{1, 'num'}, {2, 'num'}}, hint = true}).hint
for j = 1,N2 do for i = 1,N1 do s1:replace{t1[i], t2[j]} s2:replace{t1[i], t2[j]} end end
s1:count() == s2:count()
equal(s1:select{}, s2:select{})
good = true
for i = 1,N1*2 do good = good and equal(s1:select{t1[i]}, s2:select{t1[i]}) end
good
for i = 1,N1*2 do for j=1,N2*2 do good = good and equal(s1:select{t1[i], t2[j]}, s2:select{t1[i], t2[j]}) end end
good

s1:drop()
s2:drop()

-- str num
N1 = 100
t1 = {}
for i = 1,N1*2 do t1[i] = ''..(math.random(10000) * 10000 + i) end
N2 = 5
t2 = {}
for i = 1,N2*2 do t2[i] = math.random(1000000) end

s1 = box.schema.space.create('test1')
s1:create_index('test', {type = 'tree', parts = {{1, 'str'}, {2, 'num'}}, hint = false}).hint
s2 = box.schema.space.create('test2')
s2:create_index('test', {type = 'tree', parts = {{1, 'str'}, {2, 'num'}}, hint = true}).hint
for j = 1,N2 do for i = 1,N1 do s1:replace{t1[i], t2[j]} s2:replace{t1[i], t2[j]} end end
s1:count() == s2:count()
equal(s1:select{}, s2:select{})
good = true
for i = 1,N1*2 do good = good and equal(s1:select{t1[i]}, s2:select{t1[i]}) end
good
for i = 1,N1*2 do for j=1,N2*2 do good = good and equal(s1:select{t1[i], t2[j]}, s2:select{t1[i], t2[j]}) end end
good

s1:drop()
s2:drop()

test_run = require('test_run').new()

-- test c and lua triggers: must return only lua triggers
#box.space._space:on_replace()
function f() print('test') end
type(box.space._space:on_replace(f))
#box.space._space:on_replace()

ts = box.schema.space.create('test_space')
ti = ts:create_index('primary', { type = 'hash' })
type(ts.on_replace)
ts.on_replace()
ts:on_replace()
ts:on_replace(123)

function fail(old_tuple, new_tuple) error('test') end
type(ts:on_replace(fail))

ts:insert{1, 'b', 'c'}
ts:get{1}

ts:on_replace(nil, fail)

ts:insert{1, 'b', 'c'}
ts:get{1}

function fail(old_tuple, new_tuple) error('abc') end
type(ts:on_replace(fail))

ts:insert{2, 'b', 'c'}
ts:get{2}

o = nil
n = nil
function save_out(told, tnew) o = told n = tnew end
type(ts:on_replace(save_out, fail))
ts:insert{2, 'a', 'b', 'c'}
o
n

ts:replace{2, 'd', 'e', 'f'}
o
n

type(ts:on_replace(function() test = 1 end))
#ts:on_replace()
ts:drop()

-- test garbage in lua stack
#box.space._space:on_replace()
function f2() print('test2') end
type(box.space._space:on_replace(f2))
#box.space._space:on_replace()

box.space._space:on_replace(nil, f)
box.space._space:on_replace(nil, f2)

--
-- gh-587: crash on attempt to modify space from triggers
--

first = box.schema.space.create('first')
_= first:create_index('primary')
second = box.schema.space.create('second')
_ = second:create_index('primary')

-- one statement
test_run:cmd("setopt delimiter ';'");
trigger_id = first:on_replace(function()
    second:replace({2, first:get(1)[2] .. " from on_replace trigger"})
end);
test_run:cmd("setopt delimiter ''");
first:replace({1, "hello"})
first:select()
second:select()
first:on_replace(nil, trigger_id)
first:delete(1)
second:delete(1)

-- multiple statements
test_run:cmd("setopt delimiter ';'");
trigger_id = first:on_replace(function()
    second:replace({1})
    second:replace({2, first:get(1)[2] .. " in on_replace trigger"})
    second:replace({3})
end);
test_run:cmd("setopt delimiter ''");
first:replace({1, "multistatement tx"})
first:select()
second:select()
first:on_replace(nil, trigger_id)
first:delete(1)
second:delete(1)
second:delete(2)
second:delete(3)

-- rollback on error
test_run:cmd("setopt delimiter ';'");
trigger_id = first:on_replace(function()
    second:replace({1, "discarded"})
    second:insert({1})
end);
test_run:cmd("setopt delimiter ''");
first:replace({1, "rollback"})
first:select()
second:select()
first:on_replace(nil, trigger_id)

-- max recursion depth
RECURSION_LIMIT = 0
test_run:cmd("setopt delimiter ';'");
trigger_id = first:on_replace(function()
    RECURSION_LIMIT = RECURSION_LIMIT + 1
    first:auto_increment({"recursive"})
end);
test_run:cmd("setopt delimiter ''");
first:replace({1, "recursive"})
first:select()
second:select()
RECURSION_LIMIT
first:on_replace(nil, trigger_id)

-- recursion
level = 0
test_run:cmd("setopt delimiter ';'");
trigger_id = first:on_replace(function()
    level = level + 1
    if level >= RECURSION_LIMIT then
        return
    end
    first:auto_increment({"recursive", level})
end);
test_run:cmd("setopt delimiter ''");
first:replace({0, "initial"})
first:select()
second:select()
RECURSION_LIMIT
first:on_replace(nil, trigger_id)
first:truncate()
second:truncate()


-- transaction control
test_run:cmd("setopt delimiter ';'");
trigger_id = first:on_replace(function()
    box.commit()
    first:auto_increment({"recursive", level})
end);
test_run:cmd("setopt delimiter ''");
first:replace({0, "initial"})
first:select()
second:select()
first:on_replace(nil, trigger_id)

test_run:cmd("setopt delimiter ';'");
trigger_id = first:on_replace(function()
    box.rollback()
    first:auto_increment({"recursive", level})
end);
test_run:cmd("setopt delimiter ''");
first:replace({0, "initial"})
first:select()
second:select()
first:on_replace(nil, trigger_id)

test_run:cmd("setopt delimiter ';'");
trigger_id = first:on_replace(function()
    box.begin()
    first:auto_increment({"recursive", level})
    box.commit()
end);

test_run:cmd("setopt delimiter ''");
first:replace({0, "initial"})
first:select()
second:select()
first:on_replace(nil, trigger_id)

first:drop()
second:drop()

s = box.schema.space.create('test_on_repl_ddl')
_ = s:create_index('pk')
_ = s:create_index('sk', {parts = {2, 'unsigned'}})
t = s:on_replace(function () box.schema.space.create('some_space') end)
s:replace({1, 2})
t = s:on_replace(function () s:format({{'a', 'unsigned'}, {'b', 'unsigned'}}) end, t)
s:replace({2, 3})
t = s:on_replace(function () box.schema.user.create('newu') box.schema.role.create('newr') end, t)
s:replace({3, 4})
t = s:on_replace(function () box.schema.user.drop('newu') box.schema.role.drop('newr') end, t)
s:replace({4, 5})
t = s:on_replace(function () s.index.sk:drop() end, t)
s:replace({5, 6})
t = s:on_replace(function () box.schema.func.create('newf') end, t)
s:replace({6, 7})
t = s:on_replace(function () box.schema.user.grant('guest', 'read,write', 'space', 'test_on_repl_ddl') end, t)
s:replace({7, 8})
t = s:on_replace(function () s:rename('newname') end, t)
s:replace({8, 9})
t = s:on_replace(function () s.index.pk:rename('newname') end, t)
s:replace({9, 10})
s:select()
s:drop() -- test_on_repl_ddl

--
-- gh-3020: sub-statement rollback
--
s1 = box.schema.space.create('test1')
_ = s1:create_index('pk')
s2 = box.schema.space.create('test2')
_ = s2:create_index('pk')
s3 = box.schema.space.create('test3')
_ = s3:create_index('pk')

test_run:cmd("setopt delimiter ';'");

x1 = 1;
x2 = 1;

_ = s1:on_replace(function(old, new)
    for i = 1, 3 do
        s2:insert{x1}
        x1 = x1 + 1
    end
    if new[2] == 'fail' then
        error('fail')
    end
    pcall(s2.insert, s2, {123, 'fail'})
end);

_ = s2:on_replace(function(old, new)
    for i = 1, 3 do
        s3:insert{x2}
        x2 = x2 + 1
    end
    if new[2] == 'fail' then
        error('fail')
    end
end);

box.begin()
s1:insert{1}
pcall(s1.insert, s1, {123, 'fail'})
box.commit();

test_run:cmd("setopt delimiter ''");

s1:select()
s2:select()
s3:select()

s1:drop()
s2:drop()
s3:drop()

--
-- gh-3020: trigger chaining
--
s1 = box.schema.space.create('test1')
_ = s1:create_index('pk')
s2 = box.schema.space.create('test2')
_ = s2:create_index('pk')
s3 = box.schema.space.create('test3')
_ = s3:create_index('pk')

x = 1

_ = s1:on_replace(function(old, new) s2:insert(new:update{{'!', 2, x}}) x = x + 1 end)
_ = s1:on_replace(function(old, new) s3:insert(new:update{{'!', 2, x}}) x = x + 1 end)

box.begin() s1:insert{1} s1:insert{2} s1:insert{3} box.commit()

s1:select()
s2:select()
s3:select()

s1:drop()
s2:drop()
s3:drop()

s = box.schema.space.create('test')
_ = s:create_index('pk')
save_type = ''
function find_type(old, new, name, type) save_type = type return new end
s:on_replace(find_type) == find_type

_ = s:insert{1, 2}
save_type
_ = s:update({1}, {{'+', 2, 1}})
save_type
_ = s:delete{1}
save_type
_ = s:replace{2, 3}
save_type
_ = s:upsert({3,4,5}, {{'+', 2, 1}})
save_type

s:drop()

--
-- gh-4266 triggers on temporary space fail
--
s = box.schema.space.create('test', {temporary = true})

_ = s:create_index('pk')

save_type = ''

_ = s:on_replace(function(old,new, name, type) save_type = type end)
_ = s:insert{1}
save_type

s:drop()

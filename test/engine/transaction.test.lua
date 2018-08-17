test_run = require('test_run')
inspector = test_run.new()
engine = inspector:get_cfg('engine')

-- basic transaction tests
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary', { type = 'tree', parts = {1, 'unsigned'} })

-- begin/rollback
inspector:cmd("setopt delimiter ';'")
box.begin()
for key = 1, 10 do space:insert({key}) end
box.rollback();
inspector:cmd("setopt delimiter ''");

t = {}
for key = 1, 10 do assert(#space:select({key}) == 0) end
t

-- begin/commit insert
inspector:cmd("setopt delimiter ';'")
box.begin()
for key = 1, 10 do space:insert({key}) end
box.commit();
inspector:cmd("setopt delimiter ''");

t = {}
for key = 1, 10 do table.insert(t, space:select({key})[1]) end
t

-- begin/commit delete
inspector:cmd("setopt delimiter ';'")
box.begin()
for key = 1, 10 do space:delete({key}) end
box.commit();
inspector:cmd("setopt delimiter ''");

t = {}
for key = 1, 10 do assert(#space:select({key}) == 0) end
t
space:drop()


-- multi-space transactions
a = box.schema.space.create('test', { engine = engine })
index = a:create_index('primary', { type = 'tree', parts = {1, 'unsigned'} })
b = box.schema.space.create('test_tmp', { engine = engine })
index = b:create_index('primary', { type = 'tree', parts = {1, 'unsigned'} })

-- begin/rollback
inspector:cmd("setopt delimiter ';'")
box.begin()
for key = 1, 10 do a:insert({key}) end
for key = 1, 10 do b:insert({key}) end
box.rollback();
inspector:cmd("setopt delimiter ''");

t = {}
for key = 1, 10 do assert(#a:select({key}) == 0) end
t
for key = 1, 10 do assert(#b:select({key}) == 0) end
t

-- begin/commit insert
inspector:cmd("setopt delimiter ';'")
box.begin()
for key = 1, 10 do a:insert({key}) end
for key = 1, 10 do b:insert({key}) end
box.commit();
inspector:cmd("setopt delimiter ''");

t = {}
for key = 1, 10 do table.insert(t, a:select({key})[1]) end
t
t = {}
for key = 1, 10 do table.insert(t, b:select({key})[1]) end
t

-- begin/commit delete
inspector:cmd("setopt delimiter ';'")
box.begin()
for key = 1, 10 do a:delete({key}) end
for key = 1, 10 do b:delete({key}) end
box.commit();
inspector:cmd("setopt delimiter ''");

t = {}
for key = 1, 10 do assert(#a:select({key}) == 0) end
t
for key = 1, 10 do assert(#b:select({key}) == 0) end
t
a:drop()
b:drop()

-- ensure findByKey works in empty transaction context
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary', { type = 'tree', parts = {1, 'unsigned'} })

inspector:cmd("setopt delimiter ';'")
box.begin()
space:get({0})
box.rollback();
inspector:cmd("setopt delimiter ''");
space:drop()

--
-- gh-857: on_commit/rollback triggers in Lua.
--
s = box.schema.create_space('test')
_ = s:create_index('pk')
call_list = {}
on_commit = {}
inspector:cmd("setopt delimiter ';'")
for i = 1, 3 do
    table.insert(on_commit, function()
        table.insert(call_list, 'on_commit_'..tostring(i))
    end)
end;
function on_rollback()
    table.insert(call_list, 'on_rollback')
end;
inspector:cmd("setopt delimiter ''");

-- A basic test: when a transaction is committed, the appropriate
-- trigger is called, in opposite to its on_rollback vis-a-vis.
box.begin() s:replace{1} box.on_commit(on_commit[1]) box.on_rollback(on_rollback) box.commit()
call_list
call_list = {}

-- The same vice versa.
box.begin() s:replace{2} box.on_commit(on_commit[1]) box.on_rollback(on_rollback) box.rollback()
call_list
call_list = {}

-- Incorrect usage outside of a transaction.
box.on_commit()

-- Incorrect usage inside a transaction.
box.begin() s:replace{3} box.on_rollback(on_rollback) box.on_commit(100)
box.rollback()
call_list
call_list = {}

-- Multiple triggers.
funcs = {}
inspector:cmd("setopt delimiter ';'")
box.begin()
table.insert(funcs, box.on_commit())
box.on_commit(on_commit[1])
table.insert(funcs, box.on_commit())
box.on_commit(on_commit[2])
table.insert(funcs, box.on_commit())
box.on_commit(on_commit[3])
box.on_commit(on_commit[3])
table.insert(funcs, box.on_commit())
box.on_commit(on_commit[3], on_commit[2])
table.insert(funcs, box.on_commit())
box.commit();

for _, list in pairs(funcs) do
    for i, f1 in pairs(list) do
        for j, f2 in pairs(on_commit) do
            if f1 == f2 then
                list[i] = 'on_commit_'..tostring(j)
                break
            end
        end
    end
end;
inspector:cmd("setopt delimiter ''");
-- Yes, the order is reversed. Like all other Lua triggers.
call_list
funcs
--
-- Test on_commit/rollback txn iterators.
--
iter = nil
function steal_iterator(iter_) iter = iter_ end
box.begin() s:replace{1, 1} box.on_commit(steal_iterator) box.commit()
-- Fail. No transaction.
iter()
-- Fail. Not the same transaction.
box.begin() iter()
box.rollback()
-- Test the same for the iterator generator.
a = nil
b = nil
c = nil
function steal_iterator(iter) a, b, c = iter() end
box.begin() s:replace{1, 1} box.on_commit(steal_iterator) box.commit()
a(b, c)
box.begin() a(b, c)
box.rollback()
-- Test appropriate usage.
for i = 1, 5 do s:replace{i} end
stmts = nil
inspector:cmd("setopt delimiter ';'")
function on_commit(iter)
    stmts = {}
    for i, old, new, space in iter() do
        stmts[i] = {old, new, space == s.id or space}
    end
end;

box.begin()
s:replace{1, 1}
s:replace{2, 2}
s:delete{3}
s:replace{6, 6}
s:replace{0, 0}
-- Test read and rolled back statements.
s:get{3}
s:get{1}
pcall(s.insert, s, {1})
box.on_commit(on_commit)
s:replace{7, 7}
-- Test save point rollbacks.
sv = box.savepoint()
s:replace{8, 8}
box.rollback_to_savepoint(sv)
s:replace{9, 9}
box.commit();
inspector:cmd("setopt delimiter ''");
stmts

-- Check empty transaction iteration.
box.begin() box.on_commit(on_commit) box.commit()
stmts

s:drop()

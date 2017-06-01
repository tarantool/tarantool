test_run = require('test_run')
inspector = test_run.new()
engine = inspector:get_cfg('engine')

--
-- Lua select_reverse_range
--
-- lua select_reverse_range() testing
-- https://blueprints.launchpad.net/tarantool/+spec/backward-tree-index-iterator
space = box.schema.space.create('tweedledum', { engine = engine })
tmp = space:create_index('primary', { type = 'tree', parts = {1, 'unsigned'}, unique = true })
tmp = space:create_index('range', { type = 'tree', parts = {2, 'unsigned', 1, 'unsigned'}, unique = true })

space:insert{0, 0}
space:insert{1, 0}
space:insert{2, 0}
space:insert{3, 0}
space:insert{4, 0}
space:insert{5, 0}
space:insert{6, 0}
space:insert{7, 0}
space:insert{8, 0}
space:insert{9, 0}
space.index['range']:select({}, { limit = 10, iterator = 'GE' })
space.index['range']:select({}, { limit = 10, iterator = 'LE' })
space.index['range']:select({}, { limit = 4, iterator = 'LE' })
space:drop()

--
-- Tests for box.index iterators
--
space = box.schema.space.create('tweedledum', { engine = engine })
tmp = space:create_index('primary', { type = 'tree', parts = {1, 'string'}, unique = true })
tmp = space:create_index('i1', { type = 'tree', parts = {2, 'string', 3, 'string'}, unique = true })

pid = 1
tid = 999
inspector:cmd("setopt delimiter ';'")
for sid = 1, 2 do
    for i = 1, 3 do
        space:insert{'pid_'..pid, 'sid_'..sid, 'tid_'..tid}
        pid = pid + 1
        tid = tid - 1
    end
end;
inspector:cmd("setopt delimiter ''");

index = space.index['i1']

t = {}
for state, v in index:pairs('sid_1', { iterator = 'GE' }) do table.insert(t, v) end
t
t = {}
for state, v in index:pairs('sid_2', { iterator = 'LE' }) do table.insert(t, v) end
t
t = {}
for state, v in index:pairs('sid_1', { iterator = 'EQ' }) do table.insert(t, v) end
t
t = {}
for state, v in index:pairs('sid_1', { iterator = 'REQ' }) do table.insert(t, v) end
t
t = {}
for state, v in index:pairs('sid_2', { iterator = 'EQ' }) do table.insert(t, v) end
t
t = {}
for state, v in index:pairs('sid_2', { iterator = 'REQ' }) do table.insert(t, v) end
t
t = {}


index:pairs('sid_t', { iterator = 'wrong_iterator_type' })

index = nil
space:drop()

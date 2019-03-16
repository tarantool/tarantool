env = require('test_run')
test_run = env.new()
test_run:cmd("push filter ".."'\\.lua.*:[0-9]+: ' to '.lua...\"]:<line>: '")
engine = test_run:get_cfg('engine')
box.execute('pragma sql_default_engine=\''..engine..'\'')

--
-- gh-3272: Move SQL CHECK into server
--

-- invalid expression
opts = {checks = {{expr = 'X><5'}}}
format = {{name = 'X', type = 'unsigned'}}
t = {513, 1, 'test', 'memtx', 0, opts, format}
s = box.space._space:insert(t)

opts = {checks = {{expr = 'X>5'}}}
format = {{name = 'X', type = 'unsigned'}}
t = {513, 1, 'test', 'memtx', 0, opts, format}
s = box.space._space:insert(t)
box.space._space:delete(513)

opts = {checks = {{expr = 'X>5', name = 'ONE'}}}
format = {{name = 'X', type = 'unsigned'}}
t = {513, 1, 'test', 'memtx', 0, opts, format}
s = box.space._space:insert(t)
box.space._space:delete(513)

-- extra invlalid field name
opts = {checks = {{expr = 'X>5', name = 'ONE', extra = 'TWO'}}}
format = {{name = 'X', type = 'unsigned'}}
t = {513, 1, 'test', 'memtx', 0, opts, format}
s = box.space._space:insert(t)

opts = {checks = {{expr_invalid_label = 'X>5'}}}
format = {{name = 'X', type = 'unsigned'}}
t = {513, 1, 'test', 'memtx', 0, opts, format}
s = box.space._space:insert(t)

-- invalid field type
opts = {checks = {{name = 123}}}
format = {{name = 'X', type = 'unsigned'}}
t = {513, 1, 'test', 'memtx', 0, opts, format}
s = box.space._space:insert(t)


--
-- gh-3611: Segfault on table creation with check referencing this table
--
box.execute("CREATE TABLE w2 (s1 INT PRIMARY KEY, CHECK ((SELECT COUNT(*) FROM w2) = 0));")
box.execute("DROP TABLE w2;")

--
-- gh-3653: Dissallow bindings for DDL
--
box.execute("CREATE TABLE t5(x INT PRIMARY KEY, y INT, CHECK( x*y < ? ));")

opts = {checks = {{expr = '?>5', name = 'ONE'}}}
format = {{name = 'X', type = 'unsigned'}}
t = {513, 1, 'test', 'memtx', 0, opts, format}
s = box.space._space:insert(t)


test_run:cmd("clear filter")

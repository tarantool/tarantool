test_run = require('test_run').new()

-- Options check on create.
box.schema.sequence.create('test', {abc = 'abc'})
box.schema.sequence.create('test', {step = 'a'})
box.schema.sequence.create('test', {min = 'b'})
box.schema.sequence.create('test', {max = 'c'})
box.schema.sequence.create('test', {start = true})
box.schema.sequence.create('test', {cycle = 123})
box.schema.sequence.create('test', {name = 'test'})
box.schema.sequence.create('test', {step = 0})
box.schema.sequence.create('test', {min = 10, max = 1})
box.schema.sequence.create('test', {min = 10, max = 20, start = 1})

-- Options check on alter.
_ = box.schema.sequence.create('test')
box.schema.sequence.alter('test', {abc = 'abc'})
box.schema.sequence.alter('test', {step = 'a'})
box.schema.sequence.alter('test', {min = 'b'})
box.schema.sequence.alter('test', {max = 'c'})
box.schema.sequence.alter('test', {start = true})
box.schema.sequence.alter('test', {cycle = 123})
box.schema.sequence.alter('test', {name = 'test'})
box.schema.sequence.alter('test', {if_not_exists = false})
box.schema.sequence.alter('test', {step = 0})
box.schema.sequence.alter('test', {min = 10, max = 1})
box.schema.sequence.alter('test', {min = 10, max = 20, start = 1})
box.schema.sequence.drop('test')

-- Duplicate name.
sq1 = box.schema.sequence.create('test')
box.schema.sequence.create('test')
sq2, msg = box.schema.sequence.create('test', {if_not_exists = true})
sq1 == sq2, msg
_ = box.schema.sequence.create('test2')
box.schema.sequence.alter('test2', {name = 'test'})
box.schema.sequence.drop('test2')
box.schema.sequence.drop('test')

-- Check that box.sequence gets updated.
sq = box.schema.sequence.create('test')
box.sequence.test == sq
sq.step
sq:alter{step = 2}
box.sequence.test == sq
sq.step
sq:drop()
box.sequence.test == nil

-- Attempt to delete a sequence that has a record in _sequence_data.
sq = box.schema.sequence.create('test')
sq:next()
box.space._sequence:delete(sq.id)
box.space._sequence_data:delete(sq.id)
box.space._sequence:delete(sq.id)
box.sequence.test == nil

-- Default ascending sequence.
sq = box.schema.sequence.create('test')
sq.step, sq.min, sq.max, sq.start, sq.cycle
sq:get()  -- error
sq:next() -- 1
sq:get()  -- 1
sq:next() -- 2
sq:set(100)
sq:get()  -- 100
sq:next() -- 101
sq:next() -- 102
sq:reset()
sq:get()  -- error
sq:next() -- 1
sq:next() -- 2
sq:drop()

-- Default descending sequence.
sq = box.schema.sequence.create('test', {step = -1})
sq.step, sq.min, sq.max, sq.start, sq.cycle
sq:get()  -- error
sq:next() -- -1
sq:get()  -- -1
sq:next() -- -2
sq:set(-100)
sq:get()  -- -100
sq:next() -- -101
sq:next() -- -102
sq:reset()
sq:get()  -- error
sq:next() -- -1
sq:next() -- -2
sq:drop()

-- Custom min/max.
sq = box.schema.sequence.create('test', {min = 10})
sq.step, sq.min, sq.max, sq.start, sq.cycle
sq:next() -- 10
sq:next() -- 11
sq:drop()
sq = box.schema.sequence.create('test', {step = -1, max = 20})
sq.step, sq.min, sq.max, sq.start, sq.cycle
sq:next() -- 20
sq:next() -- 19
sq:drop()

-- Custom start value.
sq = box.schema.sequence.create('test', {start = 1000})
sq.step, sq.min, sq.max, sq.start, sq.cycle
sq:next() -- 1000
sq:next() -- 1001
sq:reset()
sq:next() -- 1000
sq:next() -- 1001
sq:drop()

-- Overflow and cycle.
sq = box.schema.sequence.create('test', {max = 2})
sq:next() -- 1
sq:next() -- 2
sq:next() -- error
sq:alter{cycle = true}
sq:next() -- 1
sq:next() -- 2
sq:next() -- 1
sq:alter{step = 2}
sq:next() -- 1
sq:alter{cycle = false}
sq:next() -- error
sq:drop()

-- Setting sequence value outside boundaries.
sq = box.schema.sequence.create('test')

sq:alter{step = 1, min = 1, max = 10}
sq:set(-100)
sq:next() -- 1
sq:set(100)
sq:next() -- error
sq:reset()
sq:next() -- 1
sq:alter{min = 5, start = 5}
sq:next() -- 5
sq:reset()

sq:alter{step = -1, min = 1, max = 10, start = 10}
sq:set(100)
sq:next() -- 10
sq:set(-100)
sq:next() -- error
sq:reset()
sq:next() -- 10
sq:alter{max = 5, start = 5}
sq:next() -- 5
sq:drop()

-- number64 arguments.
INT64_MIN = tonumber64('-9223372036854775808')
INT64_MAX = tonumber64('9223372036854775807')
sq = box.schema.sequence.create('test', {step = INT64_MAX, min = INT64_MIN, max = INT64_MAX, start = INT64_MIN})
sq:next() -- -9223372036854775808
sq:next() -- -1
sq:next() -- 9223372036854775806
sq:next() -- error
sq:alter{step = INT64_MIN, start = INT64_MAX}
sq:reset()
sq:next() -- 9223372036854775807
sq:next() -- -1
sq:next() -- error
sq:drop()

-- Using in a transaction.
s = box.schema.space.create('test')
_ = s:create_index('pk')
sq1 = box.schema.sequence.create('sq1', {step = 1})
sq2 = box.schema.sequence.create('sq2', {step = -1})

test_run:cmd("setopt delimiter ';'")
box.begin()
s:insert{sq1:next(), sq2:next()}
s:insert{sq1:next(), sq2:next()}
s:insert{sq1:next(), sq2:next()}
box.rollback();
box.begin()
s:insert{sq1:next(), sq2:next()}
s:insert{sq1:next(), sq2:next()}
s:insert{sq1:next(), sq2:next()}
box.commit();
test_run:cmd("setopt delimiter ''");

s:select() -- [4, -4], [5, -5], [6, -6]

sq1:drop()
sq2:drop()
s:drop()

--
-- Check that sequences are persistent.
--

sq = box.schema.sequence.create('test', {step = 2, min = 10, max = 20, start = 15, cycle = true})
sq:next()

test_run:cmd('restart server default')

sq = box.sequence.test
sq.step, sq.min, sq.max, sq.start, sq.cycle

sq:next()
sq:drop()

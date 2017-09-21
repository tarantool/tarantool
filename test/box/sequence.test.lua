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
sq:next() -- 1
sq:next() -- 2
sq:set(100)
sq:next() -- 101
sq:next() -- 102
sq:reset()
sq:next() -- 1
sq:next() -- 2
sq:drop()

-- Default descending sequence.
sq = box.schema.sequence.create('test', {step = -1})
sq.step, sq.min, sq.max, sq.start, sq.cycle
sq:next() -- -1
sq:next() -- -2
sq:set(-100)
sq:next() -- -101
sq:next() -- -102
sq:reset()
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
-- Attaching a sequence to a space.
--

-- Index create/modify checks.
s = box.schema.space.create('test')
sq = box.schema.sequence.create('test')
sq:set(123)

s:create_index('pk', {parts = {1, 'string'}, sequence = 'test'}) -- error
s:create_index('pk', {parts = {1, 'scalar'}, sequence = 'test'}) -- error
s:create_index('pk', {parts = {1, 'number'}, sequence = 'test'}) -- error

pk = s:create_index('pk', {parts = {1, 'integer'}, sequence = 'test'}) -- ok
pk:drop()
pk = s:create_index('pk', {parts = {1, 'unsigned'}, sequence = 'test'}) -- ok
pk:drop()

pk = s:create_index('pk') -- ok
s:create_index('secondary', {parts = {2, 'unsigned'}, sequence = 'test'}) -- error
pk:drop()

pk = s:create_index('pk', {parts = {1, 'unsigned'}, sequence = 'test'}) -- ok
pk:alter{parts = {1, 'string'}} -- error
box.space._index:update({s.id, pk.id}, {{'=', 6, {{0, 'string'}}}}) -- error
box.space._index:delete{s.id, pk.id} -- error
pk:alter{parts = {1, 'string'}, sequence = false} -- ok
sk = s:create_index('sk', {parts = {2, 'unsigned'}})
sk:alter{sequence = 'test'} -- error
box.space._space_sequence:insert{s.id, sq.id, false} -- error
sk:drop()
pk:drop()

s:create_index('pk', {sequence = {}}) -- error
s:create_index('pk', {sequence = 'abc'}) -- error
s:create_index('pk', {sequence = 12345}) -- error
pk = s:create_index('pk', {sequence = 'test'}) -- ok
s.index.pk.sequence_id == sq.id
pk:drop()
pk = s:create_index('pk', {sequence = sq.id}) -- ok
s.index.pk.sequence_id == sq.id
pk:drop()
pk = s:create_index('pk', {sequence = false}) -- ok
s.index.pk.sequence_id == nil
pk:alter{sequence = {}} -- error
pk:alter{sequence = 'abc'} -- error
pk:alter{sequence = 12345} -- error
pk:alter{sequence = 'test'} -- ok
s.index.pk.sequence_id == sq.id
pk:alter{sequence = sq.id} -- ok
s.index.pk.sequence_id == sq.id
pk:alter{sequence = false} -- ok
s.index.pk.sequence_id == nil
pk:drop()

sq:next() -- 124
sq:drop()
s:drop()

-- Using a sequence for auto increment.
sq = box.schema.sequence.create('test')
s1 = box.schema.space.create('test1')
_ = s1:create_index('pk', {parts = {1, 'unsigned'}, sequence = 'test'})
s2 = box.schema.space.create('test2')
_ = s2:create_index('pk', {parts = {2, 'integer'}, sequence = 'test'})
s3 = box.schema.space.create('test3')
_ = s3:create_index('pk', {parts = {2, 'unsigned', 1, 'string'}, sequence = 'test'})

s1:insert(box.tuple.new(nil)) -- 1
s2:insert(box.tuple.new('a', nil)) -- 2
s3:insert(box.tuple.new('b', nil)) -- 3
s1:truncate()
s2:truncate()
s3:truncate()
s1:insert{nil, 123, 456} -- 4
s2:insert{'c', nil, 123} -- 5
s3:insert{'d', nil, 456} -- 6
sq:next() -- 7
sq:reset()
s1:insert{nil, nil, 'aa'} -- 1
s2:insert{'bb', nil, nil, 'cc'} -- 2
s3:insert{'dd', nil, nil, 'ee'} -- 3
sq:next() -- 4
sq:set(100)
s1:insert{nil, 'aaa', 1} -- 101
s2:insert{'bbb', nil, 2} -- 102
s3:insert{'ccc', nil, 3} -- 103
sq:next() -- 104
s1:insert{1000, 'xxx'}
sq:next() -- 1001
s2:insert{'yyy', 2000}
sq:next() -- 2001
s3:insert{'zzz', 3000}
sq:next() -- 3001
s1:insert{500, 'xxx'}
s3:insert{'zzz', 2500}
s2:insert{'yyy', 1500}
sq:next() -- 3002

sq:drop() -- error
s1:drop()
sq:drop() -- error
s2:drop()
sq:drop() -- error
s3:drop()
sq:drop() -- ok

-- Automatically generated sequences.
s = box.schema.space.create('test')
sq = box.schema.sequence.create('test')
sq:set(123)

pk = s:create_index('pk', {sequence = true})
sq = box.sequence.test_seq
sq.step, sq.min, sq.max, sq.start, sq.cycle
s.index.pk.sequence_id == sq.id
s:insert{nil, 'a'} -- 1
s:insert{nil, 'b'} -- 2
s:insert{nil, 'c'} -- 3
sq:next() -- 4

pk:alter{sequence = false}
s.index.pk.sequence_id == nil
s:insert{nil, 'x'} -- error
box.sequence.test_seq == nil

pk:alter{sequence = true}
sq.step, sq.min, sq.max, sq.start, sq.cycle
sq = box.sequence.test_seq
s.index.pk.sequence_id == sq.id
s:insert{100, 'abc'}
s:insert{nil, 'cda'} -- 101
sq:next() -- 102

pk:alter{sequence = 'test'}
s.index.pk.sequence_id == box.sequence.test.id
box.sequence.test_seq == nil

pk:alter{sequence = true}
s.index.pk.sequence_id == box.sequence.test_seq.id
pk:drop()
box.sequence.test_seq == nil

pk = s:create_index('pk', {sequence = true})
s.index.pk.sequence_id == box.sequence.test_seq.id
s:drop()
box.sequence.test_seq == nil

sq = box.sequence.test
sq:next() -- 124
sq:drop()

-- Sequences are compatible with Vinyl spaces.
s = box.schema.space.create('test', {engine = 'vinyl'})
_ = s:create_index('pk', {sequence = true})

s:insert{nil, 'a'} -- 1
s:insert{100, 'b'} -- 100

box.begin()
s:insert{nil, 'c'} -- 101
s:insert{nil, 'd'} -- 102
box.rollback()

box.begin()
s:insert{nil, 'e'} -- 103
s:insert{nil, 'f'} -- 104
box.commit()

s:select() -- {1, 'a'}, {100, 'b'}, {103, 'e'}, {104, 'f'}
s:drop()

--
-- Check that sequences are persistent.
--

s1 = box.schema.space.create('test1')
_ = s1:create_index('pk', {sequence = true})
s1:insert{nil, 'a'} -- 1

box.snapshot()

s2 = box.schema.space.create('test2')
_ = s2:create_index('pk', {sequence = true})
s2:insert{101, 'aaa'}

sq = box.schema.sequence.create('test', {step = 2, min = 10, max = 20, start = 15, cycle = true})
sq:next()

test_run:cmd('restart server default')

sq = box.sequence.test
sq.step, sq.min, sq.max, sq.start, sq.cycle

sq:next()
sq:drop()

s1 = box.space.test1
s1.index.pk.sequence_id == box.sequence.test1_seq.id
s1:insert{nil, 'b'} -- 2
s1:drop()

s2 = box.space.test2
s2.index.pk.sequence_id == box.sequence.test2_seq.id
s2:insert{nil, 'bbb'} -- 102
s2:drop()

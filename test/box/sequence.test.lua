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
err, res = pcall(function() return box.schema.sequence.alter('test2', {name = 'test'}) end)
assert(res.code == box.error.TUPLE_FOUND)
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
sq:current()  -- error
sq:next() -- 1
sq:current()  -- 1
sq:next() -- 2
sq:set(100)
sq:current()  -- 100
sq:next() -- 101
sq:next() -- 102
sq:reset()
sq:current()  -- error
sq:next() -- 1
sq:next() -- 2
sq:drop()

-- Default descending sequence.
sq = box.schema.sequence.create('test', {step = -1})
sq.step, sq.min, sq.max, sq.start, sq.cycle
sq:current()  -- error
sq:next() -- -1
sq:current()  -- -1
sq:next() -- -2
sq:set(-100)
sq:current()  -- -100
sq:next() -- -101
sq:next() -- -102
sq:reset()
sq:current()  -- error
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

s:create_index('pk', {sequence = {id = 'no_such_sequence'}}) -- error
s:create_index('pk', {sequence = {field = 2}}) -- error
s:create_index('pk', {sequence = {field = 'no.such.field'}}) -- error
s:create_index('pk', {parts = {1, 'unsigned', 2, 'string'}, sequence = {field = 2}}) -- error

pk = s:create_index('pk', {parts = {1, 'string', 2, 'unsigned'}}) -- ok
pk:alter{sequence = {id = 'no_such_sequence', field = 2}} -- error
pk:alter{sequence = {id = 'test', field = 2}} -- ok
pk:alter{sequence = {id = 'test', field = 1}} -- error
pk:alter{sequence = false} -- ok
pk:alter{sequence = {field = 1}} -- error
pk:alter{sequence = {field = 2}} -- ok
pk:alter{sequence = {field = 1}} -- error
pk:alter{sequence = false} -- ok
pk:drop()

pk = s:create_index('pk', {parts = {1, 'integer'}, sequence = 'test'}) -- ok
pk:drop()
pk = s:create_index('pk', {parts = {1, 'unsigned'}, sequence = 'test'}) -- ok
pk:drop()

pk = s:create_index('pk') -- ok
s:create_index('secondary', {parts = {2, 'unsigned'}, sequence = 'test'}) -- error
s:create_index('secondary', {parts = {2, 'unsigned'}, sequence = true}) -- error
s:create_index('secondary', {parts = {2, 'unsigned'}, sequence = {field = 2}}) -- error
sk = s:create_index('secondary', {parts = {2, 'unsigned'}}) -- ok
sk:alter{sequence = 'test'} -- error
sk:alter{sequence = true} -- error
sk:alter{parts = {2, 'string'}} -- ok
sk:alter{sequence = false} -- ok (ignored)
pk:alter{sequence = 'test'} -- ok
s.index.pk.sequence_id == sq.id
sk:alter{sequence = 'test'} -- error
sk:alter{sequence = true} -- error
sk:alter{parts = {2, 'unsigned'}} -- ok
sk:alter{sequence = false} -- ok (ignored)
s.index.pk.sequence_id == sq.id
sk:drop()
s.index.pk.sequence_id == sq.id
pk:drop()

pk = s:create_index('pk', {parts = {1, 'unsigned'}, sequence = 'test'}) -- ok
pk:alter{parts = {1, 'string'}} -- error
box.space._index:delete{s.id, pk.id} -- error
pk:alter{parts = {1, 'string'}, sequence = false} -- ok
sk = s:create_index('sk', {parts = {2, 'unsigned'}})
sk:alter{sequence = 'test'} -- error
box.space._space_sequence:insert{s.id, sq.id, false, 0, ''} -- error
box.space._space_sequence:insert{s.id, sq.id, false, 2, ''} -- error
sk:drop()
pk:drop()
box.space._space_sequence:insert{s.id, sq.id, false, 0, ''} -- error

pk = s:create_index('pk', {sequence = {}}) -- ok
pk:drop()
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
pk:alter{sequence = {}} -- ok
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

s1:insert(box.tuple.new(box.NULL)) -- 1
s2:insert(box.tuple.new{'a', box.NULL}) -- 2
s3:insert(box.tuple.new{'b', box.NULL}) -- 3
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
sk = s:create_index('sk', {parts = {2, 'string'}})
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
sk:drop()
pk:drop()
box.sequence.test_seq == nil

pk = s:create_index('pk', {sequence = true})
s.index.pk.sequence_id == box.sequence.test_seq.id
s:drop()
box.sequence.test_seq == nil

sq = box.sequence.test
sq:next() -- 124
sq:drop()

-- Check that generated sequence cannot be attached to another space.
s1 = box.schema.space.create('test1')
_ = s1:create_index('pk', {sequence = true})
s2 = box.schema.space.create('test2')
s2:create_index('pk', {sequence = 'test1_seq'}) -- error
_ = s2:create_index('pk')
box.space._space_sequence:insert{s2.id, box.sequence.test1_seq.id, false, 0, ''} -- error

s1:drop()
s2:drop()

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

--
-- Test permission checks.
--

-- Sanity checks.
box.schema.user.create('user')
-- Setup read permissions for box.schema.user.info() to work.
box.schema.user.grant('user', 'read', 'space', '_priv')
box.schema.user.grant('user', 'read', 'space', '_user')
box.schema.user.grant('user', 'read', 'space', '_space')
box.schema.user.grant('user', 'read', 'space', '_sequence')
sq = box.schema.sequence.create('seq')
box.schema.user.grant('user', 'write', 'sequence', 'test') -- error: no such sequence
box.schema.user.grant('user', 'write', 'sequence', 'seq') -- ok
box.space._priv.index.object:select{'sequence'}
box.space._sequence:delete(sq.id) -- error: sequence has grants
sq:drop() -- ok
box.space._priv.index.object:select{'sequence'}

-- Access to a standalone sequence is denied unless
-- the user has the corresponding privileges.
sq = box.schema.sequence.create('seq')
box.session.su('user')
sq:set(100) -- error
sq:next() -- error
sq:reset() -- error
box.session.su('admin')
box.schema.user.grant('user', 'write', 'sequence', 'seq')
box.session.su('user')
box.schema.user.info()
sq:set(100) -- ok
sq:next() -- ok
sq:reset() -- ok
box.session.su('admin')
box.schema.user.revoke('user', 'write', 'sequence', 'seq')

-- Check that access via role works.
box.schema.role.create('seq_role')
box.schema.role.grant('seq_role', 'write', 'sequence', 'seq')
box.schema.user.grant('user', 'execute', 'role', 'seq_role')
box.session.su('user')
sq:set(100) -- ok
sq:next() -- ok
sq:reset() -- ok
box.session.su('admin')
box.schema.role.drop('seq_role')

-- Universe access grants access to any sequence.
box.schema.user.grant('user', 'write', 'universe')
box.session.su('user')
sq:set(100) -- ok
sq:next() -- ok
sq:reset() -- ok
box.session.su('admin')

-- A sequence is inaccessible after privileges have been revoked.
box.schema.user.revoke('user', 'write', 'universe')
box.session.su('user')
sq:set(100) -- error
sq:next() -- error
sq:reset() -- error
box.session.su('admin')

-- A user cannot alter sequences created by other users.
box.schema.user.grant('user', 'read,write', 'universe')
box.session.su('user')
sq:alter{step = 2} -- error
sq:drop() -- error
box.session.su('admin')
sq:drop()
box.schema.user.revoke('user', 'read,write', 'universe')

-- A user can alter/use sequences that he owns.
box.schema.user.grant('user', 'create', 'sequence')
box.schema.user.grant('user', 'write', 'space', '_sequence')
box.schema.user.grant('user', 'write', 'space', '_sequence_data')
box.session.su('user')
sq = box.schema.sequence.create('seq')
sq:alter{step = 2} -- ok
sq:drop() -- ok
sq = box.schema.sequence.create('seq')
box.session.su('admin')
box.schema.user.revoke('user', 'create', 'sequence')
box.schema.user.revoke('user', 'write', 'space', '_sequence')
box.schema.user.revoke('user', 'write', 'space', '_sequence_data')
box.session.su('user')
sq:set(100) -- ok - user owns the sequence
sq:next() -- ok
sq:reset() -- ok
box.session.su('admin')
sq:drop()

-- A sequence can be attached to a space only if the user has
-- create privilege on space and read/write on sequence.
sq1 = box.schema.sequence.create('seq1')
s1 = box.schema.space.create('space1')
_ = s1:create_index('pk')
box.schema.user.grant('user', 'write', 'space', '_sequence')
box.schema.user.grant('user', 'write', 'space', '_sequence_data')
box.schema.user.grant('user', 'write', 'space', '_schema')
box.schema.user.grant('user', 'write', 'space', '_space')
box.schema.user.grant('user', 'create', 'space')
box.schema.user.grant('user', 'create', 'sequence')
box.session.su('user')
sq2 = box.schema.sequence.create('seq2')
s2 = box.schema.space.create('space2')

box.session.su('admin')
box.schema.user.revoke('user', 'create', 'sequence')
box.schema.user.revoke('user', 'write', 'space', '_sequence')
box.schema.user.revoke('user', 'write', 'space', '_sequence_data')
box.schema.user.revoke('user', 'write', 'space', '_schema')
box.schema.user.revoke('user', 'write', 'space', '_space')
box.schema.user.grant('user', 'write', 'space', '_index')
box.schema.user.grant('user', 'write', 'space', '_space_sequence')
box.schema.user.grant('user', 'read', 'space', '_index')
box.schema.user.grant('user', 'read', 'space', '_space_sequence')
box.session.su('user')
_ = s2:create_index('pk', {sequence = 'seq1'}) -- error
s1.index.pk:alter({sequence = 'seq1'}) -- error
box.space._space_sequence:replace{s1.id, sq1.id, false, 0, ''} -- error
box.space._space_sequence:replace{s1.id, sq2.id, false, 0, ''} -- error
box.space._space_sequence:replace{s2.id, sq1.id, false, 0, ''} -- error
_ = s2:create_index('pk', {sequence = 'seq2'}) -- ok
box.session.su('admin')

-- If the user owns a sequence attached to a space,
-- it can use it for auto increment, otherwise it
-- needs privileges.
box.schema.user.revoke('user', 'write', 'space', '_index')
box.schema.user.revoke('user', 'write', 'space', '_space_sequence')
box.schema.user.revoke('user', 'read', 'space', '_space')
box.schema.user.revoke('user', 'read', 'space', '_sequence')
box.schema.user.revoke('user', 'read', 'space', '_index')
box.schema.user.revoke('user', 'read', 'space', '_space_sequence')
box.session.su('user')
s2:insert{nil, 1} -- ok: {1, 1}
box.session.su('admin')
s2.index.pk:alter{sequence = 'seq1'}
box.session.su('user')
s2:insert{2, 2} -- error
s2:insert{nil, 2} -- error
s2:update(1, {{'+', 2, 1}}) -- ok
s2:delete(1) -- ok
box.session.su('admin')
box.schema.user.grant('user', 'write', 'sequence', 'seq1')
box.session.su('user')
s2:insert{2, 2} -- ok
s2:insert{nil, 3} -- ok: {3, 3}
box.session.su('admin')
s1:drop()
s2:drop()
sq1:drop()
sq2:drop()

-- If the user has access to a space, it also has access to
-- an automatically generated sequence attached to it.
s = box.schema.space.create('test')
_ = s:create_index('pk', {sequence = true})
box.schema.user.grant('user', 'read,write', 'space', 'test')
box.session.su('user')
s:insert{10, 10} -- ok
s:insert{nil, 11} -- ok: {11, 11}
box.sequence.test_seq:set(100) -- error
box.sequence.test_seq:next() -- error
box.sequence.test_seq:reset() -- error
box.session.su('admin')
s:drop()

-- When a user is dropped, all his sequences are dropped as well.
box.schema.user.grant('user', 'write', 'space', '_sequence')
box.schema.user.grant('user', 'read', 'space', '_sequence')
box.schema.user.grant('user', 'write', 'space', '_space_sequence')
box.schema.user.grant('user', 'create', 'sequence')
box.session.su('user')
_ = box.schema.sequence.create('test1')
_ = box.schema.sequence.create('test2')
box.session.su('admin')
box.schema.user.drop('user')
box.sequence

-- Apart from the admin, only the owner can grant permissions
-- to a sequence.
box.schema.user.create('user1')
box.schema.user.create('user2')
box.schema.user.grant('user1', 'create', 'sequence')
box.schema.user.grant('user1', 'write', 'space', '_sequence')
box.schema.user.grant('user1', 'read', 'space', '_sequence')
box.schema.user.grant('user1', 'read', 'space', '_user')
box.schema.user.grant('user1', 'write', 'space', '_sequence_data')
box.schema.user.grant('user1', 'write', 'space', '_priv')
box.schema.user.grant('user2', 'read,write', 'universe')
box.session.su('user1')
sq = box.schema.sequence.create('test')
box.session.su('user2')
box.schema.user.grant('user2', 'write', 'sequence', 'test') -- error
box.session.su('user1')
box.schema.user.grant('user2', 'write', 'sequence', 'test') -- ok
box.session.su('admin')
box.schema.user.drop('user1')
box.schema.user.drop('user2')

-- gh-2914: check identifier constraints.
test_run = require('test_run').new()
identifier = require("identifier")
test_run:cmd("setopt delimiter ';'")
identifier.run_test(
	function (identifier)
		box.schema.sequence.create(identifier)
		if box.sequence[identifier]:next() ~= 1 then
			error("Cannot access sequence by identifier")
		end
	end,
	function (identifier) box.schema.sequence.drop(identifier) end
);
test_run:cmd("setopt delimiter ''");

--
-- gh-4214: error while altering an index with attached sequence.
--
s = box.schema.space.create('test')
_ = s:create_index('pk', {sequence = true})
sequence_id = s.index.pk.sequence_id
sequence_id ~= nil
s.index.pk:alter{parts = {1, 'integer'}}
s.index.pk.parts[1].type
s.index.pk:alter{sequence = true}
sequence_id == s.index.pk.sequence_id
s:drop()

--
-- gh-4009: setting sequence for an index part other than the first.
--
s = box.schema.space.create('test')
_ = s:create_index('pk', {parts = {1, 'string', 2, 'unsigned', 3, 'unsigned'}, sequence = {field = 2}})
sequence_id = s.index.pk.sequence_id
sequence_id ~= nil
s.index.pk.sequence_fieldno -- 2
s:insert{'a', box.NULL, 1}
s:insert{'a', box.NULL, 2}
s:insert{'b', 10, 10}
s:insert{'b', box.NULL, 11}
s.index.pk:alter{sequence = {field = 3}}
s.index.pk.sequence_fieldno -- 3
s.index.pk.sequence_id == sequence_id
s:insert{'c', 100, 100}
s:insert{'c', 101, box.NULL}
s.index.pk:alter{sequence = {field = 2}}
s.index.pk.sequence_fieldno -- 2
s.index.pk.sequence_id == sequence_id
s:insert{'d', 1000, 1000}
s:insert{'d', box.NULL, 1001}
s:drop()

--
-- gh-4210: using sequence with a json path key part.
--
s = box.schema.space.create('test')
s:format{{'x', 'map'}}
_ = s:create_index('pk', {parts = {{'x.a.b[1]', 'unsigned'}}, sequence = {field = 'x.a.b[1]'}})
s.index.pk.sequence_fieldno -- 1
s.index.pk.sequence_path -- .a.b[1]
s:replace{} -- error
s:replace{{c = {}}} -- error
s:replace{{a = {c = {}}}} -- error
s:replace{{a = {b = {}}}} -- error
s:replace{{a = {b = {box.NULL}}}} -- ok
s.index.pk:alter{sequence = false}
s.index.pk:alter{sequence = {field = 'x.a.b[1]'}}
s:replace{{a = {b = {box.NULL}}}} -- ok
s:drop()

--
-- gh-4753: accessing dropped sequence should yield correct error
--
s = box.schema.sequence.create('s')
s:drop()
s:next()
s:reset()

--
-- Check that altering parts of a primary index with a sequence
-- attached requires sequence update. Renaming fields does not.
--
s = box.schema.space.create('test')
s:format({{'x', 'map'}})
pk = s:create_index('pk', {parts = {{'x.a', 'unsigned'}}})
pk:alter{sequence = true} -- ok
s:insert{{a = box.NULL, b = 1}}
s:format{{'y', 'map'}} -- ok
s:insert{{a = box.NULL, b = 2}}
pk:alter{sequence = {field = 'y.b'}} -- error
pk:alter{parts = {{'y.b', 'unsigned'}}} -- error
pk:alter{parts = {{'y.b', 'unsigned'}}, sequence = {field = 'y.a'}} -- error
pk:alter{parts = {{'y.b', 'unsigned'}}, sequence = {field = 'y.b'}} -- ok
s:insert{{a = 3, b = box.NULL}}
s:drop()

--
-- Check that sequence cache is updated synchronously with _sequence changes.
--
box.begin() box.schema.sequence.create('test') sq = box.sequence.test box.rollback()
sq ~= nil
box.sequence.test == nil
sq = box.schema.sequence.create('test')
box.begin() sq:alter{step = 10} step = sq.step box.rollback()
step -- 10
sq.step -- 1
box.begin() box.space._sequence:delete{sq.id} sq = box.sequence.test box.rollback()
sq == nil
box.sequence.test ~= nil
box.sequence.test:drop()

--
-- Check that changes to _space_sequence are rolled back properly.
--
s = box.schema.space.create('test')
_ = s:create_index('pk')
sq = box.schema.sequence.create('test')
box.begin() box.space._space_sequence:insert{s.id, sq.id, false, 0, ''} id = s.index.pk.sequence_id box.rollback()
id == sq.id
s.index.pk.sequence_id == nil
s:insert{box.NULL} -- error
s.index.pk:alter{sequence = sq}
box.begin() box.space._space_sequence:delete{s.id} id = s.index.pk.sequence_id box.rollback()
id == nil
s.index.pk.sequence_id == sq.id
s:insert{box.NULL} -- ok
s.index.pk:alter{sequence = false}
sq:drop()
s:drop()

--
-- Check that if a deletion from _sequence_data is rolled back,
-- the sequence state is restored.
--
sq = box.schema.sequence.create('test')
sq:next() -- 1
box.begin() box.space._sequence_data:delete{sq.id} box.rollback()
sq:next() -- 2
sq:drop()

--
-- Update on _space_sequence is forbidden.
--
s = box.schema.create_space('test')
pk = s:create_index('pk', {sequence = true})
t = box.space._space_sequence:get({s.id})
box.space._space_sequence:update({s.id}, {{'=', 2, t[2]}})
s:drop()

--
-- gh-4752: introduce sequence:current() method which
-- fetches current sequence value but doesn't modify
-- sequence itself.
--
sq = box.schema.sequence.create('test')
sq:current()
sq:next()
sq:current()
sq:set(42)
sq:current()
sq:current()
sq:reset()
sq:current()
sq:drop()

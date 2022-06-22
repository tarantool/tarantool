env = require('test_run')
test_run = env.new()
engine = test_run:get_cfg('engine')
msgpack = require('msgpack')

--
-- gh-1557: box.NULL in indexes.
--


box.NULL == msgpack.NULL
box.NULL == nil

msgpack.decode(msgpack.encode({box.NULL}))

format = {}
format[1] = { name = 'field1', type = 'unsigned' }
format[2] = { name = 'field2', type = 'unsigned', is_nullable = true }
s = box.schema.space.create('test', { engine = engine, format = format })

-- Bad nullable value.
format[2].is_nullable = 100
s:format(format) -- Fail.

-- Primary can not be nullable.
parts = {}
parts[1] = {field = 2, type = 'unsigned', is_nullable = true}
pk = s:create_index('pk', { parts = parts }) -- Fail.

pk = s:create_index('pk')

-- Not TREE nullable.
-- Do not print errmsg, because Vinyl's one is different - it does
-- not support HASH.
ok = pcall(s.create_index, s, 'sk', { parts = parts, type = 'hash' }) -- Fail.
ok

sk = s:create_index('sk', { parts = parts })

-- Test insert.

s:insert{1, 1}
s:insert{2, box.NULL}
s:insert{3, box.NULL}
s:insert{4, 1} -- Fail.
s:insert{4, 4}
s:insert{5, box.NULL}

pk:select{}
sk:select{}

-- Test exact match.

sk:get({1})
sk:get({box.NULL}) -- Fail.

sk:update({1}, {})
sk:update({box.NULL}, {}) -- Fail.

_ = sk:delete({1})
sk:delete({box.NULL}) -- Fail.
s:insert({1, 1})

-- Test iterators.

sk:select{box.NULL}
sk:select({box.NULL}, {iterator = 'LE'})
sk:select({box.NULL}, {iterator = 'LT'})
sk:select({box.NULL}, {iterator = 'GE'})
sk:select({box.NULL}, {iterator = 'GT'})

_ = sk:delete{box.NULL}
sk:select{}
pk:select{}

-- Test snapshot during iterator (Vinyl restore).

create_iterator = require('utils').create_iterator

iter = create_iterator(sk, {box.NULL})
iter.next()

box.snapshot()
iter.iterate_over()

sk:select{}
pk:select{}

-- Test replace.

s:replace{2, 2}
s:replace{3, box.NULL} -- no changes.
s:replace{6, box.NULL}

pk:select{}
sk:select{}

-- Test not unique indexes.

s:truncate()
sk:drop()
sk = s:create_index('sk', { parts = parts, unique = false })

s:insert{1, 1}
s:insert{2, box.NULL}
s:insert{3, box.NULL}
s:insert{4, 1}
s:insert{5, box.NULL}

pk:select{}
sk:select{}

-- Test several secondary indexes.

s:truncate()
format[2].is_nullable = true
format[3] = { name = 'field3', type = 'unsigned', is_nullable = true }
s:format(format)
parts[1].field = 3
sk2 = s:create_index('sk2', { parts = parts })

s:replace{4, 3, 4}
s:replace{3, 3, 3}
s:replace{2, box.NULL, box.NULL}
s:replace{1, box.NULL, 1}
s:replace{0, 0, box.NULL}

pk:select{}
sk:select{}
sk2:select{}

-- Check duplicate conflict on replace.

s:replace{4, 4, 3} -- fail
s:replace{4, 4, box.NULL} -- ok

pk:select{}
sk:select{}
sk2:select{}

_ = pk:delete{2}
pk:select{}
sk:select{}
sk2:select{}

s:drop()

--
-- gh-2880: allow to store less field count than specified in a
-- format.
--
format = {}
format[1] = {name = 'field1', type = 'unsigned'}
format[2] = {name = 'field2', type = 'unsigned'}
format[3] = {name = 'field3'}
format[4] = {name = 'field4', is_nullable = true}
s = box.schema.create_space('test', {engine = engine, format = format})
pk = s:create_index('pk')
sk = s:create_index('sk', {parts = {2, 'unsigned'}})

s:replace{1, 2} -- error
t1 = s:replace{2, 3, 4}
t2 = s:replace{3, 4, 5, 6}
t1.field1, t1.field2, t1.field3, t1.field4
t2.field1, t2.field2, t2.field3, t2.field4
 -- Ensure the tuple is read ok from disk in a case of vinyl.
if engine == 'vinyl' then box.snapshot() end
s:select{2}

s:drop()

-- Check the case when not contiguous format tail is nullable.
format = {}
format[1] = {name = 'field1', type = 'unsigned'}
format[2] = {name = 'field2', type = 'unsigned'}
format[3] = {name = 'field3'}
format[4] = {name = 'field4', is_nullable = true}
format[5] = {name = 'field5'}
format[6] = {name = 'field6', is_nullable = true}
format[7] = {name = 'field7', is_nullable = true}
s = box.schema.create_space('test', {engine = engine, format = format})
pk = s:create_index('pk')
sk = s:create_index('sk', {parts = {2, 'unsigned'}})

s:replace{1, 2} -- error
s:replace{2, 3, 4} -- error
s:replace{3, 4, 5, 6} -- error
t1 = s:replace{4, 5, 6, 7, 8}
t2 = s:replace{5, 6, 7, 8, 9, 10}
t3 = s:replace{6, 7, 8, 9, 10, 11, 12}
t1.field1, t1.field2, t1.field3, t1.field4, t1.field5, t1.field6, t1.field7
t2.field1, t2.field2, t2.field3, t2.field4, t2.field5, t2.field6, t2.field7
t3.field1, t3.field2, t3.field3, t3.field4, t3.field5, t3.field6, t3.field7
s:select{}

s:drop()

-- Check nullable indexes with other types
s = box.schema.space.create('test', {engine = engine})
_ = s:create_index('pk')
_ = s:create_index('i1', {parts = {{2, 'string', is_nullable = true}}})
_ = s:create_index('i2', {parts = {{3, 'number', is_nullable = true}}})
_ = s:create_index('i3', {parts = {{4, 'integer', is_nullable = true}}})
_ = s:create_index('i4', {parts = {{5, 'boolean', is_nullable = true}}, unique = false})
_ = s:create_index('i5', {parts = {{6, 'scalar', is_nullable = true}}})

_ = s:auto_increment{box.NULL, 1.11, -111, false, '111'}
_ = s:auto_increment{'222', box.NULL, -222, true, 222}
_ = s:auto_increment{'333', 3.33, box.NULL, false, 3.33}
_ = s:auto_increment{'444', 4.44, -444, box.NULL, true}
_ = s:auto_increment{'555', 5.55, -555, false, box.NULL}

box.snapshot()

_ = s:auto_increment{box.NULL, 6.66, -666, true, '666'}
_ = s:auto_increment{'777', box.NULL, -777, false, 777}
_ = s:auto_increment{'888', 8.88, box.NULL, true, 8.88}
_ = s:auto_increment{'999', 9.99, -999, box.NULL, false}
_ = s:auto_increment{'000', 0.00, -000, true, box.NULL}

s.index.i1:select()
s.index.i2:select()
s.index.i3:select()
s.index.i4:select()
s.index.i5:select()

s:drop()

-- Check that action property is consistent if nullable
format = {}
format[1] = { name = 'field1', type = 'unsigned' }
format[2] = { name = 'field2', type = 'unsigned', is_nullable = false}
s = box.schema.space.create('test', { engine = engine, format = format })
pk = s:create_index('pk')

format[2].nullable_action = 'abort'
s:format(format) -- Ok.

format[2].nullable_action = 'rollback'
s:format(format) -- Ok.

format[2].nullable_action = 'fail'
s:format(format) -- Ok.

format[2].nullable_action = 'ignore'
s:format(format) -- Ok.

format[2].nullable_action = 'replace'
s:format(format) -- Ok.

format[2].nullable_action = 'default'
s:format(format) -- Ok.

format[2].nullable_action = 'none'
s:format(format) -- Fail.

format[2].is_nullable = false
format[2].nullable_action = 'abort'
s:format(format) -- Ok.

format[2].nullable_action = 'rollback'
s:format(format) -- Ok.

format[2].nullable_action = 'fail'
s:format(format) -- Ok.

format[2].nullable_action = 'ignore'
s:format(format) -- Ok.

format[2].nullable_action = 'replace'
s:format(format) -- Ok.

format[2].nullable_action = 'default'
s:format(format) -- Ok.

format[2].nullable_action = 'none'
s:format(format) -- Fail.

parts = {}
parts[1] = {field = 2, type = 'unsigned', is_nullable = true, nullable_action = 'abort'}
sk = s:create_index('sk', { parts = parts }) -- Fail.

parts[1].nullable_action = 'rollback'
sk = s:create_index('sk', { parts = parts }) -- Fail.

parts[1].nullable_action = 'fail'
sk = s:create_index('sk', { parts = parts }) -- Fail.

parts[1].nullable_action = 'ignore'
sk = s:create_index('sk', { parts = parts }) -- Fail.

parts[1].nullable_action = 'replace'
sk = s:create_index('sk', { parts = parts }) -- Fail.

parts[1].nullable_action = 'default'
sk = s:create_index('sk', { parts = parts }) -- Fail.

parts[1].nullable_action = 'none'
sk = s:create_index('sk', { parts = parts }) -- Ok, assume is_nullable = false.
sk:drop()

parts[1].is_nullable = false
parts[1].nullable_action = 'none'
sk = s:create_index('sk', { parts = parts }) -- Fail.

parts[1].nullable_action = 'abort'
sk = s:create_index('sk', { parts = parts }) -- Ok.

sk:drop()
parts[1].nullable_action = 'rollback'
sk = s:create_index('sk', { parts = parts }) -- Ok.

sk:drop()
parts[1].nullable_action = 'replace'
sk = s:create_index('sk', { parts = parts }) -- Ok.

sk:drop()
parts[1].nullable_action = 'fail'
sk = s:create_index('sk', { parts = parts }) -- Ok.

sk:drop()
parts[1].nullable_action = 'ignore'
sk = s:create_index('sk', { parts = parts }) -- Ok.

s:drop()

--
-- gh-2973: allow to enable nullable on a non-empty space.
--
format = {}
format[1] = {name = 'field1', type = 'unsigned'}
format[2] = {name = 'field2', type = 'unsigned'}
s = box.schema.create_space('test', {engine = engine, format = format})
pk = s:create_index('pk')
s:replace{1, 1}
s:replace{100, 100}
s:replace{50, 50}
s:replace{25, box.NULL}

format[2].is_nullable = true
s:format(format)
s:replace{25, box.NULL}
s:replace{10, box.NULL}
s:replace{150, box.NULL}
s:select{}
s:drop()

s = box.schema.create_space('test', {engine = engine})
pk = s:create_index('pk')
sk = s:create_index('sk', {parts = {{2, 'unsigned', is_nullable = false}}})
s:replace{1, 1}
s:replace{100, 100}
s:replace{50, 50}
s:replace{25, box.NULL}
sk:alter({parts = {{2, 'unsigned', is_nullable = true}}})
s:replace{25, box.NULL}
s:replace{10, box.NULL}
s:replace{150, box.NULL}
sk:select{}
s:drop()

--
-- gh-2988: allow absense of tail nullable indexed fields.
--
s = box.schema.space.create('test', {engine = engine})
pk = s:create_index('pk')
sk = s:create_index('sk', {parts = {{2, 'unsigned', is_nullable = true}}})

-- Test tuple_compare_slowpath, tuple_compare_with_key_slowpath.

s:replace{} -- Fail
-- Compare full vs not full.
s:replace{2}
s:replace{1, 2}
s:select{}
sk:select{box.NULL}
sk:select{2}
-- Compare not full vs full.
s:replace{4, 5}
s:replace{3}
s:select{}
sk:select{box.NULL}
sk:select{5}
-- Compare extended keys.
s:replace{7}
s:replace{6}
s:select{}
sk:select{box.NULL}
sk:select{}
-- Test tuple extract key during dump for vinyl.
box.snapshot()
sk:select{}
s:select{}

-- Test tuple_compare_sequential_nullable,
-- tuple_compare_with_key_sequential.
s:drop()
s = box.schema.space.create('test', {engine = engine})
pk = s:create_index('pk')
parts = {}
parts[1] = {1, 'unsigned'}
parts[2] = {2, 'unsigned', is_nullable = true}
parts[3] = {3, 'unsigned', is_nullable = true}
sk = s:create_index('sk', {parts = parts})
-- Compare full vs not full.
s:replace{1, 2, 3}
s:replace{3}
s:replace{2, 3}
sk:select{}
sk:select{3, box.NULL}
sk:select{3, box.NULL, box.NULL}
sk:select{2}
sk:select{2, 3}
sk:select{3, 100}
sk:select{3, box.NULL, 100}
sk:select({3, box.NULL}, {iterator = 'GE'})
sk:select({3, box.NULL}, {iterator = 'LE'})
s:select{}
-- Test tuple extract key for vinyl.
box.snapshot()
sk:select{}
sk:select{3, box.NULL}
sk:select{3, box.NULL, box.NULL}
sk:select{2}
sk:select{2, 3}
sk:select{3, 100}
sk:select{3, box.NULL, 100}
sk:select({3, box.NULL}, {iterator = 'GE'})
sk:select({3, box.NULL}, {iterator = 'LE'})

-- Test a tuple_compare_sequential() for a case, when there are
-- two equal tuples, but in one of them field count < unique field
-- count.
s:replace{1, box.NULL}
s:replace{1, box.NULL, box.NULL}
s:select{1}

--
-- Partially sequential keys. See tuple_extract_key.cc and
-- contains_sequential_parts template flag.
--
s:drop()
s = box.schema.space.create('test', {engine = engine})
pk = s:create_index('pk')
parts = {}
parts[1] = {2, 'unsigned', is_nullable = true}
parts[2] = {3, 'unsigned', is_nullable = true}
parts[3] = {5, 'unsigned', is_nullable = true}
parts[4] = {6, 'unsigned', is_nullable = true}
parts[5] = {4, 'unsigned', is_nullable = true}
parts[6] = {7, 'unsigned', is_nullable = true}
sk = s:create_index('sk', {parts = parts})
s:insert{1, 1, 1, 1, 1, 1, 1}
s:insert{8, 1, 1, 1, 1, box.NULL}
s:insert{9, 1, 1, 1, box.NULL}
s:insert{6, 6}
s:insert{10, 6, box.NULL}
s:insert{2, 2, 2, 2, 2, 2}
s:insert{7}
s:insert{5, 5, 5}
s:insert{3, 5, box.NULL, box.NULL, box.NULL}
s:insert{4, 5, 5, 5, box.NULL}
s:insert{11, 4, 4, 4}
s:insert{12, 4, box.NULL, 4}
s:insert{13, 3, 3, 3, 3}
s:insert{14, box.NULL, 3, box.NULL, 3}
s:select{}
sk:select{}
sk:select{5, 5, box.NULL}
sk:select{5, 5, box.NULL, 100}
sk:select({7, box.NULL}, {iterator = 'LT'})
box.snapshot()
sk:select{}
sk:select{5, 5, box.NULL}
sk:select{5, 5, box.NULL, 100}
sk:select({7, box.NULL}, {iterator = 'LT'})

s:drop()

--
-- The main case of absent nullable fields - create an index over
-- them on not empty space (available on memtx only).
--
s = box.schema.space.create('test', {engine = engine})
pk = s:create_index('pk')
s:replace{1}
s:replace{2}
s:replace{3}
sk = s:create_index('sk', {parts = {{2, 'unsigned', is_nullable = true}}})
s:replace{4}
s:replace{5, 6}
s:replace{7, 8}
s:replace{9, box.NULL}
s:select{}
sk:select{}
sk:select{box.NULL}
s:drop()

--
-- The complex case: when an index part is_nullable is set to,
-- false and it changes min_field_count, this part must become
-- optional and turn on comparators for optional fields. See the
-- big comment in alter.cc in index_def_new_from_tuple().
--
s = box.schema.create_space('test', {engine = engine})
pk = s:create_index('pk')
sk = s:create_index('sk', {parts = {2, 'unsigned'}})
s:replace{1, 1}
s:replace{2, box.NULL}
s:select{}
sk:alter({parts = {{2, 'unsigned', is_nullable = true}}})
s:replace{20, box.NULL}
sk:select{}
s:replace{10}
sk:select{}
s:replace{40}
sk:select{}
s:drop()

--
-- Check that if an index alter makes a field be optional, and
-- this field is used in another index, then this another index
-- is updated too. Case of @locker.
--
s = box.schema.space.create('test', {engine = engine})
_ = s:create_index('pk')
i1 = s:create_index('i1', {parts = {2, 'unsigned', 3, 'unsigned'}})
i2 = s:create_index('i2', {parts = {3, 'unsigned', 2, 'unsigned'}})

i1:alter{parts = {{2, 'unsigned'}, {3, 'unsigned', is_nullable = true}}}
-- i2 alter makes i1 contain optional part. Its key_def and
-- comparators must be updated.
i2:alter{parts = {{3, 'unsigned', is_nullable = true}, {2, 'unsigned'}}}
s:insert{1, 1}
s:insert{100, 100}
s:insert{50, 50}
s:insert{25, 25, 25}
s:insert{75, 75, 75}
s:select{}
i1:select{}
i2:select{}
i2:select{box.NULL, 50}
i2:select{}
s:drop()

-- gh-3430 allow different nullability in space format and indices.
-- Resulting field nullability is the strictest of the two.
s = box.schema.space.create('test', {engine=engine})
pk = s:create_index('primary', {parts={1, 'unsigned'}})
sk = s:create_index('secondary', {parts={2, 'unsigned', is_nullable=false}})
format = {}
format[1] = {name = 'first', type = 'unsigned', is_nullable = false}
format[2] = {name = 'second', type = 'unsigned', is_nullable = false}
s:format(format)
-- Field 2 is not nullable.
s:insert{5}
s:insert{5, box.NULL}

s.index.secondary:alter{parts={{2, 'unsigned', is_nullable=true}}} -- This is allowed.
-- Without space format setting this fails.
s:insert{5, box.NULL}
s:insert{5}

s.index.secondary:alter{parts={{2, 'unsigned', is_nullable=false}}}
format[2].is_nullable = true
s:format(format) -- This is also allowed.
-- inserts still fail due to not nullable index parts.
s:insert{5, box.NULL}
s:insert{5}

s.index.secondary:alter{parts={{2, 'unsigned', is_nullable=true}}}
-- Now the field really is nullable.
-- Success.
s:insert{5, box.NULL}
s:insert{6}
s:insert{7, 8}
s:select{}

-- Check that we cannot set field nullability to false when the
-- space has tuples with NULL in this field.
format[2].is_nullable = false
s:format(format) -- Fail.
s.index.secondary:alter{parts={{2, 'unsigned', is_nullable=false}}} -- Fail.
_ = s:delete{5}
s:format(format) -- Still fail.
s.index.secondary:alter{parts={{2, 'unsigned', is_nullable=false}}} -- Still fail.
-- Now check we can set nullability to false step by step.
_ = s:delete{6}

format[2].is_nullable = false
s:format(format)
s:insert{5, box.NULL} -- Fail.
s:insert{5} -- Fail.
format[2].is_nullable = true
s:format(format)
s.index.secondary:alter{parts={{2, 'unsigned', is_nullable=false}}}
s:insert{5, box.NULL} -- Fail.
s:insert{5} -- Fail.
format[2].is_nullable = false
s:format(format)
s:select{}
s:insert{5} -- Fail.
s:insert{9, 10} -- Success.

s:drop()

-- gh-3744: Assertion after improper index creation
s = box.schema.space.create('test', {engine=engine})
pk = s:create_index('primary', {parts={1, 'unsigned'}})
sk1 = s:create_index('sk1', {parts={{2, 'number', is_nullable=false}}})
s:insert{1, -1, 1}
sk2 = s:create_index('sk2', {parts={{2, 'number', is_nullable=true}}})
s:insert{2, nil, 2} --error
s:drop()

--
-- gh-4480: Introduce exclude_null option
--

-- Basic functionality
s = box.schema.space.create('test', {engine=engine})
pk = s:create_index('primary', {parts={1, 'number'}})
sk1 = s:create_index('sk1', {parts={{2, 'number', is_nullable=true, exclude_null=true}}})
sk1.parts
sk2 = s:create_index('sk2', {parts={{2, 'number', is_nullable=true, exclude_null=false}}})
sk2.parts

s:insert{1, 1}
s:insert{2, box.NULL}
pk:select{}
sk1:select{} -- [1, 1] only
sk2:select{}

-- Error
sk3 = s:create_index('sk3', {parts={{2, 'number', is_nullable=false}}})
s:truncate()

-- Ok
sk3 = s:create_index('sk3', {parts={{2, 'number', is_nullable=false}}})

s:insert{1, 1}
s:insert{2, box.NULL} -- error

s:drop()

-- if exclude_null=true, then is_nullable=true (explicitly or implicitly)
s = box.schema.space.create('test', {engine=engine})
pk = s:create_index('primary', {parts={1, 'number'}})
sk1 = s:create_index('sk1', {parts={{2, 'number', is_nullable=false, exclude_null=true}}})
sk1 = s:create_index('sk1', {parts={{2, 'number', exclude_null=true}}})
s:drop()

-- Multipart
s = box.schema.space.create('test', {engine=engine})
pk = s:create_index('primary', {parts={1, 'number'}})
parts = {}
parts[1] = {2, 'number', exclude_null=true}
parts[2] = {3, 'number', is_nullable=true}
sk1 = s:create_index('sk1', {parts=parts})

s:insert{1, 1, 1}
s:insert{2, box.NULL, 2}
s:insert{3, 3, box.NULL}
s:select{}
sk1:select{}
s:drop()

-- Insert, then create index
s = box.schema.space.create('test', {engine=engine})
pk = s:create_index('primary', {parts={1, 'number'}})

s:insert{1, 11}
s:insert{2, 22}
s:insert{3, box.NULL}
s:insert{4, box.NULL}

sk1 = s:create_index('sk1', {parts={{2, 'number', exclude_null=true}}})
sk1.parts
s:select{}
sk1:select{}

-- Alter to exclude_null=false
sk1:alter({parts={{2, 'number', is_nullable=true, exclude_null=false}}})
sk1.parts
sk1:select{}

-- Alter back to exclude_null=true
sk1:alter({parts={{2, 'number', exclude_null=true}}})
sk1.parts
sk1:select{}

-- Alter to the wrong format
sk1:alter({parts={{2, 'number', is_nullable=false, exclude_null=true}}})
sk1.parts

-- Check that delete doesn't crash on sk1
s:insert{5, box.NULL}
sk1:select{}
_ = s:delete{5}
sk1:select{}
s:select{}

-- Update and upsert correctness
_ = s:update(1, {{'=', 2, box.NULL}})
_ = s:update(3, {{'=', 2, 33}})
s:upsert({2, 22}, {{'=', 2, box.NULL}})
s:upsert({5, 55}, {{'=', 2, box.NULL}})
sk1:select{}
s:select{}

-- Replace correctness
_ = s:replace({1, 11})
_ = s:replace({3, box.NULL})
sk1:select{}
s:select{}
s:drop()

-- Tuple without the field
s = box.schema.space.create('test', {engine = engine})
pk = s:create_index('pk', {type='tree', parts={{1, 'uint'}}})
sk = s:create_index('sk', {type='tree', parts={{2, 'uint', exclude_null=true}}})
s:replace{1}
sk:select{}
s:select{}
s:drop()

-- Forbid creating primary key with exclude_null option
s = box.schema.space.create('test', {engine=engine})
pk = s:create_index('primary', {parts={1, 'number', exclude_null=true}})
s:drop()

-- Restore from snapshot
s = box.schema.space.create('test', {engine=engine})
pk = s:create_index('primary', {parts={1, 'number'}})
sk = s:create_index('sk', {parts={{2, 'number', exclude_null=true}}})
s:replace{1, box.NULL}
s:replace{2, 2}
s:select{}
sk:select{}
box.snapshot()
test_run:cmd("restart server default")
s = box.space.test
sk = s.index.sk
s:select{}
sk:select{}
s:drop()

-- First upsert
engine = test_run:get_cfg('engine')
s = box.schema.space.create('test', {engine=engine})
pk = s:create_index('pk')
sk = s:create_index('sk', {parts={{2, 'number', exclude_null=true}}})

s:replace{1, 1}
box.snapshot()
s:upsert({2, box.NULL}, {{'=', 2, box.NULL}})
s:select{}
sk:select{}
s:drop()

-- Both old and new tuple are null when creating index
s = box.schema.space.create('test', {engine=engine})
pk = s:create_index('pk')
fiber = require('fiber')
s:replace{1, box.NULL}
sk = s:create_index('sk', {parts={{2, 'number', exclude_null=true}}})
s:select{}
sk:select{}
s:drop()

-- Check that vy_build_on_replace works as expected
errinj = box.error.injection

s = box.schema.space.create('test', {engine=engine})
pk = s:create_index('primary', {parts={1, 'number'}})
s:replace{42, 42}
errinj.set('ERRINJ_BUILD_INDEX_DELAY', true)
ch = fiber.channel(1)

test_run:cmd("setopt delimiter ';'")
function txn_fun()
    box.begin()
    s:replace{1, box.NULL}
    errinj.set('ERRINJ_BUILD_INDEX_DELAY', false)
    box.commit()
    ch:put(true)
end;
test_run:cmd("setopt delimiter ''");

_ = fiber.new(txn_fun)
sk = s:create_index('sk', {parts={{2, 'number', exclude_null=true}}})
ch:get()
sk:select{}
s:drop()

-- gh-5674: ignoring index part options when type is omitted
s = box.schema.space.create('test', {engine=engine})
_ = s:create_index('pk', {parts = {1, 'int'}})
sk = s:create_index('sk', {parts = {2, is_nullable=true}})
sk.parts
sk:drop()
sk = s:create_index('sk', {parts = {2, is_nullable=true, collation='unicode'}})
sk.parts
sk:drop()
sk = s:create_index('sk', {parts = {2, type='string', is_nullable=true, collation='unicode'}})
sk.parts
sk:drop()
sk = s:create_index('sk', {parts = {2, is_nullable=true, 3}})
sk.parts
sk:drop()
sk = s:create_index('sk', {parts = {2, 3}})
sk.parts
sk:drop()
sk = s:create_index('sk', {parts = {2, 3, 4}})
sk.parts
sk:drop()
sk = s:create_index('sk', {parts = {{2, 'int'}, {3, 'string'}}})
sk.parts
sk:drop()
sk = s:create_index('sk', {parts = {{2, is_nullable=true}, {3, collation='unicode'}}})
sk.parts
sk:drop()
s:drop()

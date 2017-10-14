env = require('test_run')
test_run = env.new()
engine = test_run:get_cfg('engine')
NULL = require('msgpack').NULL

--
-- gh-1557: NULL in indexes.
--

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

-- Conflict of is_nullable in format and in parts.
parts[1].is_nullable = false
sk = s:create_index('sk', { parts = parts }) -- Fail.

-- Try skip nullable in format and specify in part.
parts[1].is_nullable = true
sk = s:create_index('sk', { parts = parts }) -- Ok.
format[2].is_nullable = nil
s:format(format) -- Fail.
sk:drop()

-- Try to set nullable in part with no format.
s:format({})
sk = s:create_index('sk', { parts = parts })
-- And then set format with no nullable.
s:format(format) -- Fail.
format[2].is_nullable = true
s:format(format) -- Ok.

-- Test insert.

s:insert{1, 1}
s:insert{2, NULL}
s:insert{3, NULL}
s:insert{4, 1} -- Fail.
s:insert{4, 4}
s:insert{5, NULL}

pk:select{}
sk:select{}

-- Test exact match.

sk:get({1})
sk:get({NULL}) -- Fail.

sk:update({1}, {})
sk:update({NULL}, {}) -- Fail.

_ = sk:delete({1})
sk:delete({NULL}) -- Fail.
s:insert({1, 1})

-- Test iterators.

sk:select{NULL}
sk:select({NULL}, {iterator = 'LE'})
sk:select({NULL}, {iterator = 'LT'})
sk:select({NULL}, {iterator = 'GE'})
sk:select({NULL}, {iterator = 'GT'})

_ = sk:delete{NULL}
sk:select{}
pk:select{}

-- Test snapshot during iterator (Vinyl restore).

create_iterator = require('utils').create_iterator

iter = create_iterator(sk, {NULL})
iter.next()

box.snapshot()
iter.iterate_over()

sk:select{}
pk:select{}

-- Test replace.

s:replace{2, 2}
s:replace{3, NULL} -- no changes.
s:replace{6, NULL}

pk:select{}
sk:select{}

-- Test not unique indexes.

s:truncate()
sk:drop()
sk = s:create_index('sk', { parts = parts, unique = false })

s:insert{1, 1}
s:insert{2, NULL}
s:insert{3, NULL}
s:insert{4, 1}
s:insert{5, NULL}

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
s:replace{2, NULL, NULL}
s:replace{1, NULL, 1}
s:replace{0, 0, NULL}

pk:select{}
sk:select{}
sk2:select{}

-- Check duplicate conflict on replace.

s:replace{4, 4, 3} -- fail
s:replace{4, 4, NULL} -- ok

pk:select{}
sk:select{}
sk2:select{}

_ = pk:delete{2}
pk:select{}
sk:select{}
sk2:select{}

s:drop()

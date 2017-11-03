--
-- gh-2784: do not validate space formatted but not indexed fields
-- in surrogate statements.
--

-- At first, test simple surrogate delete generated from a key.
format = {{name = 'a', type = 'unsigned'}, {name = 'b', type = 'unsigned'}}
s = box.schema.space.create('test', {engine = 'vinyl', format = format})
_ = s:create_index('pk')
s:insert{1, 1}
-- Type of a second field in a surrogate tuple must be NULL but
-- with UNSIGNED type, specified in a tuple_format. It is
-- possible, because only indexed fields are used in surrogate
-- tuples.
s:delete(1)
s:drop()

-- Test select after snapshot. This select gets surrogate
-- tuples from a disk. Here NULL also can be stored in formatted,
-- but not indexed field.
format = {}
format[1] = {name = 'a', type = 'unsigned'}
format[2] = {name = 'b', type = 'unsigned'}
format[3] = {name = 'c', type = 'unsigned'}
s = box.schema.space.create('test', {engine = 'vinyl', format = format})
_ = s:create_index('pk')
_ = s:create_index('sk', {parts = {2, 'unsigned'}})
s:insert{1, 1, 1}
box.snapshot()
s:delete(1)
box.snapshot()
s:select()
s:drop()

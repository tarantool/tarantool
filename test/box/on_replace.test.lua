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

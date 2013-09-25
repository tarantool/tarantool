ts = box.schema.create_space('ttest_space')
ts:create_index('primary', 'hash')

type(ts.on_replace)
ts.on_replace()
ts:on_replace()
ts:on_replace(123)

type(ts:on_replace(function(old_tuple, new_tuple) error('test') end))


type(ts:on_replace())
ts:on_replace()()

ts:insert(1, 'b', 'c')
ts:select(0, 1)


ts:on_replace(nil)

ts:insert(1, 'b', 'c')
ts:select(0, 1)


type(ts:on_replace(function(old_tuple, new_tuple) error('abc') end))


ts:insert(2, 'b', 'c')
ts:select(0, 2)



type(ts:on_replace(function(told, tnew) o = told n = tnew end))

ts:insert(2, 'a', 'b', 'c')
o == nil
n ~= nil
n[1] == 'a'
n[2] == 'b'
n[3] == 'c'

ts:replace(2, 'b', 'c', 'd')
o ~= nil
n ~= nil
o[1] == 'a'
n[1] == 'b'
o[2] == 'b'
n[2] == 'c'


ts:drop()

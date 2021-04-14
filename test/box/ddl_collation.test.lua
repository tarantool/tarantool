env = require('test_run')
test_run = env.new()

-- collation
function setmap(table) return setmetatable(table, { __serialize = 'map' }) end

box.internal.collation.create('test')
box.internal.collation.create('test', 'ICU')
box.internal.collation.create(42, 'ICU', 'ru_RU')
box.internal.collation.create('test', 42, 'ru_RU')
box.internal.collation.create('test', 'ICU', 42)
box.internal.collation.create('test', 'nothing', 'ru_RU')
box.internal.collation.create('test', 'ICU', 'ru_RU', setmap{}) --ok
err, res = pcall(function() return box.internal.collation.create('test', 'ICU', 'ru_RU') end)
assert(res.code == box.error.TUPLE_FOUND)
box.internal.collation.drop('test')
box.internal.collation.drop('nothing') -- allowed
box.internal.collation.create('test', 'ICU', 'ru_RU', 42)
box.internal.collation.create('test', 'ICU', 'ru_RU', 'options')
box.internal.collation.create('test', 'ICU', 'ru_RU', {ping='pong'})
box.internal.collation.create('test', 'ICU', 'ru_RU', {french_collation='german'})
box.internal.collation.create('test', 'ICU', 'ru_RU', {french_collation='on'}) --ok
box.internal.collation.drop('test') --ok
box.internal.collation.create('test', 'ICU', 'ru_RU', {strength='supervillian'})
box.internal.collation.create('test', 'ICU', 'ru_RU', {strength=42})
box.internal.collation.create('test', 'ICU', 'ru_RU', {strength=2}) --ok
box.internal.collation.drop('test') --ok
box.internal.collation.create('test', 'ICU', 'ru_RU', {strength='primary'}) --ok
box.internal.collation.drop('test') --ok
c = box.space._collation:get{1}:totable()
c[2] = 'unicode_test'
box.space._collation:replace(c)

box.begin() box.internal.collation.create('test2', 'ICU', 'ru_RU') box.rollback()

box.internal.collation.create('test', 'ICU', 'ru_RU')
box.internal.collation.exists('test')

test_run:cmd('restart server default')
function setmap(table) return setmetatable(table, { __serialize = 'map' }) end

box.internal.collation.exists('test')
box.internal.collation.drop('test')

box.space._collation:auto_increment{'test'}
box.space._collation:auto_increment{'test', 0, 'ICU'}
box.space._collation:auto_increment{'test', 'ADMIN', 'ICU', 'ru_RU'}
box.space._collation:auto_increment{42, 0, 'ICU', 'ru_RU'}
box.space._collation:auto_increment{'test', 0, 42, 'ru_RU'}
box.space._collation:auto_increment{'test', 0, 'ICU', 42}
box.space._collation:auto_increment{'test', 0, 'ICU', 'ru_RU', setmap{}} --ok
err, res = pcall(function() return box.space._collation:auto_increment{'test', 0, 'ICU', 'ru_RU', setmap{}} end)
assert(res.code == box.error.TUPLE_FOUND)
box.space._collation.index.name:delete{'test'} -- ok
box.space._collation.index.name:delete{'nothing'} -- allowed
box.space._collation:auto_increment{'test', 0, 'ICU', 'ru_RU', 42}
box.space._collation:auto_increment{'test', 0, 'ICU', 'ru_RU', 'options'}
box.space._collation:auto_increment{'test', 0, 'ICU', 'ru_RU', {ping='pong'}}
opts = {normalization_mode='NORMAL'}
box.space._collation:auto_increment{'test', 0, 'ICU', 'ru_RU', opts}
opts.normalization_mode = 'OFF'
_ = box.space._collation:auto_increment{'test', 0, 'ICU', 'ru_RU', opts} -- ok
_ = box.space._collation.index.name:delete{'test'} -- ok
opts.numeric_collation = 'PERL'
box.space._collation:auto_increment{'test', 0, 'ICU', 'ru_RU', opts}
opts.numeric_collation = 'ON'
_ = box.space._collation:auto_increment{'test', 0, 'ICU', 'ru_RU', opts} --ok
_ = box.space._collation.index.name:delete{'test'} -- ok
opts.alternate_handling1 = 'ON'
box.space._collation:auto_increment{'test', 0, 'ICU', 'ru_RU', opts}
opts.alternate_handling1 = nil
opts.alternate_handling = 'ON'
box.space._collation:auto_increment{'test', 0, 'ICU', 'ru_RU', opts}
opts.alternate_handling = 'SHIFTED'
_ = box.space._collation:auto_increment{'test', 0, 'ICU', 'ru_RU', opts} --ok
_ = box.space._collation.index.name:delete{'test'} -- ok
opts.case_first = 'ON'
box.space._collation:auto_increment{'test', 0, 'ICU', 'ru_RU', opts}
opts.case_first = 'OFF'
_ = box.space._collation:auto_increment{'test', 0, 'ICU', 'ru_RU', opts} --ok
_ = box.space._collation.index.name:delete{'test'} -- ok
opts.case_level = 'UPPER'
box.space._collation:auto_increment{'test', 0, 'ICU', 'ru_RU', opts}
opts.case_level = 'DEFAULT'
_ = box.space._collation:auto_increment{'test', 0, 'ICU', 'ru_RU', opts} --ok
_ = box.space._collation.index.name:delete{'test'} -- ok

box.space._collation:auto_increment{'test', 0, 'ICU', 'ru_RU', setmap{}}
box.space._collation:select{}
test_run:cmd('restart server default')
box.space._collation:select{}
box.space._collation.index.name:delete{'test'}

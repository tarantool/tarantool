test_run = require('test_run').new()

s = box.schema.space.create('test')
_ = s:create_index('primary')
_ = s:create_index('secondary', {unique = false, parts = {2, 'unsigned'}})

function fail(old, new) error('fail') end
function save(old, new) old_tuple = old new_tuple = new end
function ret_new(old, new) return new end
function ret_old(old, new) return old end
function ret_nil(old, new) return nil end
function ret_null(old, new) return nil end
function ret_none(old, new) return end
function ret_invalid(old, new) return 'test' end
function ret_update(old, new) return box.tuple.update(new, {{'+', 3, 1}}) end
function ret_update_pk(old, new) return box.tuple.update(new, {{'+', 1, 1}}) end

-- Exception in trigger.
s:before_replace(fail) == fail
s:insert{1, 1}
s:select()
s:before_replace(nil, fail)

-- Check 'old' and 'new' trigger arguments.
old_tuple = nil
new_tuple = nil
s:before_replace(save) == save
s:insert{1, 1}
old_tuple, new_tuple
s:replace{1, 2}
old_tuple, new_tuple
s:update(1, {{'+', 2, 1}})
old_tuple, new_tuple
s:upsert({1, 1}, {{'=', 2, 1}})
old_tuple, new_tuple
s:upsert({2, 2}, {{'=', 2, 2}})
old_tuple, new_tuple
s:select()
s:delete(1)
old_tuple, new_tuple
s:delete(2)
old_tuple, new_tuple
s:select()
s:before_replace(nil, save)

-- Returning 'new' from trigger doesn't affect statement.
s:before_replace(ret_new) == ret_new
s:insert{1, 1}
s:update(1, {{'+', 2, 1}})
s:select()
s:delete(1)
s:select()
s:before_replace(nil, ret_new)

-- Returning 'old' from trigger skips statement.
s:insert{1, 1}
s:before_replace(ret_old) == ret_old
s:insert{2, 2}
s:update(1, {{'+', 2, 1}})
s:delete(1)
s:select()
s:before_replace(nil, ret_old)
s:delete(1)

-- Returning nil from trigger turns statement into DELETE.
s:insert{1, 1}
s:before_replace(ret_nil) == ret_nil
s:replace{1, 2}
s:select()
s:before_replace(nil, ret_nil)

-- Returning box.NULL from trigger turns statement into DELETE.
s:insert{1, 1}
s:before_replace(ret_null) == ret_null
s:replace{1, 2}
s:select()
s:before_replace(nil, ret_null)

-- Returning nothing doesn't affect the operation.
s:insert{1, 1}
s:insert{2, 2}
s:before_replace(ret_none) == ret_none
s:replace{1, 2}
s:update(1, {{'+', 2, 1}})
s:delete(2)
s:select()
s:before_replace(nil, ret_none)
s:delete(1)

-- Update statement from trigger.
s:before_replace(ret_update) == ret_update
s:insert{1, 1, 1}
s:update(1, {{'+', 2, 1}})
s:select()
s:before_replace(nil, ret_update)
s:delete(1)

-- Invalid return value.
s:before_replace(ret_invalid) == ret_invalid
s:insert{1, 1}
s:select()
s:before_replace(nil, ret_invalid)

-- Update of the primary key from trigger is forbidden.
s:insert{1, 1}
s:before_replace(ret_update_pk) == ret_update_pk
s:replace{1, 2}
s:before_replace(nil, ret_update_pk)
s:delete(1)

-- Update over secondary index + space:before_replace.
s2 = box.schema.space.create('test2')
_ = s2:create_index('pk')
_ = s2:create_index('sk', {unique = true, parts = {2, 'unsigned'}})
s2:insert{1, 1, 1, 1}
s2:before_replace(ret_update) == ret_update
s2.index.sk:update(1, {{'+', 4, 1}})
s2:select()
s2:drop()

-- Stacking triggers.
old_tuple = nil
new_tuple = nil
s:before_replace(save) == save
s:before_replace(ret_update) == ret_update
s:insert{1, 1, 1}
old_tuple, new_tuple
s:before_replace(nil, save)
s:before_replace(nil, ret_update)
s:delete(1)

-- Issue DML from trigger.
s2 = box.schema.space.create('test2')
_ = s2:create_index('pk')
cb = function(old, new) s2:insert{i, old, new} end
s:before_replace(cb) == cb
i = 1
s:insert{1, 1}
i = 2
s:replace{1, 2}
s:replace{1, 3} -- error: conflict in s2
s:select()
s2:select()

-- DML done from space:before_replace is undone
-- if space:on_replace fails.
s:truncate()
s2:truncate()
s:on_replace(fail) == fail
s:replace{1, 3}
s:select()
s2:select()
s:on_replace(nil, fail)
s:before_replace(nil, cb)
s2:drop()

-- If space:before_replace turns the request into NOP,
-- space:on_replace isn't called.
old_tuple = nil
new_tuple = nil
s:insert{1, 1}
s:before_replace(ret_old) == ret_old
s:on_replace(save) == save
s:replace{1, 2}
old_tuple, new_tuple
s:delete(1)
old_tuple, new_tuple
s:insert{2, 2}
old_tuple, new_tuple
s:select()
s:before_replace(nil, ret_old)
s:on_replace(nil, save)
s:delete(1)

-- Changed done in space:before_replace are visible
-- in space:on_replace
old_tuple = nil
new_tuple = nil
s:before_replace(ret_update) == ret_update
s:on_replace(save) == save
s:insert{1, 1, 1}
old_tuple, new_tuple
s:replace{1, 2, 2}
old_tuple, new_tuple
s:select()
s:before_replace(nil, ret_update)
s:on_replace(nil, save)
s:delete(1)

-- Nesting limit: space.before_replace
cb = function(old, new) s:insert{1, 1} end
s:before_replace(cb) == cb
s:insert{1, 1} -- error
s:select()
s:before_replace(nil, cb)

-- Nesting limit: space.before_replace + space.on_replace
cb = function(old, new) s:delete(1) end
s:before_replace(cb) == cb
s:on_replace(cb) == cb
s:insert{1, 1} -- error
s:select()
s:before_replace(nil, cb)
s:on_replace(nil, cb)

-- Make sure the server can recover from xlogs after
-- using space:before_replace.
test_run:cmd('restart server default')

s = box.space.test
s:select()

-- Check that IPROTO_NOP is actually written to xlog.
fio = require('fio')
xlog = require('xlog')

type(s:before_replace(function(old, new) return old end))
s:insert{1, 1}

path = fio.pathjoin(box.cfg.wal_dir, string.format('%020d.xlog', box.info.lsn - 1))
fun, param, state = xlog.pairs(path)
state, row = fun(param, state)
row.HEADER.type
row.BODY.space_id == s.id

-- gh-3128 before_replace with run_triggers
s2 = box.schema.space.create("test2")
_ = s2:create_index("prim")
before_replace1 = function() s2:insert{1} s:run_triggers(false) end
before_replace2 = function() s2:insert{2} end
on_replace = function() s2:insert{3} end

type(s:on_replace(on_replace))
type(s:before_replace(before_replace1))
type(s:before_replace(before_replace2))

s:insert{1, 1}
s2:select{}
s:truncate()
s2:truncate()

s:on_replace(nil, on_replace)
s:before_replace(nil, before_replace1)
s:before_replace(nil, before_replace2)

--
-- gh-3128
-- If at least one before trigger returns old
-- insertion will be aborted, but other before triggers
-- will be executed
before_replace1 = function(old, new) s2:insert{1} return old end
before_replace2 = function(old, new) s2:insert{2} end

type(s:on_replace(on_replace))
type(s:before_replace(before_replace1))
type(s:before_replace(before_replace2))

s:insert{1, 1}
s:select{}
s2:select{}

s:on_replace(nil, on_replace)
s:before_replace(nil, before_replace1)
s:before_replace(nil, before_replace2)
s2:drop()
s:drop()

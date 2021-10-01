test_run = require('test_run').new()
netbox = require('net.box')
fio = require('fio')
errinj = box.error.injection

-- Check that an invalid listening uri
-- does not make tarantool blind.
bad_uri = "baduribaduri:1"
old_listen = box.cfg.listen
box.cfg({ listen = bad_uri })
conn = netbox.connect(old_listen)
assert(conn:ping())
assert(fio.path.exists(old_listen))
assert(box.cfg.listen == old_listen)

-- Check that failure in listen does
-- not make tarantool blind and not
-- leads to unreleased resources.
errinj.set("ERRINJ_IPROTO_CFG_LISTEN", 1)
new_listen = old_listen .. "A"
box.cfg({ listen = new_listen })
test_run:wait_cond(function() return fio.path.exists(old_listen) end)
test_run:wait_cond(function() return not fio.path.exists(new_listen) end)
conn = netbox.connect(old_listen)
assert(conn:ping())
assert(box.cfg.listen == old_listen)

-- Check the error message when listen fails
-- and reverts to old listen fails also.
-- We set 'ERRINJ_IPROTO_CFG_LISTEN' to 2, so
-- listen fails and rollback to old value fails
-- also. So we rollback to special value (for
-- box.cfg.listen it's nil), it can't fails.
errinj.set("ERRINJ_IPROTO_CFG_LISTEN", 2)
new_listen = old_listen .. "A"
box.cfg({ listen = new_listen })
test_run:wait_cond(function() return not fio.path.exists(old_listen) end)
test_run:wait_cond(function() return not fio.path.exists(new_listen) end)
conn = netbox.connect(old_listen)
assert(not conn:ping())
errmsg = "failed to revert 'listen' configuration option: " .. \
         "Error injection 'iproto listen'"
assert(test_run:grep_log('default', errmsg))
assert(box.cfg.listen == nil)
assert(box.info.listen == nil)


test_run = require('test_run').new()
--
-- gh-4729: box should not trust xrow header group id, received
-- from a remote client on DDL/DML.
--
box.schema.user.grant('guest', 'super')
s1 = box.schema.space.create('test_local', {is_local = true})
_ = s1:create_index('pk', {parts = {1, 'unsigned'}})
s2 = box.schema.space.create('test_normal')
_ = s2:create_index('pk', {parts = {1, 'unsigned'}})

test_run:cmd('create server replica with rpl_master=default, script="replication/replica.lua"')
test_run:cmd('start server replica')
test_run:switch('replica')

netbox = require('net.box')
c = netbox.connect(box.cfg.replication[1])
c.space.test_local:insert({1})
c.space.test_normal:insert({1})
c:close()
test_run:wait_cond(function()                                   \
    return box.space.test_normal ~= nil and                     \
           box.space.test_normal.index.pk ~= nil and            \
           box.space.test_normal:count() == 1                   \
end)
box.space.test_local:select{}
box.space.test_normal:select{}

test_run:switch('default')
test_run:cmd("stop server replica")
test_run:cmd("cleanup server replica")
test_run:cmd("delete server replica")
s1:select{}
s2:select{}
s1:drop()
s2:drop()
box.schema.user.revoke('guest', 'super')

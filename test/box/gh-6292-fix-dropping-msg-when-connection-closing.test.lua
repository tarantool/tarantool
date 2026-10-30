net = require('net.box')
fiber = require('fiber')
test_run = require('test_run').new()

box.schema.user.grant('guest', 'execute', 'universe')
old_readahead = box.cfg.readahead
old_net_msg_max = box.cfg.net_msg_max

--
-- Check that tarantool process all requests when connection is closing,
-- net_msg_max limit is reached and readahead limit is not reached:
--
box.cfg ({ net_msg_max = 2, readahead = 100 * 1024 * 1024 })
counter = 0
expected_counter = 1024
conn = net.connect(box.cfg.listen)
for i = 1, expected_counter do \
    conn:eval('counter = counter + 1', {}, {is_async = true}) \
end
conn:close()
test_run:wait_cond(function() return counter == expected_counter end)

--
-- Check that tarantool process all requests when connection is closing
-- readahead limit is reached and net_msg_max limit is not reached:
--
box.cfg ({ net_msg_max = 16 * 1024, readahead = 128 })
counter = 0
expected_counter = 1024
conn = net.connect(box.cfg.listen)
for i = 1, expected_counter do \
    conn:eval('counter = counter + 1', {}, {is_async = true}) \
end
conn:close()
test_run:wait_cond(function() return counter == expected_counter end)

box.cfg ({ net_msg_max = old_net_msg_max, readahead = old_readahead })
box.schema.user.revoke('guest', 'execute', 'universe')

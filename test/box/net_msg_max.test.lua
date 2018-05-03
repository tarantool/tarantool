test_run = require('test_run').new()

fiber = require('fiber')
net_box = require('net.box')

box.schema.user.grant('guest', 'read,write,execute', 'universe')
conn = net_box.connect(box.cfg.listen)
conn2 = net_box.connect(box.cfg.listen)
active = 0
finished = 0
continue = false
limit = 768
run_max = (limit - 100) / 2

old_readahead = box.cfg.readahead
box.cfg{readahead = 9000}
long_str = string.rep('a', 1000)

test_run:cmd("setopt delimiter ';'")
function do_long_f(...)
	active = active + 1
	while not continue do
		fiber.sleep(0.01)
	end
	active = active - 1
	finished = finished + 1
end;

function do_long(c)
	c:call('do_long_f', {long_str})
end;

function run_workers(c)
	finished = 0
	continue = false
	for i = 1, run_max do
		fiber.create(do_long, c)
	end
end;

-- Wait until 'active' stops growing - it means, that the input
-- is blocked.
function wait_active(value)
	while value ~= active do
		fiber.sleep(0.01)
	end
	fiber.sleep(0.01)
-- No more messages.
	assert(value == active)
end;

function wait_finished(needed)
	continue = true
	while finished ~= needed do fiber.sleep(0.01) end
end;
test_run:cmd("setopt delimiter ''");

--
-- Test that message count limit is reachable.
--
run_workers(conn)
run_workers(conn2)
wait_active(run_max * 2)
wait_finished(active)

--
-- Test that each message in a batch is checked. When a limit is
-- reached, other messages must be processed later.
--
run_max = limit * 5
run_workers(conn)
wait_active(limit + 1)
wait_finished(run_max)

conn2:close()
conn:close()

box.schema.user.revoke('guest', 'read,write,execute', 'universe')
box.cfg{readahead = old_readahead}

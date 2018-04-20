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
		fiber.sleep(0.1)
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
function wait_block()
	local old_val = -1
	while old_val ~= active do
		old_val = active
		fiber.sleep(0.1)
	end
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
wait_block()
active == run_max * 2 or active
wait_finished(active)

conn2:close()
conn:close()

box.schema.user.revoke('guest', 'read,write,execute', 'universe')
box.cfg{readahead = old_readahead}

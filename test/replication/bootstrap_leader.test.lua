test_run = require('test_run').new()

create_server_cmd = "create server replica%d with script='replication/replica_uuid_rw%d.lua'"

test_run:cmd("setopt delimiter ';'")

for i = 1,3 do
    test_run:cmd(string.format(create_server_cmd, i, i))
end;

test_run:cmd("start server replica1 with wait_load=True, wait=True");
test_run:cmd("start server replica2 with args='1,2,3 1.0 100500 0.1', wait_load=False, wait=False");
test_run:cmd("start server replica3 with args='1,2,3 0.1 0.5 100500', wait_load=True, wait=True");

test_run:cmd("switch replica3");
fiber = require('fiber');
while (#box.info.replication < 3) do
    fiber.sleep(0.05)
end;
test_run:cmd("switch default");

for i = 1,3 do
    test_run:cmd("stop server replica"..i.." with cleanup=1")
    test_run:cmd("delete server replica"..i)
end;

test_run:cmd("setopt delimiter ''");

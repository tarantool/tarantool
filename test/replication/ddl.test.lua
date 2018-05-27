test_run = require('test_run').new()

SERVERS = { 'ddl1', 'ddl2', 'ddl3', 'ddl4' }

-- Deploy a cluster.
test_run:create_cluster(SERVERS)
test_run:wait_fullmesh(SERVERS)
test_run:cmd("switch ddl1")
test_run = require('test_run').new()
fiber = require('fiber')

for i = 0, 199 do box.space.test:replace({1, 2, 3, 4}) box.space.test:truncate() box.space.test:truncate() end

fiber.sleep(0.001)

test_run:cmd("switch default")
test_run:drop_cluster(SERVERS)


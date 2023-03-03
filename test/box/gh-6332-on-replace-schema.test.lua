env = require('test_run')
test_run = env.new()

-- We are going to change content of system space manually, so just in case
-- let's start new instance.
--
test_run:cmd("create server test with script = \"box/lua/cfg_test1.lua\"")
test_run:cmd("start server test")
test_run:cmd("switch test")

box.space._schema:replace{'version'}
box.space._schema:replace{'version', 123}
box.space._schema:replace{'replicaset_uuid'}
box.space._schema:replace{'replicaset_uuid', 666}
box.space._schema:replace{'asd'}
box.space._schema:replace{666}

test_run:cmd("switch default")
test_run:cmd("stop server test")
test_run:cmd("cleanup server test")

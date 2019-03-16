test_run = require('test_run').new()
test_run:cmd("create server test with script='sql/vinyl-opts-cfg.lua'")
test_run:cmd("start server test")
test_run:cmd("switch test")

box.execute('pragma sql_default_engine= \'vinyl\'')
box.execute('CREATE TABLE v1 (id INT PRIMARY KEY, b INT);')
box.space.V1.index[0].options

box.execute('CREATE INDEX i1 ON v1(b);')
box.space.V1.index[1].options

box.space.V1:drop()

test_run:cmd('switch default')
test_run:cmd("stop server test")
test_run:cmd("cleanup server test")

--
-- gh-853 - memory leak on start if replace in xlog
--
env = require('test_run')
test_run = env.new()
test_run:cmd("create server tiny with script='box/tiny.lua'")
test_run:cmd("start server tiny")
test_run:cmd("switch tiny")
_ = box.schema.space.create('test')
_ = box.space.test:create_index('pk')
test_run:cmd("setopt delimiter ';'")
for i=1, 500 do
    box.space.test:replace{1, string.rep('a', 50000)}
-- or we run out of memory too soon
    collectgarbage('collect')
end;
test_run:cmd("setopt delimiter ''");
test_run:cmd('restart server tiny')
box.space.test:len()
box.space.test:drop()
test_run:cmd("switch default")
test_run:cmd("stop server tiny")
test_run:cmd("cleanup server tiny")


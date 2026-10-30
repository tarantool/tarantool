env = require('test_run')
test_run = env.new()
test_run:cmd("create server gh_5610_dirty_restart with script='box/gh-5610-dirty-restart.lua'")
test_run:cmd("start server gh_5610_dirty_restart")
test_run:cmd("switch gh_5610_dirty_restart")

fiber = require('fiber')
s = box.schema.space.create('test', {is_sync = true})
i = s:create_index('pk')
_ = fiber.new(function() box.space.test:insert{1} end)
s:select{}
fiber.sleep(0) -- to be sure
s:select{}

test_run:cmd("restart server gh_5610_dirty_restart")
box.space.test:select{}

test_run:cmd("switch default")
test_run:cmd("stop server gh_5610_dirty_restart")
test_run:cmd("cleanup server gh_5610_dirty_restart")

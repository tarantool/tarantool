env = require('test_run')
log = require('log')
test_run = env.new()

--
-- gh-1607: on_shutdown triggers.
--
f = function() log.warn('on_shutdown 1') end
g = function() log.warn('on_shutdown 2') end
h = function() log.warn('on_shutdown 3') end
-- Check that on_shutdown triggers may yield
-- and perform some complicated actions.
fiber = require('fiber')
test_run:cmd("setopt delimiter ';'")
trig = function()
    fiber.sleep(0.01)
    fiber.yield()
    box.schema.space.create("shutdown")
    box.space.shutdown:create_index("pk")
    box.space.shutdown:insert{1,2,3}
    log.warn('on_shutdown 4')
end;
test_run:cmd("setopt delimiter ''");
_ = box.ctl.on_shutdown(f)
_ = box.ctl.on_shutdown(g)
-- Check that replacing triggers works
_ = box.ctl.on_shutdown(h, g)
_ = box.ctl.on_shutdown(trig)
test_run:cmd('restart server default with wait=False')
test_run:wait_log('default', 'on_shutdown 1', nil, 30, {noreset=true})
test_run:grep_log('default', 'on_shutdown 2', nil, {noreset=true})
test_run:grep_log('default', 'on_shutdown 3', nil, {noreset=true})
test_run:grep_log('default', 'on_shutdown 4', nil, {noreset=true})
box.space.shutdown:select{}
box.space.shutdown:drop()

-- Check that os.exit invokes on_shutdown triggers
fiber = require("fiber")
test_run:cmd("create server test with script='box/proxy.lua'")
test_run:cmd("start server test")
logfile = test_run:eval("test", "box.cfg.log")[1]
test_run:cmd("stop server test")
-- clean up any leftover logs
require("fio").unlink(logfile)
test_run:cmd("start server test")
test_run:cmd("switch test")
log = require('log')
_ = box.ctl.on_shutdown(function() log.warn("on_shutdown 5") end)
-- Check that we don't hang infinitely after os.exit()
-- even if the following code doesn't yield.
fiber = require("fiber")
test_run:cmd("switch default")
-- eval the command on the 'test' instance while we're on default
-- instance to make sure test_run doesn't lose connection to the
-- shutting down instance.
test_run:eval("test", "_ = fiber.new(function() os.exit() while true do end end)")
fiber.sleep(0.1)
-- The server should be already stopped by os.exit(),
-- but start doesn't work without a prior call to stop.
test_run:cmd("stop server test")
test_run:cmd("start server test")
test_run:wait_log('test', 'on_shutdown 5', nil, 30, {noreset=true})
-- make sure we exited because of os.exit(), not a signal.
test_run:grep_log('test', 'signal', nil, {noreset=true})
test_run:cmd("stop server test")
test_run:cmd("cleanup server test")
test_run:cmd("delete server test")

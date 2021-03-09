env = require('test_run')
fiber=require('fiber')
test_run = env.new()
build_dir = os.getenv("BUILDDIR") .. "/test/app/"
soext = (jit.os == "OSX" and "dylib" or "so")
xtmlib = "xtmlib."..soext
os.execute(string.format("cp %s/%s .", build_dir, xtmlib))

test_run:cmd('create server test with script="app/xtm.lua"')
test_run:cmd('start server test')
test_run:cmd('switch test')
module = require('xtmlib')
module.cfg{}
test_run:cmd('switch default')
while test_run:grep_log("test", "module_msg_func called") == nil do fiber.sleep(0.1) end
while test_run:grep_log("test", "tx_msg_func called") == nil do fiber.sleep(0.1) end
test_run:cmd('switch test')
module.stop()
test_run:cmd('switch default')
test_run:cmd('stop server test')
os.execute(string.format("rm %s", xtmlib))
test_run:cmd('cleanup server test')
test_run:cmd('delete server test')

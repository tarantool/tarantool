
-- write data recover from latest snapshot

name = string.match(arg[0], "([^,]+)%.lua")
os.execute("rm -f " .. name .."/*.snap")
os.execute("rm -f " .. name .."/*.xlog")

env = require('test_run')
test_run = env.new()
test_run:cmd('restart server default')
engine = test_run:get_cfg('engine')

name = string.match(arg[0], "([^,]+)%.lua")
os.execute("touch " .. name .."/lock")

space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary')

for key = 1, 351 do space:insert({key}) end
box.snapshot()

test_run:cmd('restart server default')

name = string.match(arg[0], "([^,]+)%.lua")
os.execute("rm -f " .. name .."/lock")

space = box.space['test']
index = space.index['primary']
index:select({}, {iterator = box.index.ALL})
space:drop()

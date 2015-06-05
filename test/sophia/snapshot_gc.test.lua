name = string.match(arg[0], "([^,]+)%.lua")
os.execute("rm -f " .. name .."/*.snap")
os.execute("rm -f " .. name .."/*.xlog")
os.execute("touch " .. name .."/mt")

--# stop server default
--# start server default

space = box.schema.create_space('test', { engine = 'sophia' })
index = space:create_index('primary')

for key = 1, 351 do space:insert({key}) end
box.snapshot()
space:drop()
sophia_schedule()
sophia_dir()[1]

-- ensure that previous space has been garbage collected
space = box.schema.create_space('test', { engine = 'sophia' })
index = space:create_index('primary')
for key = 1, 351 do space:insert({key}) end
sophia_dir()[1] -- 2
box.snapshot()
space:drop()
sophia_schedule()
sophia_dir()[1] -- 1

space = box.schema.create_space('test', { engine = 'sophia' })
index = space:create_index('primary')
for key = 1, 351 do space:insert({key}) end
sophia_dir()[1] -- 2
box.snapshot()
space:drop()
sophia_schedule()
sophia_dir()[1] -- 1

name = string.match(arg[0], "([^,]+)%.lua")
os.execute("rm -f " .. name .."/*.snap")
os.execute("rm -f " .. name .."/*.xlog")
os.execute("rm -f " .. name .."/mt")
os.execute("rm -f " .. name .."/lock")

--# stop server default
--# start server default

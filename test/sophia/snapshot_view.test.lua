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
name = string.match(arg[0], "([^,]+)%.lua")
-- remove tarantool xlogs
os.execute("rm -f " .. name .."/*.xlog")
os.execute("rm -f " .. name .."/mt")
os.execute("touch " .. name .."/lock")
sophia_rmdir()

--# stop server default
--# start server default

name = string.match(arg[0], "([^,]+)%.lua")
space = box.space['test']
space:len()
sophia_dir()[1]
space:drop()
sophia_schedule()
sophia_dir()[1]

os.execute("rm -f " .. name .."/*.snap")
os.execute("rm -f " .. name .."/*.xlog")
os.execute("rm -f " .. name .."/lock")

--# stop server default
--# start server default

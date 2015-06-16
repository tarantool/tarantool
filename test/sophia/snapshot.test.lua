
-- snapshot
name = string.match(arg[0], "([^,]+)%.lua")
os.execute("rm -f " .. name .."/*.snap")
os.execute("rm -f " .. name .."/*.xlog")
os.execute("touch " .. name .."/mt")

--# stop server default
--# start server default

name = string.match(arg[0], "([^,]+)%.lua")
space = box.schema.space.create('test', { engine = 'sophia' })
index = space:create_index('primary')

for key = 1, 351 do space:insert({key}) end
box.snapshot()

os.execute("rm -f " .. name .."/mt")
os.execute("touch " .. name .."/lock")

--# stop server default
--# start server default

name = string.match(arg[0], "([^,]+)%.lua")
space = box.space['test']
t = {}
for key = 1, 351 do table.insert(t, space:get({key})) end
t
space:drop()

os.execute("rm -f " .. name .."/lock")

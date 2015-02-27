
-- snapshot

os.execute("touch mt")

--# stop server default
--# start server default

space = box.schema.space.create('test', { engine = 'sophia' })
index = space:create_index('primary')

for key = 1, 351 do space:insert({key}) end
box.snapshot()

os.execute("rm -f mt")
os.execute("touch lock")

--# stop server default
--# start server default

space = box.space['test']
t = {}
for key = 1, 351 do table.insert(t, space:get({key})) end
t
space:drop()

os.execute("rm -f lock")

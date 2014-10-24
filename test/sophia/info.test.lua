
sophia_rmdir()

box.info().sophia['sophia.version']

space = box.schema.create_space('test', { engine = 'sophia', id = 100 })
index = space:create_index('primary', { type = 'tree', parts = {1, 'num'} })

for key = 1, 10 do space:insert({key}) end

box.info().sophia['db.100.profiler.index_count']

space:drop()

sophia_rmdir()

-- gh-1671 upsert is broken in a transaction

-- upsert after upsert

space = box.schema.space.create('test', { engine = 'vinyl' })
index = space:create_index('primary')
space:insert({1, 1, 2})
space:insert({2})
space:insert({3, 4, 'abc'})
box.begin()
space:upsert({1}, {{'#', 3, 1}})
space:upsert({1}, {{'!', 2, 20}})
space:upsert({1}, {{'+', 3, 20}})
box.commit()
space:select{}

box.begin()
space:upsert({2}, {{'!', 2, 10}})
space:upsert({3, 4, 5}, {{'+', 2, 1}})
space:upsert({2, 2, 2, 2}, {{'+', 2, 10.5}})
space:upsert({3}, {{'-', 2, 2}})
box.commit()
space:select{}

space:drop()

-- upsert after replace

space = box.schema.space.create('test', { engine = 'vinyl' })
index = space:create_index('primary')
space:insert{1}
space:insert{2}

box.begin()
space:replace({3, 4})
space:upsert({3, 3, 3, 3}, {{'+', 2, 1}})
box.commit()
space:select{}

box.begin()
space:replace({2, 2})
space:upsert({2}, {{'!', 2, 1}})
space:upsert({2}, {{'!', 2, 3}})
box.commit()
space:select{}

box.begin()
space:replace({4})
space:upsert({4}, {{'!', 2, 1}})
space:replace({5})
space:upsert({4}, {{'!', 2, 3}})
space:upsert({5}, {{'!', 2, 1}, {'+', 2, 1}})
box.commit()
space:select{}

space:drop()

-- upsert after delete

space = box.schema.space.create('test', { engine = 'vinyl' })
index = space:create_index('primary')
space:insert{1}
space:insert{2}
space:insert{3}
space:insert{4}

box.begin()
space:delete({1})
space:upsert({1, 2}, {{'!', 2, 100}})
box.commit()
space:select{}

box.begin()
space:delete({2})
space:upsert({1}, {{'+', 2, 1}})
space:upsert({2, 200}, {{'!', 2, 1000}})
space:upsert({2}, {{'!', 2, 1005}})
box.commit()
space:select{}

space:drop()

-- replace after upsert

space = box.schema.space.create('test', { engine = 'vinyl' })
index = space:create_index('primary')
space:insert{1}
space:insert{2}
space:insert{3}
space:insert{4}

box.begin()
space:upsert({1, 2}, {{'!', 2, 100}})
space:replace({1, 2, 3})
box.commit()
space:select{}

box.begin()
space:upsert({2}, {{'!', 2, 2}})
space:upsert({3}, {{'!', 2, 3}})
space:replace({2, 20})
space:replace({3, 30})
box.commit()
space:select{}

space:drop()

-- delete after upsert

box.cfg{}
space = box.schema.space.create('test', { engine = 'vinyl' })
index = space:create_index('primary')
space:insert{1}
space:insert{2}
space:insert{3}
space:insert{4}

box.begin()
space:upsert({1, 2}, {{'!', 2, 100}})
space:delete({1})
box.commit()
space:select{}

box.begin()
space:upsert({5}, {{'!', 2, 100}})
space:delete({5})
box.commit()
space:select{}

box.begin()
space:upsert({5}, {{'!', 2, 100}})
space:delete({4})
space:upsert({4}, {{'!', 2, 100}})
space:delete({5})
space:upsert({4}, {{'!', 2, 105}})
box.commit()
space:select{}


space:drop()

--
-- gh-1828: Automatically convert UPSERT into REPLACE
-- gh-1826: vinyl: memory explosion on UPSERT
--

space = box.schema.space.create('test', { engine = 'vinyl' })
_ = space:create_index('primary', { type = 'tree', range_size = 250 * 1024 * 1024 } )

-- No runs
for i=1,2000 do space:upsert({0, 0}, {{'+', 2, 1}}) end
space:count() -- exploded before #1826

-- Mem has DELETE
box.snapshot()
space:delete({0})
for i=1,2000 do space:upsert({0, 0}, {{'+', 2, 1}}) end
space:count() -- exploded before #1826

-- Mem has REPLACE
box.snapshot()
space:replace({0, 0})
for i=1,2000 do space:upsert({0, 0}, {{'+', 2, 1}}) end
space:count() -- exploded before #1826

space:drop()

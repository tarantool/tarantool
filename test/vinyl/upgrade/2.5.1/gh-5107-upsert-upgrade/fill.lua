box.cfg{}

local s = box.schema.create_space('test', {engine = 'vinyl'})
s:create_index('pk')
s:insert({1, 2})
box.snapshot()
s:upsert({1, 0}, {{'+', 2, 1}})
s:upsert({1, 0}, {{'-', 2, 2}})
s:upsert({2, 0}, {{'+', 2, 1}})
s:upsert({2, 0}, {{'-', 2, 2}})
s:upsert({1, 0}, {{'=', 2, 2}})
s:upsert({1, 0}, {{'-', 2, 2}})
box.snapshot()

os.exit()

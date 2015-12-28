env = require('test_run')
test_run = env.new()
s = box.schema.create_space('tweedledum')
index = s:create_index('pk')

errinj = box.error.injection
errinj.set("ERRINJ_TUPLE_ALLOC", true)
s:upsert({111, '111', 222, '222'}, {{'!', 5, '!'}})
errinj.set("ERRINJ_TUPLE_ALLOC", false)
s:select{111}

s:drop()

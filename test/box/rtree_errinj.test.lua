errinj = require('box.error.injection')

s = box.schema.space.create('spatial')

s:create_index('primary')
s:create_index('spatial', { type = 'rtree', unique = false, parts = {2, 'array'}})

errinj.set("ERRINJ_INDEX_ALLOC", true)
s:insert{1,{0,0}}
s:insert{2,{0,10}}
s:insert{3,{0,50}}
s:insert{4,{10,0}}
s:insert{5,{50,0}}
s:insert{6,{10,10}}
s:insert{7,{10,50}}
s:insert{8,{50,10}}
s:insert{9,{50,50}}
errinj.set("ERRINJ_INDEX_ALLOC", false)

s:drop()

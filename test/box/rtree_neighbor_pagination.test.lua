s = box.schema.space.create('spatial')
_ = s:create_index('primary')
_ = s:create_index('spatial', { type = 'rtree', unique = false, parts = {2, 'array'}})

s:insert{1,{0.0,0.0}}
s:insert{2,{0.0,10.0}}
s:insert{3,{0.0,50.0}}
s:insert{4,{10.0,0.0}}
s:insert{5,{50.0,0.0}}
s:insert{6,{10.0,10.0}}
s:insert{7,{10.0,50.0}}
s:insert{8,{50.0,10.0}}
s:insert{9,{50.0,50.0}}

-- select neighbors of point (5,5)
local tuples, pos = s.index.spatial:select({5.0,5.0}, {iterator = 'NEIGHBOR', fetch_pos=true, limit = 5})
process_tuples(tuples)
-- select neighbors of point (5,5) from pos
tuples, pos = s.index.spatial:select({5.0,5.0}, {fetch_pos=true, limit=4, after=pos})
process_tuples(tuples)

s:drop()

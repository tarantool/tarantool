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

-- select all records
s.index.spatial:select({}, {iterator = 'ALL'})
-- select records belonging to rectangle (0,0,10,10)
s.index.spatial:select({0.0,0.0,10.0,10.0}, {iterator = 'LE'})
-- select records with coordinates (10,10)
s.index.spatial:select({10.0,10.0}, {iterator = 'EQ'})
-- select neighbors of point (5,5)
s.index.spatial:select({5.0,5.0}, {iterator = 'NEIGHBOR'})

s:drop()

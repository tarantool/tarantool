-- clear statistics
--# stop server default
--# start server default
box.stat.INSERT.total
box.stat.DELETE.total
box.stat.UPDATE.total
box.stat.REPLACE.total
box.stat.SELECT.total
box.stat.ERROR.total

space = box.schema.space.create('tweedledum')
index = space:create_index('primary', { type = 'hash' })

-- check stat_cleanup
-- add several tuples
for i=1,10 do space:insert{i, 'tuple'..tostring(i)} end
box.stat.INSERT.total
box.stat.DELETE.total
box.stat.UPDATE.total
box.stat.REPLACE.total
box.stat.SELECT.total

-- check exceptions
space:get('Impossible value')
box.stat.ERROR.total

--# stop server default
--# start server default

-- statistics must be zero
box.stat.INSERT.total
box.stat.DELETE.total
box.stat.UPDATE.total
box.stat.REPLACE.total
box.stat.SELECT.total
box.stat.ERROR.total

-- cleanup
box.space.tweedledum:drop()

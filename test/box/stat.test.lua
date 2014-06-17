-- clear statistics
--# stop server default
--# start server default
box.stat.INSERT.total
box.stat.DELETE.total
box.stat.UPDATE.total
box.stat.REPLACE.total
box.stat.SELECT.total

space = box.schema.create_space('tweedledum')
space:create_index('primary', { type = 'hash' })

-- check stat_cleanup
-- add several tuples
for i=1,10 do space:insert{i, 'tuple'..tostring(i)} end
box.stat.INSERT.total
box.stat.DELETE.total
box.stat.UPDATE.total
box.stat.REPLACE.total
box.stat.SELECT.total

--# stop server default
--# start server default

-- statistics must be zero
box.stat.INSERT.total
box.stat.DELETE.total
box.stat.UPDATE.total
box.stat.REPLACE.total
box.stat.SELECT.total

-- cleanup
box.space.tweedledum:drop()

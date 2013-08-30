--# create server hot_standby with configuration='replication/cfg/hot_standby.cfg', need_init=False
--# create server replica with configuration='replication/cfg/replica.cfg'
--# start server hot_standby
--# start server replica
--# setopt delimiter ';'
box.replace(box.schema.SPACE_ID, 0, 0, 'tweedledum');
box.replace(box.schema.INDEX_ID, 0, 0, 'primary', 'hash', 1, 1, 0, 'num');

for i = 1, 10 do
    box.insert(0, i, 'the tuple '..tostring(i))
end;
for i = 1, 10 do
    box.select(0, 0, i)
end;

--# set connection replica
while box.info.lsn < 11 do
    box.fiber.sleep(0.01)
end;
for i = 1, 10 do
    box.select(0, 0, i)
end;

--# stop server default
box.fiber.sleep(0.5)

--# set connection hot_standby
box.replace(box.schema.SPACE_ID, 0, 0, 'tweedledum');
box.replace(box.schema.INDEX_ID, 0, 0, 'primary', 'hash', 1, 1, 0, 'num');
for i = 11, 20 do
    box.insert(0, i, 'the tuple '..tostring(i))
end;
for i = 11, 20 do
    box.select(0, 0, i)
end;

--# set connection replica
while box.info.lsn < 21 do
    box.fiber.sleep(0.01)
end;
for i = 11, 20 do
    box.select(0, 0, i)
end;

--# stop server hot_standby
--# stop server replica
--# cleanup server hot_standby
--# cleanup server replica
--# start server default
--# set connection default
box.space[0]:drop()

--# create server hot_standby with configuration='replication/cfg/hot_standby.cfg', need_init=False
--# create server replica with configuration='replication/cfg/replica.cfg'
--# start server hot_standby
--# start server replica
box.replace(box.schema.SPACE_ID, 0, 0, 'tweedledum')
box.replace(box.schema.INDEX_ID, 0, 0, 'primary', 'hash', 1, 1, 0, 'num')

--# setopt delimiter ';'
--# set connection default, hot_standby, replica
function _insert(_begin, _end)
    a = {}
    for i = _begin, _end do
        table.insert(a, box.insert(0, i, 'the tuple '..i))
    end
    return unpack(a)
end;
function _select(_begin, _end)
    a = {}
    for i = _begin, _end do
        table.insert(a, box.select(0, 0, i))
    end
    return unpack(a)
end;
function _wait_lsn(_lsn)
    while box.info.lsn < _lsn do
        box.fiber.sleep(0.001)
    end
end;
--# setopt delimiter ''
--# set connection default
_insert(1, 10)
_select(1, 10)

--# set connection replica
_wait_lsn(13)
_select(1, 10)

--# stop server default
box.fiber.sleep(0.2)

--# set connection hot_standby
box.replace(box.schema.SPACE_ID, 0, 0, 'tweedledum')
box.replace(box.schema.INDEX_ID, 0, 0, 'primary', 'hash', 1, 1, 0, 'num')

_insert(11, 20)
_select(11, 20)

--# set connection replica
_wait_lsn(23)
_select(11, 20)

--# stop server hot_standby
--# stop server replica
--# cleanup server hot_standby
--# cleanup server replica
--# start server default
--# set connection default
box.space[0]:drop()

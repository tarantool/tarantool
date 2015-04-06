--# set connection default
box.schema.user.grant('guest', 'replication')
box.schema.func.create('_set_pri_lsn')
box.schema.user.grant('guest', 'execute', 'function', '_set_pri_lsn')
--# create server hot_standby with script='replication/hot_standby.lua', rpl_master=default
--# create server replica with rpl_master=default, script='replication/replica.lua'
--# start server hot_standby
--# start server replica
--# set connection default

--# setopt delimiter ';'
--# set connection default, hot_standby, replica
fiber = require('fiber');
while box.info.server.id == 0 do fiber.sleep(0.01) end;
while box.space['_priv']:len() < 1 do fiber.sleep(0.001) end;
do
    local pri_id = ''
    local begin_lsn = 0

    function _set_pri_lsn(_id, _lsn)
        pri_id = _id
        begin_lsn = _lsn
    end

    function _get_pri_lsn()
        return box.info.vclock[pri_id]
    end

    function _print_lsn()
        return (_get_pri_lsn() - begin_lsn + 1)
    end

    function _insert(_begin, _end)
        local a = {}
        for i = _begin, _end do
            table.insert(a, box.space.tweedledum:insert{i, 'the tuple '..i})
        end
        return a
    end

    function _select(_begin, _end)
        local a = {}
        for i = _begin, _end do
            local tuple = box.space.tweedledum:get{i}
            if tuple ~= nil then
                table.insert(a, tuple)
            end
        end
        return a
    end

    function _wait_lsn(_lsnd)
        while _get_pri_lsn() < _lsnd + begin_lsn do
            fiber.sleep(0.001)
        end
        begin_lsn = begin_lsn + _lsnd
    end
end;
--# setopt delimiter ''
--# set connection hot_standby
box.info.status
--# set connection default
box.info.status

space = box.schema.space.create('tweedledum')
index = space:create_index('primary', { type = 'hash' })

-- set begin lsn on master, replica and hot_standby.
--# set variable replica_port to 'replica.listen'
REPLICA = require('uri').parse(tostring(replica_port))
REPLICA ~= nil
a = (require 'net.box'):new(REPLICA.host, REPLICA.service)
a:call('_set_pri_lsn', box.info.server.id, box.info.server.lsn)
a:close()

_insert(1, 10)
_select(1, 10)

--# set connection replica
_wait_lsn(10)
_select(1, 10)

--# stop server default
--# set connection hot_standby
while box.info.status ~= 'running' do fiber.sleep(0.001) end
--# set connection replica

-- hot_standby.listen is garbage, since hot_standby.lua
-- uses MASTER environment variable for its listen
--# set variable hot_standby_port to 'hot_standby.master'
HOT_STANDBY = require('uri').parse(tostring(hot_standby_port))
HOT_STANDBY ~= nil
a = (require 'net.box'):new(HOT_STANDBY.host, HOT_STANDBY.service)
a:call('_set_pri_lsn', box.info.server.id, box.info.server.lsn)
a:close()

--# set connection hot_standby
_insert(11, 20)
_select(11, 20)

--# set connection replica
_wait_lsn(10)
_select(11, 20)

--# stop server hot_standby
--# stop server replica
--# cleanup server hot_standby
--# cleanup server replica
--# deploy server default
--# start server default
--# set connection default

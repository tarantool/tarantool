env = require('test_run')
test_run = env.new()
engine = test_run:get_cfg('engine')

box.schema.user.grant('guest', 'replication')
box.schema.func.create('_set_pri_lsn')
box.schema.user.grant('guest', 'execute', 'function', '_set_pri_lsn')
test_run:cmd("create server hot_standby with script='replication/hot_standby.lua', rpl_master=default")
test_run:cmd("create server replica with rpl_master=default, script='replication/replica.lua'")
test_run:cmd("start server hot_standby")
test_run:cmd("start server replica")

test_run:cmd("setopt delimiter ';'")
test_run:cmd("set connection default, hot_standby, replica")
fiber = require('fiber');
while box.info.id == 0 do fiber.sleep(0.01) end;
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
test_run:cmd("setopt delimiter ''");

test_run:cmd("switch replica")
fiber = require('fiber')
test_run:cmd("switch hot_standby")
fiber = require('fiber')
box.info.status
test_run:cmd("switch default")
fiber = require('fiber')
box.info.status

space = box.schema.space.create('tweedledum', {engine = engine})
index = space:create_index('primary', {type = 'tree'})
index = space:create_index('secondary', {type = 'tree'})

-- set begin lsn on master, replica and hot_standby.
test_run:cmd("set variable replica_port to 'replica.listen'")
REPLICA = require('uri').parse(tostring(replica_port))
REPLICA ~= nil
a = (require 'net.box').connect(REPLICA.host, REPLICA.service)
a:call('_set_pri_lsn', {box.info.id, box.info.lsn})
a:close()

test_run:cmd("switch hot_standby")
test_run:wait_lsn("hot_standby", "default")
_set_pri_lsn(box.info.id, box.info.lsn)
_print_lsn()

test_run:cmd("switch default")
_insert(1, 10)
_select(1, 10)

-- Check box.info.vclock is updated during hot standby.
test_run:cmd("switch hot_standby")
_wait_lsn(10)
box.space.tweedledum.index[1]:select()

test_run:cmd("switch replica")
_wait_lsn(10)
_select(1, 10)

test_run:cmd("stop server default")
test_run:cmd("switch hot_standby")
while box.info.status ~= 'running' do fiber.sleep(0.001) end
test_run:cmd("switch replica")

-- hot_standby.listen is garbage, since hot_standby.lua
-- uses MASTER environment variable for its listen
test_run:cmd("set variable hot_standby_port to 'hot_standby.master'")
HOT_STANDBY = require('uri').parse(tostring(hot_standby_port))
HOT_STANDBY ~= nil
a = (require 'net.box').connect(HOT_STANDBY.host, HOT_STANDBY.service)
a:call('_set_pri_lsn', {box.info.id, box.info.lsn})
a:close()

test_run:cmd("switch hot_standby")
_insert(11, 20)
_select(11, 20)

test_run:cmd("switch replica")
_wait_lsn(10)
_select(11, 20)

test_run:cmd("stop server hot_standby")
test_run:cmd("cleanup server hot_standby")
test_run:cmd("delete server hot_standby")
test_run:cmd("deploy server default")
test_run:cmd("start server default")
test_run:cmd("switch default")
test_run:cmd("stop server replica")
test_run:cmd("cleanup server replica")
test_run:cmd("delete server replica")
test_run:cleanup_cluster()

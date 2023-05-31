local t = require('luatest')
local cluster = require('luatest.replica_set')
local server = require('luatest.server')
local proxy = require('luatest.replica_proxy')
local fiber = require('fiber')
local fio = require('fio')

local g = t.group('linearizable-read')

local function build_replication(num_instances)
    local t = {}
    for i = 1, num_instances do
        table.insert(t, server.build_listen_uri('server_' .. i .. '_proxy'))
    end
    return t
end

local num_servers = 3

g.before_all(function(cg)
    cg.cluster = cluster:new({})
    cg.servers = {}
    cg.box_cfg = {
        replication = build_replication(num_servers),
        replication_timeout = 0.1,
        memtx_use_mvcc_engine = true,
    }
    cg.servers[1] = cg.cluster:build_and_add_server({
        alias = 'server_1',
        box_cfg = cg.box_cfg,
    })
    -- Servers 2 and 3 are interconnected without a proxy.
    for i = 2, num_servers do
        cg.box_cfg.replication[i] = server.build_listen_uri('server_' .. i,
            cg.cluster.id)
    end
    for i = 2, num_servers do
        cg.servers[i] = cg.cluster:build_and_add_server({
            alias = 'server_' .. i,
            box_cfg = cg.box_cfg,
        })
    end
    cg.proxies = {}
    for i = 1, num_servers do
        cg.proxies[i] = proxy:new({
            client_socket_path = server.build_listen_uri('server_' .. i ..
                '_proxy'),
            server_socket_path = server.build_listen_uri('server_' .. i,
                cg.cluster.id),
        })
        cg.proxies[i]:start({force = true})
    end
    cg.cluster:start()
    cg.cluster:wait_for_fullmesh()
    cg.servers[1]:exec(function()
        box.schema.space.create('sync', {is_sync=true})
        box.space.sync:create_index('pk')
    end)
    cg.servers[2]:wait_for_vclock_of(cg.servers[1])
    cg.servers[3]:wait_for_vclock_of(cg.servers[1])
end)

g.after_all(function(cg)
    cg.cluster:drop()
end)

g.test_wait_others = function(cg)
    cg.proxies[1]:pause()
    local fid = cg.servers[1]:exec(function()
        local fiber = require('fiber')
        local f = fiber.new(function()
            return pcall(box.begin, {txn_isolation = 'linearizable'})
        end)
        f:set_joinable(true)
        fiber.sleep(0.01)
        t.assert_equals(f:status(), 'suspended', 'begin() waits for replicas')
        return f:id()
    end)
    cg.proxies[1]:resume()
    local ok = cg.servers[1]:exec(function(fid)
        local fiber = require('fiber')
        local _, ok, err =  fiber.join(fiber.find(fid))
        return ok, err
    end, {fid})
    t.assert(ok, 'begin() succeeds after replica connections are established')
end

g.test_timeout = function(cg)
    t.tarantool.skip_if_not_debug()
    cg.servers[1]:exec(function()
        box.error.injection.set('ERRINJ_RELAY_FROM_TX_DELAY', true)
        local ok, err = pcall(box.begin, {txn_isolation = 'linearizable',
                                          timeout = 0.01})
        box.error.injection.set('ERRINJ_RELAY_FROM_TX_DELAY', false)
        t.assert(not ok, 'Error when relay is unresponsive')
        t.assert_equals(err.type, 'TimedOut',
                        'Timeout when relay is unresponsive')
    end)
end

g.before_test('test_no_dirty_reads', function(cg)
    -- Make cluster size 5. Having quorum 3 out of 5 will make the instance
    -- poll 5 - 3 + 1 == all 3 of the "real" nodes.
    cg.servers[1]:exec(function()
        local uuid = require('uuid')
        box.space._cluster:insert{4, tostring(uuid.new())}
        box.space._cluster:insert{5, tostring(uuid.new())}
    end)
    cg.servers[2]:wait_for_vclock_of(cg.servers[1])
    cg.servers[2]:exec(function()
        box.ctl.promote()
    end)
    cg.servers[1]:wait_for_vclock_of(cg.servers[2])
    cg.servers[3]:wait_for_vclock_of(cg.servers[2])
end)

g.after_test('test_no_dirty_reads', function(cg)
    cg.servers[2]:exec(function()
        box.ctl.demote()
    end)
    cg.servers[1]:wait_for_vclock_of(cg.servers[2])
    cg.servers[1]:exec(function()
        box.space._cluster:delete{4}
        box.space._cluster:delete{5}
    end)
    cg.servers[2]:wait_for_vclock_of(cg.servers[1])
    cg.servers[3]:wait_for_vclock_of(cg.servers[1])
end)

g.test_no_dirty_reads = function(cg)
    local quorum = cg.servers[2]:exec(function()
        local lsn = box.info.lsn
        local q = box.cfg.replication_synchro_quorum
        box.cfg{replication_synchro_quorum = 4}
        require('fiber').new(box.space.sync.insert, box.space.sync, {1})
        t.helpers.retrying({}, function() assert(box.info.lsn > lsn) end)
        return q
    end)
    cg.servers[1]:exec(function()
        local ok, err = pcall(box.begin, {txn_isolation = 'linearizable',
                                          timeout = 0.1})
        t.assert(not ok, 'Error with unconfirmed transaction')
        t.assert_equals(err.type, 'TimedOut', 'Timeout waiting for confirm')
        t.assert_equals(box.info.synchro.queue.len, 1,
                        'Pending transaction is seen')
        t.assert_equals(box.space.sync:get{1}, nil, 'No dirty reads')
    end)
    cg.servers[2]:exec(function(q)
        box.cfg{replication_synchro_quorum = q}
    end, {quorum})
    cg.servers[1]:exec(function()
        box.begin{txn_isolation = 'linearizable'}
        local len = box.info.synchro.queue.len
        local get = box.space.sync:get{1}
        box.commit()
        -- If the assertion fails before box.commit(), ER_FUNCTION_TX_ACTIVE
        -- shadows the real error. So save the values inside the txn and check
        -- them outside.
        t.assert_equals(len, 0, 'Waited for confirm')
        t.assert_equals(get, {1}, 'Confirmed data is seen')
    end)
end

g.test_reconnect_while_waiting = function(cg)
    for i = 2, num_servers do
        cg.servers[i]:exec(function()
            local repl = table.copy(box.cfg.replication)
            table.remove(repl, 1)
            box.cfg{replication = repl}
        end)
    end
    local f = fiber.new(cg.servers[1].exec, cg.servers[1], function()
        local ok, err = pcall(box.begin, {txn_isolation = 'linearizable'})
        if ok then
            box.commit()
        end
        return ok, err
    end)
    f:set_joinable(true)
    for i = 2, num_servers do
        cg.servers[i]:exec(function(repl)
            box.cfg{replication = repl}
        end, {cg.box_cfg.replication})
    end
    local success, ok, _ = f:join()
    t.assert(success, 'fiber joined successfully')
    t.assert(ok, 'reconnect while begin() waits for vclocks is tolerated')
end

g.test_leader_change = function(cg)
    cg.servers[1]:exec(function()
        box.ctl.promote()
    end)
    cg.servers[2]:wait_for_vclock_of(cg.servers[1])
    cg.servers[3]:wait_for_vclock_of(cg.servers[1])
    t.helpers.retrying({}, function()
        cg.servers[2]:assert_follows_upstream(cg.servers[1]:get_instance_id())
        cg.servers[2]:assert_follows_upstream(cg.servers[3]:get_instance_id())
    end)
    for i = 1, num_servers do
        cg.proxies[i]:pause()
    end
    cg.servers[2]:exec(function()
        box.ctl.promote()
        box.space.sync:insert{2}
    end)
    local fid = cg.servers[1]:exec(function()
        local f = require('fiber').new(function()
            box.begin{txn_isolation = 'linearizable'}
            local owner = box.info.synchro.queue.owner
            local get = box.space.sync:get{2}
            box.commit()
            return owner, get
        end)
        f:set_joinable(true)
        t.assert_equals(f:status(), 'suspended', 'begin waits for others')
        return f:id()
    end)
    for i = 1, num_servers do
        cg.proxies[i]:resume()
    end
    local owner, get = cg.servers[1]:exec(function(fid)
        local fiber = require('fiber')
        local _, owner, get = fiber.join(fiber.find(fid))
        return owner, get
    end, {fid})
    t.assert_equals(owner, cg.servers[2]:get_instance_id(),
                    'leader change is noticed in box.begin()')
    t.assert_equals(get, {2}, 'Transaction committed by new leader is seen')
end

local g_basic = t.group('linearizable-read-basic')

g_basic.before_all(function(cg)
    cg.cluster = cluster:new({})
    cg.nomvcc = cg.cluster:build_and_add_server({alias = 'nomvcc'})
    cg.mvcc = cg.cluster:build_and_add_server({
        alias = 'mvcc',
        box_cfg = {
            memtx_use_mvcc_engine = true,
        },
    })
    cg.cluster:start()
    cg.mvcc:exec(function()
        box.schema.space.create('async'):create_index('pk')
        box.schema.space.create('sync', {is_sync = true}):create_index('pk')
        box.schema.space.create('local', {is_local = true}):create_index('pk')
        box.schema.space.create('temporary', {temporary = true})
        box.space.temporary:create_index('pk')
        box.schema.space.create('vinyl', {engine = 'vinyl', is_sync = true})
        box.space.vinyl:create_index('pk')
        -- For the sake of writes to the sync space.
        box.ctl.promote()
    end)
end)

g_basic.after_all(function(cg)
    cg.cluster:drop()
end)

local function begin(opts)
    return pcall(box.begin, opts)
end

local function linearizable_read_from_space(spacename)
    box.begin{txn_isolation = 'linearizable'}
    local ok, err = pcall(box.space[spacename].select, box.space[spacename])
    box.commit()
    return ok, err
end

local function linearizable_write_to_space(spacename)
    box.begin{txn_isolation = 'linearizable'}
    local ok, err = pcall(box.space[spacename].replace,
                          box.space[spacename], {1})
    box.commit()
    return ok, err
end

local function test_linearizable_access(node, spacename, is_ok, msg)
    local ok, err = node:exec(linearizable_read_from_space, {spacename})
    t.assert_equals(ok, is_ok, 'Expected result with read from ' .. spacename)
    if not ok then
        t.assert_equals(err.message, msg, 'Expected error message with ' ..
                                          'read from ' .. spacename)
    end
    ok, err = node:exec(linearizable_write_to_space, {spacename})
    t.assert_equals(ok, is_ok, 'Expected result with write to ' .. spacename)
    if not ok then
        t.assert_equals(err.message, msg, 'Expected error message with ' ..
                                          'write to ' .. spacename)
    end
end

local set_log_before_cfg = [[
    local logfile = require('fio').pathjoin(
        os.getenv('TARANTOOL_WORKDIR'),
        os.getenv('TARANTOOL_ALIAS') .. '.log'
    )
    require('log').cfg{log = logfile}
]]

g_basic.test_cfg_failure = function(cg)
    local cfg_failure = cg.cluster:build_and_add_server{
        alias = 'cfg_failure',
        box_cfg = {txn_isolation = 'linearizable'},
        env = {['TARANTOOL_RUN_BEFORE_BOX_CFG'] = set_log_before_cfg},
    }
    -- The server will be dropped by after_all.
    cfg_failure:start{wait_until_ready = false}
    local logfile = fio.pathjoin(cfg_failure.workdir, 'cfg_failure.log')
    local err = "Incorrect value for option 'txn_isolation': cannot set " ..
                "default transaction isolation to 'linearizable'"
    t.helpers.retrying({}, function()
        t.assert(cfg_failure:grep_log(err, nil, {filename = logfile}),
                 'Cannot set default isolation level to linearizable')
        t.assert(cfg_failure:grep_log('fatal error, exiting the event loop',
                                      nil, {filename = logfile}),
                 'Fatal error')
    end)
end

g_basic.test_basic = function(cg)
    local ok, err = cg.nomvcc:exec(begin, {{txn_isolation = 'linearizable'}})
    t.assert(not ok, 'Error without mvcc enabled')
    t.assert_equals(err.message, 'Linearizable transaction does not support ' ..
                                 'disabled memtx mvcc engine',
                    'Correct error without mvcc enabled')

    local errmsg = 'space "async" does not support linearizable operations'
    test_linearizable_access(cg.mvcc, 'async', false, errmsg)
    errmsg = 'space "vinyl" does not support linearizable operations'
    test_linearizable_access(cg.mvcc, 'vinyl', false, errmsg)
    test_linearizable_access(cg.mvcc, 'temporary', true)
    test_linearizable_access(cg.mvcc, 'local', true)
    test_linearizable_access(cg.mvcc, 'sync', true)
end

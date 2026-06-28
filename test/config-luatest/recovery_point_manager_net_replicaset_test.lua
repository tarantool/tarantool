local t = require('luatest')
local cbuilder = require('luatest.cbuilder')
local cluster = require('luatest.cluster')
local treegen = require('luatest.treegen')
local justrun = require('luatest.justrun')

local g = t.group()

local REDUNDANCY = 3
local BACKEND = 'roles.recovery-point-manager.backends.net-replicaset'

-- A cluster with a router and a 3-node storage replicaset (the backup target).
-- The `backup` user is the dedicated identity the backend connects as: it holds
-- only the lua_call privilege on box.backup.recovery_point.create.
local function test_config()
    local sharding_role = {
        privileges = {{permissions = {'execute'}, universe = true}},
    }
    local builder = cbuilder:new()
    :set_global_option('credentials.roles.sharding', sharding_role)
    :set_global_option('credentials.users.storage.roles', {'sharding'})
    :set_global_option('credentials.users.storage.password', 'secret_storage')
    :set_global_option('credentials.users.backup.password', 'secret_backup')
    :set_global_option('credentials.users.backup.privileges', {{
        permissions = {'execute'},
        lua_call = {'box.backup.recovery_point.create'},
    }})
    :set_global_option('iproto.advertise.sharding.login', 'storage')
    :use_group('test')
    builder:use_replicaset('router'):add_instance('router', {})
    builder:use_replicaset('storage')
           :set_replicaset_option('replication.failover', 'manual')
           :set_replicaset_option('leader', 'storage1')
    for i = 1, REDUNDANCY do
        builder:add_instance('storage' .. i, {})
    end
    return builder:config()
end

-- The cluster is shared across the whole group: tests only observe it (open
-- client connections, create recovery points) and never change its topology.
g.before_all(function()
    g.cluster = cluster:new(test_config(), nil, {auto_cleanup = false})
    g.cluster:start()
end)

g.after_all(function()
    if g.cluster ~= nil then
        g.cluster:drop()
        g.cluster = nil
    end
end)

-- Find a recovery point's row on the storage leader by its exact (replica_id,
-- lsn). The returned timestamp is a lossy seconds double, not the raw
-- nanosecond primary key, so it cannot be used for a by-key lookup.
local function find_point(point)
    return g.cluster.storage1:exec(function(point)
        for _, row in box.space._recovery_point:pairs() do
            if row.replica_id == point.replica_id and
               row.lsn == point.lsn then
                return row:tomap({names_only = true})
            end
        end
    end, {point})
end

-- A plain, imperative box.cfg{} turns box.cfg into a table, but the declarative
-- config (which new() resolves its target through) stays uninitialized. The
-- backend must still forbid new().
g.test_new_without_declarative_config = function()
    local dir = treegen.prepare_directory({}, {})
    local script = ([[
        local t = require('luatest')
        box.cfg{}
        t.assert_equals(box.info.config.status, 'uninitialized')
        local backend = require(%q)
        t.assert_error_msg_contains('config is uninitialized', backend.new,
                                    {target = 'storage'})
        os.exit(0)
    ]]):format(BACKEND)
    treegen.write_file(dir, 'no_declarative_config.lua', script)
    local res = justrun.tarantool(dir, {}, {'no_declarative_config.lua'},
                                  {nojson = true, stderr = true})
    t.assert_equals(res.exit_code, 0, res.stderr)
end

g.test_validate = function()
    g.cluster.router:exec(function(backend_name)
        local backend = require(backend_name)
        -- A good cfg validates.
        backend.config.validate({target = 'storage', login = 'backup'})
        -- target is mandatory.
        t.assert_error_msg_contains('target is mandatory', function()
            backend.config.validate({login = 'backup'})
        end)
        -- A password requires a login.
        t.assert_error_msg_contains(
            'Password cannot be set without setting login', function()
                backend.config.validate({target = 'storage', password = 'p'})
            end)
        -- transport is a plain/ssl enum, available on any edition.
        backend.config.validate({target = 'storage',
                                 params = {transport = 'ssl'}})
        t.assert_error_msg_contains('Got dtls', function()
            backend.config.validate({target = 'storage',
                                     params = {transport = 'dtls'}})
        end)
    end, {BACKEND})
end

g.test_ssl_params_are_enterprise_only = function()
    t.tarantool.skip_if_enterprise()
    g.cluster.router:exec(function(backend_name)
        local backend = require(backend_name)
        t.assert_error_msg_contains(
            'available only in Tarantool Enterprise Edition', function()
                backend.config.validate({target = 'storage',
                                         params = {ssl_key_file = '/k.pem'}})
            end)
    end, {BACKEND})
end

g.test_create_point = function()
    local point = g.cluster.router:exec(function(backend_name)
        local backend = require(backend_name)
        local inst = backend.new({target = 'storage', login = 'backup'})

        -- create_point returns the created point, tagged with the id as label.
        local point, err = inst:create_point({label = 'rpm.test.point',
                                              timeout = 5})
        t.assert_equals(err, nil)
        t.assert_equals(point.label, 'rpm.test.point')
        t.assert_not_equals(point.lsn, nil)

        -- info() is the replicaset info tagged with the backend type.
        local info = inst:info()
        t.assert_equals(info.backend_type, 'net-replicaset')
        t.assert_equals(info.replicaset, 'storage')
        t.assert_not_equals(info.leader, nil)
        t.assert_equals(info.instances[info.leader].status, 'rw')
        t.assert_type(info.alerts, 'table')

        -- drop() closes the connection; a later create_point fails gracefully
        -- (nil + err, not a raise).
        inst:drop()
        local point2, err2 = inst:create_point({label = 'rpm.test.point2',
                                                timeout = 1})
        t.assert_equals(point2, nil)
        t.assert_not_equals(err2, nil)
        return point
    end, {BACKEND})

    -- The point landed on the storage leader; check the label was stored.
    local row = find_point(point)
    t.assert_not_equals(row, nil)
    t.assert_equals(row.opts.label, 'rpm.test.point')
end

-- End-to-end: a manager driving the net-replicaset backend creates points on
-- the target leader periodically.
g.test_manager = function()
    -- The manager runs on the router and connects to the storage replicaset as
    -- the `backup` user.
    local last_point = g.cluster.router:exec(function(backend_name)
        local rp = box.backup.recovery_point
        local backend = require(backend_name)
        local m = rp.manager_create('rpm', {
            backend = backend,
            backend_cfg = {target = 'storage', login = 'backup'},
            create_interval = 0.1,
            timeout = 5,
        })
        t.assert_equals(rp.managers['rpm'], m)

        -- The loop periodically creates a point on the storage leader.
        local first_point
        t.helpers.retrying({timeout = 60}, function()
            first_point = m:info().last_point
            t.assert_not_equals(first_point, nil)
        end)
        t.helpers.retrying({timeout = 60}, function()
            t.assert_gt(m:info().last_point.lsn, first_point.lsn)
        end)

        local info = m:info()
        -- info().backend is the net-replicaset backend's info; the healthy
        -- target raises no alerts.
        t.assert_equals(info.backend.backend_type, 'net-replicaset')
        t.assert_equals(info.backend.replicaset, 'storage')
        t.assert_equals(next(info.alerts), nil)
        -- last_point is a well-formed recovery point.
        t.assert_type(info.last_point.label, 'string')
        t.assert_not_equals(info.last_point.lsn, nil)

        m:drop()
        t.assert_equals(rp.managers['rpm'], nil)
        return info.last_point
    end, {BACKEND})

    -- The manager's last point landed on the storage leader.
    t.assert_not_equals(find_point(last_point), nil)
end

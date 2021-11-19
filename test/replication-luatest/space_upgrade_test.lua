local t = require('luatest')
local cluster = require('test.luatest_helpers.cluster')
local helpers = require('test.luatest_helpers')

local function make_create_cluster(g) return function()
    g.cluster = cluster:new({})
    local master_uri = helpers.instance_uri('master')
    local ro_replica_uri = helpers.instance_uri('ro_replica')
    local rw_replica_uri = helpers.instance_uri('rw_replica')
    local replication = {master_uri, ro_replica_uri, rw_replica_uri}
    local box_cfg = {
        listen = master_uri,
        replication = replication,
        replication_timeout = 1,
    }
    g.master = g.cluster:build_server({alias = 'master', box_cfg = box_cfg})

    box_cfg.listen = rw_replica_uri
    g.rw_replica = g.cluster:build_server({alias = 'rw_replica', box_cfg = box_cfg})

    box_cfg.read_only = true
    box_cfg.listen = ro_replica_uri
    g.ro_replica = g.cluster:build_server({alias = 'ro_replica', box_cfg = box_cfg})

    g.cluster:add_server(g.master)
    g.cluster:add_server(g.ro_replica)
    g.cluster:add_server(g.rw_replica)
    g.cluster:start()
end end

local function make_destroy_cluster(g) return function()
    g.cluster:drop()
end end

local g = t.group('space-upgrade')

g.before_all(make_create_cluster(g))
g.after_all(make_destroy_cluster(g))

local function check_upgrade_status(instance, status)
    if type(status) == "string" then
        t.assert_equals(instance:eval("return box.space._space_upgrade:select()[1][2]"), status)
    else
        t.assert_equals(instance:eval("return box.space._space_upgrade:select()"), {})
    end
end

-- Check that replica waits for master and is currently in-progress and
-- in read-only mode.
--
local function check_replica_during_upgrade(replica)
    -- Check upgrade and RO status.
    check_upgrade_status(replica, "inprogress")
    t.assert_equals(replica:eval("return box.cfg.read_only"), true)
    -- Check that only half of data has been upgraded.
    -- upd: since now iterators always return updated data.
    t.assert_equals(replica:eval("return box.space.test.index[0]:get({15})[2]"), "15")
    t.assert_equals(replica:eval("return box.space.test.index[0]:get({5})[2]"), "5")
    -- Verify that data manipilations are now allowed due to RO mode.
    replica:eval("_, err = pcall(box.space.test.replace, box.space.test, {100, 100})")
    local err_str = "Can't modify data on a read-only instance - box.cfg.read_only is true"
    t.assert_equals(replica:eval("return tostring(err)"), err_str)
    replica:eval("_, err = pcall(box.space.test.replace, box.space.test, {100, '100'})")
    t.assert_equals(replica:eval("return tostring(err)"), err_str)
end

local function check_replica_after_upgrade(replica, is_ro)
    -- Check upgrade and RO status.
    check_upgrade_status(replica, {})
    t.assert_equals(replica:eval("return box.cfg.read_only"), is_ro)
    -- Check that all data has been upgraded.
    t.assert_equals(replica:eval("return box.space.test.index[0]:get({20})[2]"), "20")
    replica:eval("res, err = pcall(box.space.test.replace, box.space.test, {100, 100})")
    if is_ro then
        t.assert_equals(replica:eval("return tostring(err)"),
                        "Can't modify data on a read-only instance - box.cfg.read_only is true")
    else
        t.assert_equals(replica:eval("return tostring(err)"),  "Tuple field 2 (y) type does not match one required "..
                                                               "by operation: expected string, got unsigned")
    end

    replica:eval("res, err = pcall(box.space.test.replace, box.space.test, {100, 'asd'})")
    if is_ro then
        t.assert_equals(replica:eval("return tostring(err)"), "Can't modify data on a read-only instance - "..
                                                              "box.cfg.read_only is true")
    else
        t.assert_equals(replica:eval("return res"), true)
    end
end

g.test_space_upgrade = function(cg)
    cg.master:eval("s = box.schema.space.create('test')")
    cg.master:eval("s:create_index('pk')")
    cg.master:eval("s:format({{'f1', 'unsigned'}, {'f2', 'unsigned'}})")
    cg.master:eval("for i = 1, 20 do s:replace({i, i}) end")
    cg.master:eval("box.error.injection.set('ERRINJ_SPACE_UPGRADE_DELAY', true)")
    cg.master:eval("body   = [[\
        function(tuple)\
            local new_tuple = {}\
            new_tuple[1] = tuple[1]\
            new_tuple[2] = tostring(tuple[2])\
            return new_tuple\
        end\
    ]];")
    cg.master:eval("box.schema.func.create('upgrade_func', "..
                                           "{body = body, is_deterministic = true, is_sandboxed = true})")
    cg.master:eval("new_format = {{name='x', type='unsigned'}, {name='y', type='string'}}")
    cg.master:eval("f = s:upgrade({mode = 'notest', func = 'upgrade_func', format = new_format, background = true})")
    cg.master:eval("f:set_joinable(true)")
    check_upgrade_status(cg.master, "inprogress")

    check_replica_during_upgrade(cg.ro_replica)
    check_replica_during_upgrade(cg.rw_replica)

    cg.master:eval("box.error.injection.set('ERRINJ_SPACE_UPGRADE_DELAY', false)")
    cg.master:eval("f:join()")
    check_upgrade_status(cg.master, {})
    check_replica_after_upgrade(cg.ro_replica, true)
    check_replica_after_upgrade(cg.rw_replica, false)
end

local fio = require('fio')
local t = require('luatest')
local cbuilder = require('luatest.cbuilder')
local cluster = require('test.config-luatest.cluster')
local server = require('luatest.server')
local it = require('test.interactive_tarantool')

local g = t.group()

g.before_all(cluster.init)
g.after_each(cluster.drop)
g.after_all(cluster.clean)

g.after_each(function(g)
    if g.server ~= nil then
        g.server:stop()
        g.server = nil
    end
    if g.it ~= nil then
        g.it:close()
        g.it = nil
    end
end)

local function assert_hierarchy(server, exp)
    server:exec(function(exp)
        local config = require('config')

        t.assert_equals(config:info().hierarchy, exp)
        t.assert_equals(config:info('v1').hierarchy, exp)
        t.assert_equals(config:info('v2').hierarchy, exp)
        t.assert_equals(box.info.config.hierarchy, exp)
    end, {exp})
end

-- Verify that the instance/replicaset/group names are shown in
-- config:info() if tarantool is started from a declarative
-- configuration.
g.test_basic = function(g)
    local config = cbuilder:new()
        :use_group('g-001')
        :use_replicaset('r-001')
        :add_instance('i-001', {})
        :config()

    local cluster = cluster.new(g, config)
    cluster:start()

    assert_hierarchy(cluster['i-001'], {
        group = 'g-001',
        replicaset = 'r-001',
        instance = 'i-001',
    })
end

-- Verify that the reported hierarchy is actually corresponds to
-- the given instance, not some other one.
g.test_many = function(g)
    local config = cbuilder:new()
        :set_global_option('replication.failover', 'manual')

        :use_group('g1')

        :use_replicaset('g1-r1')
        :set_replicaset_option('leader', 'g1-r1-i1')
        :add_instance('g1-r1-i1', {})
        :add_instance('g1-r1-i2', {})

        :use_replicaset('g1-r2')
        :set_replicaset_option('leader', 'g1-r2-i1')
        :add_instance('g1-r2-i1', {})
        :add_instance('g1-r2-i2', {})

        :use_group('g2')

        :use_replicaset('g2-r1')
        :set_replicaset_option('leader', 'g2-r1-i1')
        :add_instance('g2-r1-i1', {})
        :add_instance('g2-r1-i2', {})

        :use_replicaset('g2-r2')
        :set_replicaset_option('leader', 'g2-r2-i1')
        :add_instance('g2-r2-i1', {})
        :add_instance('g2-r2-i2', {})

        :config()

    local cluster = cluster.new(g, config)
    cluster:start()

    for _, instance in ipairs({
        'g1-r1-i1',
        'g1-r1-i2',
        'g1-r2-i1',
        'g1-r2-i2',
        'g2-r1-i1',
        'g2-r1-i2',
        'g2-r2-i1',
        'g2-r2-i2',
    }) do
        assert_hierarchy(cluster[instance], {
            group = instance:match('(g[12])%-r[12]%-i[12]'),
            replicaset = instance:match('(g[12]%-r[12])%-i[12]'),
            instance = instance,
        })
    end
end

-- Like test_basic, but specifically targets a situation, when the
-- persistent names are not shown, only the configured ones.
g.test_2_11_1 = function(g)
    local config = cbuilder:new()
        :use_group('g-001')
        :use_replicaset('r-001')
        :add_instance('i-001', {
            -- We have to set the UUIDs, because a safety check
            -- requires to verify the configuration <-> database
            -- correspondence either by names or by UUIDs, and
            -- there are no names in the given snapshot.
            database = {
                instance_uuid = '0679afea-8275-4028-bf7a-a83adc18b844',
                replicaset_uuid = '02140ab2-0d0e-421e-832e-d79024392a24',
            },
        })
        :config()

    -- Copy the 2.11.1 snapshot.
    --
    -- Can't use the `datadir` luatest.server option, because its
    -- content is copied to the `workdir` directory, while the
    -- cluster helper starts servers in a temporary directory
    -- using the `chdir` option instead of `workdir`.
    local snap = 'test/box-luatest/upgrade/2.11.1/00000000000000000000.snap'
    local cluster = cluster.new(g, config)
    local data_dir = fio.pathjoin(cluster._dir, 'var/lib/i-001')
    fio.mktree(data_dir)
    assert(fio.copyfile(snap, data_dir))

    -- Add permissions (won't needed after gh-10539).
    --
    -- Otherwise it is not possible to connect to the instance
    -- from the test and the ready check fails on the startup.
    g.it = it.new()
    g.it:roundtrip("fio = require('fio')")
    g.it:roundtrip(('fio.chdir(%q)'):format(data_dir))
    g.it:roundtrip('box.cfg()')
    g.it:roundtrip("box.schema.user.create('client', {password = 'secret'})")
    g.it:roundtrip("box.schema.user.grant('client', 'super')")
    g.it:close()
    g.it = nil

    -- Start with 2.11.1 snapshot.
    cluster:start()

    -- Verify a test prerequisite: the names are not written to
    -- the database.
    cluster['i-001']:exec(function()
        t.assert_equals(box.info.name, box.NULL)
        t.assert_equals(box.info.replicaset.name, box.NULL)
    end)

    -- Verify that the configured names are shown.
    --
    -- All the previous steps are setup and we do it just to call
    -- this little function!
    assert_hierarchy(cluster['i-001'], {
        group = 'g-001',
        replicaset = 'r-001',
        instance = 'i-001',
    })
end

-- Verify that there are no instance/replicaset/group names in
-- config:info() if tarantool is started from a Lua script,
-- without the declarative configuration.
g.test_script = function(g)
    g.server = server:new({alias = 'classic_server'})
    g.server:start()
    assert_hierarchy(g.server, {})
end

-- Same as previous, but for tarantool started in the interactive
-- mode, without a YAML config or a Lua script.
g.test_interactive = function(g)
    g.it = it.new()
    g.it:roundtrip("config = require('config')")

    t.assert_equals(g.it:roundtrip('config:info().hierarchy'), {})
    t.assert_equals(g.it:roundtrip("config:info('v1').hierarchy"), {})
    t.assert_equals(g.it:roundtrip("config:info('v2').hierarchy"), {})
    t.assert_equals(g.it:roundtrip('box.info.config.hierarchy'), {})
end

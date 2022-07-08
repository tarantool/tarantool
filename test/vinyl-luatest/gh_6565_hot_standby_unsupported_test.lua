local fio = require('fio')
local server = require('test.luatest_helpers.server')
local t = require('luatest')

local g = t.group()

g.before_each(function(cg)
    cg.master = server:new({alias = 'master'})
    cg.master:start()
    cg.replica = server:new({
        alias = 'replica',
        workdir = cg.master.workdir,
        box_cfg = {hot_standby = true},
    })
    cg.replica_log = fio.pathjoin(cg.replica.workdir,
                                  cg.replica.alias .. '.log')
end)

g.after_each(function(cg)
    -- The replica should fail to initialize and exit by itself, but let's
    -- still try to kill it to prevent it from lingering until the next test,
    -- because the next test may try to connect to the same URI.
    pcall(cg.replica.stop, cg.replica)
    cg.master:drop()
end)

g.test_panic_if_vinyl_space_exists = function(cg)
    cg.master:exec(function()
        box.schema.create_space('test', {engine = 'vinyl'})
        box.space.test:create_index('pk')
    end)
    cg.replica:start({wait_for_readiness = false})
    t.helpers.retrying({}, function()
        t.assert(cg.replica:grep_log("Entering hot standby mode",
                                     nil, {filename = cg.replica_log}))
        t.assert(cg.replica:grep_log("Vinyl does not support hot standby mode",
                                     nil, {filename = cg.replica_log}))
    end)
end

g.test_panic_if_vinyl_space_is_created = function(cg)
    cg.replica:start({wait_for_readiness = false})
    t.helpers.retrying({}, function()
        t.assert(cg.replica:grep_log("Entering hot standby mode",
                                     nil, {filename = cg.replica_log}))
    end)
    cg.master:exec(function()
        box.schema.create_space('test', {engine = 'vinyl'})
        box.space.test:create_index('pk')
    end)
    t.helpers.retrying({}, function()
        t.assert(cg.replica:grep_log("couldn't apply the request",
                                     nil, {filename = cg.replica_log}))
        t.assert(cg.replica:grep_log("Vinyl does not support hot standby mode",
                                     nil, {filename = cg.replica_log}))
    end)
end

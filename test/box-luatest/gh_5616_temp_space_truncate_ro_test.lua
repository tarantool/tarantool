local server = require('luatest.server')
local t = require('luatest')

local g_single = t.group('gh_5616_temp_space_truncate_ro.single')

g_single.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
end)

g_single.after_all(function(cg)
    cg.server:drop()
end)

g_single.after_each(function(cg)
    cg.server:exec(function()
        box.cfg({read_only = false})
        if box.space.test ~= nil then
            box.space.test:drop()
        end
    end)
end)

-- Checks that a temporary space can be truncated in the read-only mode.
g_single.test_temp_space_truncate_ro = function(cg)
    cg.server:exec(function()
        local s = box.schema.create_space('test', {temporary = true})
        s:create_index('primary')
        s:insert({1})
        box.cfg({read_only = true})
        local ok, err = pcall(s.truncate, s)
        t.assert(ok, err)
        t.assert_equals(s:select(), {})
    end)
end

-- Checks that a local space can be truncated in the read-only mode.
g_single.test_local_space_truncate_ro = function(cg)
    cg.server:exec(function()
        local s = box.schema.create_space('test', {is_local = true})
        s:create_index('primary')
        s:insert({1})
        box.cfg({read_only = true})
        local ok, err = pcall(s.truncate, s)
        t.assert(ok, err)
        t.assert_equals(s:select(), {})
    end)
end

-- Checks that a persistent space can't be truncated in the read-only mode.
g_single.test_persistent_space_truncate_ro = function(cg)
    cg.server:exec(function()
        local s = box.schema.create_space('test')
        s:create_index('primary')
        s:insert({1})
        box.cfg({read_only = true})
        local ok, err = pcall(s.truncate, s)
        t.assert_not(ok, err)
        t.assert_equals(s:select(), {{1}})
    end)
end

local g_replication = t.group('gh_5616_temp_space_truncate_ro.replication')

g_replication.before_all(function(cg)
    cg.master = server:new({alias = 'master'})
    cg.master:start()
    cg.replica = server:new({
        alias = 'replica',
        box_cfg = {
            read_only = true,
            replication = cg.master.net_box_uri,
        },
    })
    cg.replica:start()
end)

g_replication.after_all(function(cg)
    cg.replica:drop()
    cg.master:drop()
end)

g_replication.after_each(function(cg)
    cg.master:exec(function()
        if box.space.test ~= nil then
            box.space.test:drop()
        end
    end)
    cg.replica:wait_for_vclock_of(cg.master)
end)

-- Checks that a truncate operation for a temporary space isn't replicated to
-- a read-only replica.
g_replication.test_temp_space_truncate_ro = function(cg)
    cg.master:exec(function()
        local s = box.schema.create_space('test', {temporary = true})
        s:create_index('primary')
        s:insert({1})
    end)
    cg.replica:wait_for_vclock_of(cg.master)
    cg.replica:exec(function()
        local s = box.space.test
        t.assert_equals(s:select(), {})
        s:insert({2})
    end)
    cg.master:exec(function()
        local s = box.space.test
        s:truncate()
        t.assert_equals(s:select(), {})
    end)
    cg.replica:wait_for_vclock_of(cg.master)
    cg.replica:exec(function()
        local s = box.space.test
        t.assert_equals(s:select(), {{2}})
    end)
end

-- Checks that a truncate operation for a local space isn't replicated to
-- a read-only replica.
g_replication.test_local_space_truncate_ro = function(cg)
    cg.master:exec(function()
        local s = box.schema.create_space('test', {is_local = true})
        s:create_index('primary')
        s:insert({1})
    end)
    cg.replica:wait_for_vclock_of(cg.master)
    cg.replica:exec(function()
        local s = box.space.test
        t.assert_equals(s:select(), {})
        s:insert({2})
    end)
    cg.master:exec(function()
        local s = box.space.test
        s:truncate()
        t.assert_equals(s:select(), {})
    end)
    cg.replica:wait_for_vclock_of(cg.master)
    cg.replica:exec(function()
        local s = box.space.test
        t.assert_equals(s:select(), {{2}})
    end)
end

-- Checks that a truncate operation for a persistent space is replicated to
-- a read-only replica.
g_replication.test_persistent_space_truncate_ro = function(cg)
    cg.master:exec(function()
        local s = box.schema.create_space('test')
        s:create_index('primary')
        s:insert({1})
    end)
    cg.replica:wait_for_vclock_of(cg.master)
    cg.replica:exec(function()
        local s = box.space.test
        t.assert_equals(s:select(), {{1}})
    end)
    cg.master:exec(function()
        local s = box.space.test
        s:truncate()
        t.assert_equals(s:select(), {})
    end)
    cg.replica:wait_for_vclock_of(cg.master)
    cg.replica:exec(function()
        local s = box.space.test
        t.assert_equals(s:select(), {})
    end)
end

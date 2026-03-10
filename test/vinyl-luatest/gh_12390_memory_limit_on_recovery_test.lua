local server = require('luatest.server')
local fio = require('fio')
local t = require('luatest')

local g = t.group()

g.after_each(function(cg)
    if cg.server ~= nil then
        cg.server:drop()
    end
end)

g.test_recovery_memory_limit = function(cg)
    cg.server = server:new({box_cfg = {vinyl_memory = 10 * 1024 * 1024}})
    cg.server:start()
    cg.server:exec(function()
        local fiber = require('fiber')

        local s = box.schema.create_space('test', {engine = 'vinyl'})
        s:create_index('pk')

        box.error.injection.set('ERRINJ_VY_RUN_WRITE', true)
        local d = string.rep('x', 10 * 1024)
        fiber.set_max_slice(100)
        for i = 1, 768 do
            s:insert({i, d})
        end
    end)
    cg.server:restart({box_cfg = {vinyl_memory = 1024 * 1024}})
    cg.server:exec(function()
        local fiber = require('fiber')

        local stats = box.info.vinyl()
        t.assert_ge(stats.scheduler.dump_count, 7)

        local s = box.space.test
        local d = string.rep('x', 10 * 1024)
        local expected = {}
        for i = 1, 768 do
            table.insert(expected, {i, d})
        end
        fiber.set_max_slice(100)
        t.assert_equals(s:select(), expected)
    end)
    cg.server:restart({box_cfg = {vinyl_memory = 1024 * 1024}})
    cg.server:exec(function()
        local fiber = require('fiber')

        local s = box.space.test
        local d = string.rep('x', 10 * 1024)
        local expected = {}
        for i = 1, 768 do
            table.insert(expected, {i, d})
        end
        fiber.set_max_slice(100)
        t.assert_equals(s:select(), expected)
    end)
end

-- Check that recovery can be continued after crash and there is no stalls.
g.test_recovery_after_recovery_crash = function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server = server:new({box_cfg = {vinyl_memory = 10 * 1024 * 1024}})
    cg.server:start()
    cg.server:exec(function()
        local fiber = require('fiber')

        local s = box.schema.create_space('test', {engine = 'vinyl'})
        s:create_index('pk')

        box.error.injection.set('ERRINJ_VY_RUN_WRITE', true)
        local d = string.rep('x', 10 * 1024)
        fiber.set_max_slice(100)
        for i = 1, 768 do
            s:insert({i, d})
        end
    end)

    local before_box_cfg = [[
        box.error.injection.set('ERRINJ_VY_RUN_WRITE_CRASH_COUNTDOWN', 3)
    ]]
    t.assert_error(cg.server.restart, cg.server, {
        box_cfg = {vinyl_memory = 1024 * 1024},
        env = {TARANTOOL_RUN_BEFORE_BOX_CFG = before_box_cfg},
    })
    t.helpers.retrying({}, function()
        t.assert(cg.server:grep_log('injected crash'))
    end)

    local path = fio.pathjoin(cg.server.workdir, 512, 0)
    t.assert(fio.path.is_dir(path))
    local old_files = fio.listdir(path)
    t.assert_gt(#old_files, 0)

    cg.server:restart({
        box_cfg = {vinyl_memory = 1024 * 1024},
        env = {},
    })
    -- Check there was no throttling due unexpected run/index files.
    t.assert_not(cg.server:grep_log('throttling scheduler'))

    cg.server:exec(function()
        local fiber = require('fiber')

        local s = box.space.test
        local d = string.rep('x', 10 * 1024)
        local expected = {}
        for i = 1, 768 do
            table.insert(expected, {i, d})
        end
        fiber.set_max_slice(100)
        t.assert_equals(s:select(), expected)
        -- Forget unused run files.
        -- We may fail to remove them due injected crash.
        box.snapshot()
        s:insert({1000})
        box.snapshot()
    end)

    local new_files = fio.listdir(path)
    -- Check old run/index files are deleted.
    t.assert_items_exclude(new_files, old_files)
end

g.test_recovery_respect_inprogress_tasks = function(cg)
    cg.server = server:new({box_cfg = {vinyl_memory = 10 * 1024 * 1024}})
    cg.server:start()
    cg.server:exec(function()
        local fiber = require('fiber')

        local s = box.schema.create_space('test', {engine = 'vinyl'})
        s:create_index('pk')

        box.error.injection.set('ERRINJ_VY_RUN_WRITE', true)
        local d = string.rep('x', 10 * 1024)
        fiber.set_max_slice(100)
        for i = 1, 768 do
            s:insert({i, d})
        end
    end)
    -- Slow down so that at the end of the recovery the where inprogress
    -- tasks.
    local before_box_cfg = [[
        box.error.injection.set('ERRINJ_VY_RUN_WRITE_STMT_TIMEOUT', 0.02)
    ]]
    cg.server:restart({
        box_cfg = {vinyl_memory = 1024 * 1024},
        env = {TARANTOOL_RUN_BEFORE_BOX_CFG = before_box_cfg},
    })
    cg.server:exec(function()
        local fiber = require('fiber')
        -- Stop slowing down so that inprogress tasks fail in case we
        -- cleanup their files by mistake at the end of the recovery.
        box.error.injection.set('ERRINJ_VY_RUN_WRITE_STMT_TIMEOUT', 0)

        local stats = box.info.vinyl()
        t.assert_ge(stats.scheduler.dump_count, 7)

        local s = box.space.test
        local d = string.rep('x', 10 * 1024)
        local expected = {}
        for i = 1, 768 do
            table.insert(expected, {i, d})
        end
        fiber.set_max_slice(100)
        t.assert_equals(s:select(), expected)
    end)
    -- Check there was no throttling due to errors caused by deleting
    -- legal inprogress files.
    t.assert_not(cg.server:grep_log('throttling scheduler'))
end

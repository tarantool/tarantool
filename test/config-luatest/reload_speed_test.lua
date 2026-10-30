local fio = require('fio')
local fiber = require('fiber')
local t = require('luatest')
local treegen = require('luatest.treegen')
local server = require('luatest.server')

local g = t.group()

g.after_each(function(g)
    g.config_file = nil

    if g.fh ~= nil then
        g.fh:close()
        g.fh = nil
    end

    if g.server ~= nil then
        -- If tarantool works without any yield, it doesn't
        -- process SIGTERM. Let's send SIGKILL.
        if g.server.process ~= nil and g.server.process:is_alive() then
            g.server.process:kill('KILL')
        end

        -- Now we can wait for termination.
        g.server:stop()

        g.server = nil
    end
end)

local function open_config_file_for_writing(g, dir)
    g.config_file = fio.pathjoin(dir, 'config.yaml')
    local flags = {'O_CREAT', 'O_WRONLY', 'O_TRUNC'}
    local mode = tonumber('644', 8)
    g.fh = fio.open(g.config_file, flags, mode)
end

-- Generate instance name.
local function iname(i)
    return ('i-%04d'):format(i)
end

-- Generate replicaset name.
local function rname(i)
    return ('r-%04d'):format(i)
end

-- Verify that startup/reload a single instance takes less than
-- 10 seconds on a cluster with 9000 instances in the
-- configuration.
--
-- The test case is added together with an implementation of a
-- lazy evaluation of the instance configurations. The startup of
-- a single instance from the 9K instances config has the
-- following timings on my laptop:
--
-- * 0.6 seconds with the lazy configuration evaluation;
-- * 21 seconds without it.
g.test_basic = function(g)
    local dir = treegen.prepare_directory({}, {})

    -- Stream write to don't take much memory.
    open_config_file_for_writing(g, dir)
    g.fh:write('credentials:\n')
    g.fh:write('  users:\n')
    g.fh:write('    guest:\n')
    g.fh:write('      roles: [super]\n')
    g.fh:write('iproto:\n')
    g.fh:write('  listen: [{uri: "unix/:./{{ instance_name }}.iproto"}]\n')
    g.fh:write('\n')
    g.fh:write('groups:\n')
    g.fh:write('  g-001:\n')
    g.fh:write('    replicasets:\n')

    -- 9K replicasets with 1 instance each.
    for i = 1, 9000 do
        g.fh:write('      ' .. rname(i) .. ':\n')
        g.fh:write('        instances:\n')
        g.fh:write('          ' .. iname(i) .. ': {}\n')
    end

    g.fh:close()
    g.fh = nil

    g.server = server:new({
        alias = iname(1),
        chdir = dir,
        config_file = g.config_file,
    })

    -- Verify that startup takes less than 10 seconds.
    local finished_ch = fiber.channel(0)
    fiber.new(function()
        g.server:start()
        finished_ch:put(true)
    end)
    local ok = finished_ch:get(10)
    t.assert(ok, 'verify that tarantool starts within 10 seconds')

    -- Verify that we can obtain an option of another instance.
    g.server:exec(function()
        local config = require('config')

        local function iname(i)
            return ('i-%04d'):format(i)
        end

        local exp = 'tarantool - ' .. iname(2)
        local res = config:get('process.title', {instance = iname(2)})
        t.assert_equals(res, exp)
    end)

    -- Verify that configuration reload takes less than 10
    -- seconds.
    fiber.new(function()
        g.server:exec(function()
            local config = require('config')

            config:reload()
        end)
        finished_ch:put(true)
    end)
    local ok = finished_ch:get(10)
    t.assert(ok, 'verify that tarantool reloads within 10 seconds')

    -- Verify that we can obtain an option of another instance.
    g.server:exec(function()
        local config = require('config')

        local function iname(i)
            return ('i-%04d'):format(i)
        end

        local exp = 'tarantool - ' .. iname(3)
        local res = config:get('process.title', {instance = iname(3)})
        t.assert_equals(res, exp)
    end)
end

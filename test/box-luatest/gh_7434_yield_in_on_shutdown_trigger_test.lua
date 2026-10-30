local t = require('luatest')
local fio = require('fio')
local popen = require('popen')

local g = t.group('gh-7434')

-- Check that a child process created a file in the shutdown trigger
local tarantool = arg[-1]
local script = os.getenv('SOURCEDIR') .. '/test/box-luatest/gh_7434_child.lua'
local output_file

g.before_all(function(cg)
    cg.tempdir = fio.tempdir()
    output_file = cg.tempdir .. '/on_shutdown_completed.txt'
end)

g.after_each(function()
    os.remove(output_file)
end)

g.after_all(function(cg)
    fio.rmtree(cg.tempdir)
end)

-- Shutdown by reaching the end of the script
g.test_finish = function()
    local ph = popen.new({tarantool, script, output_file})
    t.assert(ph)
    ph:wait()
    ph:close()
    t.assert(fio.path.lexists(output_file))
end

-- Shutdown by Ctrl+D (interactive mode)
g.test_ctrld = function()
    local ph = popen.new({tarantool, '-i', script, output_file},
                         {stdout = popen.opts.PIPE, stdin = popen.opts.PIPE})
    t.assert(ph)
    ph:read()
    ph:shutdown({stdin = true})
    ph:wait()
    ph:close()
    t.assert(fio.path.lexists(output_file))
end

-- Shutdown by os.exit()
g.test_osexit = function()
    local ph = popen.new({tarantool, script, output_file, 'os_exit'})
    t.assert(ph)
    ph:wait()
    ph:close()
    t.assert(fio.path.lexists(output_file))
end

-- Shutdown by SIGTERM (init fiber is running)
g.test_sigterm1 = function()
    local ph = popen.new({tarantool, script, output_file, 'sleep'},
                         {stdout = popen.opts.PIPE})
    t.assert(ph)
    ph:read()
    ph:terminate()
    ph:wait()
    ph:close()
    t.assert(fio.path.lexists(output_file))
end

-- Shutdown by SIGTERM (init fiber finished, background is running)
g.test_sigterm2 = function()
    local ph = popen.new({tarantool, script, output_file, 'background'},
                         {stdout = popen.opts.PIPE})
    t.assert(ph)
    ph:read()
    ph:terminate()
    ph:wait()
    ph:close()
    t.assert(fio.path.lexists(output_file))
end

-- Shutdown by SIGTERM (lua script is passed via the -e option)
g.test_hyphene = function()
    local fiber = require('fiber')
    local f = io.open(script, 'r')
    t.assert(f)
    local expr = f:read('*all')
    f:close()
    expr = expr:gsub('arg%[1%]', '"' .. output_file .. '"')
    expr = expr:gsub('arg%[2%]', '"background"')
    local ph = popen.new({tarantool, '-e', expr}, {stdout = popen.opts.PIPE})
    t.assert(ph)
    ph:read()
    fiber.sleep(0.1)
    -- Verify that child process is still alive after reaching end of the script
    t.assert(ph:info().status.state == popen.state.ALIVE)
    ph:terminate()
    ph:wait()
    ph:close()
    t.assert(fio.path.lexists(output_file))
end

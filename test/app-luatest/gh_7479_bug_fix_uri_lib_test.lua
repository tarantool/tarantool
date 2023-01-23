local t = require('luatest')
local fio = require('fio')
local popen = require('popen')
local net_box = require('net.box')
local server = require('luatest.server')

local g = t.group()

-- Create test instance.
g.before_all(function()
    g.server = server:new({alias = 'test-gh-7479'})
    g.server:start()
    -- Get port for connection testing.
    g.server_port = g.server:exec(function()
        return require('console').listen(0):name()['port']
    end)
end)

-- Stop test instance.
g.after_all(function()
    g.server:stop()
end)

-- Checks whether it is possible to specify only port for tarantoolctl connect.
g.test_tarantoolctl_connect = function()
    -- Find tarantoolctl.
    -- Probably test-run knows the path to tarantoolctl.
    local TARANTOOLCTL_PATH = os.getenv('TARANTOOLCTL')
    if TARANTOOLCTL_PATH == nil then
        -- If test-run dose not know the path to tarantoolctl.
        -- Assume that current directory is tarantool's build directory.
        local BUILDDIR = os.getenv('BUILDDIR')
        TARANTOOLCTL_PATH = ('%s/extra/dist/tarantoolctl'):format(BUILDDIR)
        if not fio.path.exists(TARANTOOLCTL_PATH) then
            error("Can't find tarantoolctl")
        end
    end

    local cmd_tarantoolctl = {
        TARANTOOLCTL_PATH,
        'connect',
        -- Indicate only port, without localhost:* before the port value.
        tostring(g.server_port),
    }

    -- Connection will be established in a separate process.
    local ph_tarantoolctl = popen.new(cmd_tarantoolctl, {
        stdin  = 'devnull',
        stdout = 'devnull',
        stderr = popen.opts.PIPE,
    })
    -- Stderr should contain a message about successful connection.
    local result = ph_tarantoolctl:read({stderr = true}):rstrip()
    ph_tarantoolctl:close()

    t.assert_equals(
        result,
        'connected to localhost:' .. tostring(g.server_port)
    )
end

-- Checks whether it is possible to specify only port for net.box.connect.
g.test_net_box_connect_with_only_port_indication = function()
    -- Indicate only port, without localhost:* before the port value.
    local ok, _ = pcall(net_box.connect, nil, g.server_port)
    t.assert_equals(ok, true)
end

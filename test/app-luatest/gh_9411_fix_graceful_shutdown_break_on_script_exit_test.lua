local popen = require('popen')
local t = require('luatest')

local g = t.group()

g.after_each(function()
    if g.handle ~= nil then
        g.handle:close()
    end
    g.handle = nil
end)

g.test = function()
    local script = [[
        local fiber = require('fiber')

        box.ctl.set_on_shutdown_timeout(1)
        box.ctl.on_shutdown(function()
            fiber.sleep(0.2)
            print('shutdown callback finished')
        end, nil)

        fiber.create(function()
            os.exit(0)
        end)

        fiber.sleep(0.1)
    ]]
    local tarantool_bin = arg[-1]
    local handle, err = popen.new({tarantool_bin, '-e', script},
                                  {stdout = popen.opts.PIPE,
                                   stdin = popen.opts.DEVNULL,
                                   stderr = popen.opts.DEVNULL})
    assert(handle, err)
    g.handle = handle
    local output, err = handle:read({timeout = 3})
    assert(output, err)
    t.assert_equals(output, 'shutdown callback finished\n')
    local status = handle:wait()
    t.assert_equals(status.state, 'exited')
    t.assert_equals(status.exit_code, 0)
end

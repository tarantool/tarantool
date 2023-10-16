local t = require('luatest')
local popen = require('popen')

local g = t.group()

local tarantool = arg[-1]

g.after_each(function()
    if g.handle ~= nil then
        g.handle:close()
    end
    g.handle = nil
end)

g.test = function()
    local code = [[
        box.ctl.on_shutdown(function()
            print('hello')
        end)
        os.exit()
    ]]
    local handle, err = popen.new({tarantool, '-e', code},
                                  {stdout = popen.opts.PIPE,
                                   stdin = popen.opts.DEVNULL,
                                   stderr = popen.opts.DEVNULL})
    assert(handle, err)
    g.handle = handle
    local output, err = handle:read({timeout = 3})
    assert(output, err)
    t.assert_equals(output, "hello\n")
    local status = handle:wait()
    t.assert_equals(status.state, 'exited')
    t.assert_equals(status.exit_code, 0)
end

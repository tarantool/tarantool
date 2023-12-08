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
                                   stderr = popen.opts.DEVNULL,
                                   env = os.environ() })
    assert(handle, err)
    g.handle = handle
    -- NB: Don't guess a good timeout, just use 60 seconds as
    -- something that is definitely less than default test timeout
    -- (110 seconds), but large enough to perform an action that
    -- usually takes a fraction of a second.
    --
    -- The bad case doesn't stuck the test case anyway: if the
    -- child process doesn't call an on_shutdown trigger, the
    -- process exits without output and we get EOF (an empty
    -- string) here.
    local output, err = handle:read({timeout = 60})
    assert(output, err)
    t.assert_equals(output, "hello\n")
    local status = handle:wait()
    t.assert_equals(status.state, 'exited')
    t.assert_equals(status.exit_code, 0)
end

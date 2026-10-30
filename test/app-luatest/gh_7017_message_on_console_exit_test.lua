local popen = require('popen')
local t = require('luatest')
local g = t.group('gh-7017')

-- Read from ph:stdout and fail if it doesn't contain a `pattern'.
local function grep_stdout_or_fail(ph, pattern)
    local output = ''
    t.helpers.retrying({}, function()
        local chunk = ph:read({timeout = 0.05})
        if chunk ~= nil then
           output = output .. chunk
        end
        t.assert_str_contains(output, pattern)
    end)
end

-- Check that the message is printed when local console is exited.
g.test_message_on_console_exit = function()
    local tarantool_exe = arg[-1]
    local lua_code = [[ fiber = require('fiber')
                        fiber.new(function() fiber.sleep(120) end)
                        print('gh-7017 started')
                        require('console').start() ]]
    local ph = popen.new({tarantool_exe, '-e', lua_code},
                         {stdin = popen.opts.PIPE, stdout = popen.opts.PIPE})
    t.assert(ph)

    -- Wait for the process startup.
    grep_stdout_or_fail(ph, "gh-7017 started")

    -- Send EOF to exit console.
    ph:shutdown({stdin = true})

    -- Check that the message is printed.
    grep_stdout_or_fail(ph, "Exited console. Type Ctrl-C to exit Tarantool.")
    ph:close()
end

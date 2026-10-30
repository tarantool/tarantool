local t = require('luatest')

local g = t.group('gh-8158')

-- Check that Tarantool can be successfully started from the root directory.
g.test_start_from_root_dir = function()
    local popen = require('popen')
    local tarantool_exe = arg[-1]

    -- Use `cd' and `shell = true' due to lack of cwd option in popen (gh-5633).
    local cmd = string.format('cd / && %s -e "os.exit()"', tarantool_exe)
    local ph = popen.new({cmd}, {shell = true})
    t.assert(ph)
    local status = ph:wait()
    t.assert(status.state == popen.state.EXITED)
    t.assert(status.exit_code == 0)
    ph:close()
end

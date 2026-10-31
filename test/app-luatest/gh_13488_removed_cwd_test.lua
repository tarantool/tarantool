local fio = require('fio')
local popen = require('popen')
local t = require('luatest')

local g = t.group()

-- Tarantool must start in a removed working directory.
g.test_removed_cwd = function()
    local dir = fio.tempdir()
    local script = "print(package.searchroot()) " ..
                   "print((pcall(require, 'no_such_module'))) os.exit()"
    local cmd = ('cd %q && rmdir %q && exec %q -e %q'):format(
        dir, dir, arg[-1], script)
    local ph = popen.shell(cmd, 'r')
    local status = ph:wait({timeout = 60})
    local output = ph:read({timeout = 1})
    ph:close()
    t.assert_equals(status, {state = 'exited', exit_code = 0})
    t.assert_equals(output, 'nil\tgetcwd: No such file or directory\n' ..
                            'false\n')
end

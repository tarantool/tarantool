local popen = require('popen')
local t = require('luatest')

local g = t.group()

g.test_hostname_result = function()
    local ph = popen.shell("hostname", 'r')

    local exp_status = {
        state = popen.state.EXITED,
        exit_code = 0,
    }
    local status = ph:wait()
    t.skip_if(status.exit_code == 127, "no hostname on host")
    t.assert_equals(status, exp_status, 'verify process status')

    local hostname = ph:read():rstrip()
    t.assert_equals(hostname, box.info.hostname, 'verify hostname')

    ph:close()
end

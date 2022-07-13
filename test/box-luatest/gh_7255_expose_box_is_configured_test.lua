local fio = require('fio')
local t = require('luatest')

local g = t.group()

g.test_box_is_configured = function()
    t.assert_equals(box.is_configured, false)

    local vardir = os.getenv('VARDIR') or '/tmp/t'
    local workdir = fio.pathjoin(vardir, 'gh-7255')
    fio.rmtree(workdir)
    fio.mkdir(workdir)
    box.cfg{work_dir = workdir, log = fio.pathjoin(workdir, 'self.log')}
    t.assert_equals(box.is_configured, true)

    box.cfg{log_level = 4}
    t.assert_equals(box.is_configured, true)
end

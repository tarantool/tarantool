local t = require('luatest')
local treegen = require('luatest.treegen')
local justrun = require('luatest.justrun')

local g = t.group()

g.test_snapshot_before_box_cfg = function()
    t.assert_error_msg_equals('Please call box.cfg{} first',
                              -- The box.snapshot function object itself is
                              -- guarded by a metatable on box, so we have to
                              -- access it through a pcall-protected
                              -- `function() ... end`.
                              function() box.snapshot() end)
end

g.test_snapshot_during_box_cfg = function()
    local dir = treegen.prepare_directory({}, {})
    treegen.write_file(dir, 'main.lua', [[
        local t = require('luatest')
        local fiber = require('fiber')
        fiber.create(function() box.cfg{} end)
        t.assert_error_msg_equals('Please call box.cfg{} first',
                                  box.snapshot)
        box.ctl.wait_rw()
        os.exit(0)
    ]])
    local opts = {nojson = true, stderr = true}
    local res = justrun.tarantool(dir, {}, {'main.lua'}, opts)
    t.assert_equals(res.exit_code, 0, {res.stdout, res.stderr})
end

local t = require('luatest')
local treegen = require('luatest.treegen')
local justrun = require('luatest.justrun')

local g = t.group()

g.test_wal_sync_before_cfg = function()
    t.assert_error_msg_equals(
        'Please call box.cfg{} first',
        box.ctl.wal_sync
    )
end

g.test_wal_sync_during_cfg = function()
    local dir = treegen.prepare_directory({}, {})

    treegen.write_file(dir, 'main.lua', [[
        local t = require('luatest')
        local fiber = require('fiber')

        fiber.create(function()
            box.cfg{}
        end)

        t.assert_error_msg_equals(
            'Please call box.cfg{} first',
            box.ctl.wal_sync
        )

        box.ctl.wait_rw()
        os.exit(0)
    ]])

    local opts = {nojson = true, stderr = true}
    local res = justrun.tarantool(dir, {}, {'main.lua'}, opts)
    t.assert_equals(res.exit_code, 0, {res.stdout, res.stderr})
end

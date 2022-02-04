local server = require('test.luatest_helpers.server')
local t = require('luatest')
local g = t.group()
local fio = require('fio')

g.before_each(function()
    g.server = server:new({alias = 'master',
                          datadir = 'test/box-luatest/gh_6794_data'})
end)

g.after_test("test_panic_without_force_recovery", function()
    g.server:cleanup()
end)

g.after_test("test_ignore_with_force_recovery", function()
    g.server:drop()
end)

local mismatch_msg = "Replicaset vclock {.*} doesn't match recovered data {.*}"

g.test_panic_without_force_recovery = function()
    g.server:start({wait_for_readiness = false})
    t.helpers.retrying({}, function()
        local msg = "Can't proceed. " .. mismatch_msg
        local filename = fio.pathjoin(g.server.workdir, g.server.alias..'.log')
        t.assert(g.server:grep_log(msg, nil, {filename = filename}))
    end)
end

g.test_ignore_with_force_recovery = function()
    g.server.box_cfg = {force_recovery = true}
    g.server:start()
    t.helpers.retrying({}, function()
        local msg = mismatch_msg .. ": ignoring, because 'force_recovery' "..
                    "configuration option is set."
        t.assert(g.server:grep_log(msg))
    end)
    t.assert(g.server:exec(function()
                return box.info.signature >= 2 and
                       box.info.status == 'running' and
                       box.info.ro == false end),
             "Failed to recover data")
end

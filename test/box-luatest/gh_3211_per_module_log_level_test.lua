local server = require('test.luatest_helpers.server')
local t = require('luatest')

local function find_in_log(cg, str, must_be_present)
    t.helpers.retrying({timeout = 0.3, delay = 0.1}, function()
        local found = cg.server:grep_log(str) ~= nil
        t.assert(found == must_be_present)
    end)
end

local g1 = t.group('gh-3211-1')

g1.before_each(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
end)

g1.after_each(function(cg)
    cg.server:drop()
end)

-- Test log.new{...}
g1.test_log_new = function(cg)
    cg.server:exec(function()
        local t = require('luatest')
        local log = require('log')
        t.assert_error_msg_content_equals(
            "Illegal parameters, name should be a string", log.new)
        t.assert_error_msg_content_equals(
            "Illegal parameters, name should be a string", log.new, 123)

        local log1 = log.new('module1')
        local log2 = log.new('module2')
        log.info('info from the default log')
        log1.info('info from the first module')
        log2.info('info from the second module')
    end)

    find_in_log(cg, 'info from the default log', true)
    find_in_log(cg, 'module1 I> info from the first module', true)
    find_in_log(cg, 'module2 I> info from the second module', true)
end

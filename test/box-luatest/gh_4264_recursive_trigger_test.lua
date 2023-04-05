local t = require('luatest')
local server = require('luatest.server')
local fio = require('fio')

local g = t.group('gh-4264')

g.before_all(function(cg)
    cg.server = server:new{alias = 'server'}
    cg.server:start()
    cg.server:exec(function()
        local s = box.schema.space.create('test')
        s:create_index('pk')
    end)
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test_recursive_trigger_invocation = function(cg)
    local order = cg.server:exec(function()
        local order = {}
        local level = 0
        local s = box.space.test
        local f1 = function()
            level = level + 1
            table.insert(order, level * 10 + 1)
            if level >= 3 then
                return
            end
            s:replace{1}
            level = level - 1
        end
        local f2 = function()
            table.insert(order, level * 10 + 2)
        end
        s:on_replace(f2)
        s:on_replace(f1)
        s:replace{1}
        return order
    end)
    t.assert_equals(order, {11, 21, 31, 32, 22, 12},
                    "Correct recursive trigger invocation")
end

g.test_trigger_clear_from_trigger = function(cg)
    local order = cg.server:exec(function()
        local order = {}
        local s = box.space.test
        local f2 = function()
            table.insert(order, 2)
        end
        local f1
        f1 = function()
            table.insert(order, 1)
            s:on_replace(nil, f2)
            s:on_replace(nil, f1)
        end
        s:on_replace(f2)
        s:on_replace(f1)
        s:replace{2}
        return order
    end)
    t.assert_equals(order, {1}, "Correct trigger invocation when 1st trigger \
                                 clears 2nd")
end

local g2 = t.group('gh-4264-on-shutdown')

g2.before_test('test_on_shutdown_trigger_clear', function(cg)
    cg.server = server:new{alias = 'server'}
    cg.server:start()
end)

g2.after_test('test_on_shutdown_trigger_clear', function(cg)
    cg.server:drop()
end)

g2.test_on_shutdown_trigger_clear = function(cg)
    cg.server:exec(function()
        local log = require('log')
        local f2 = function()
            log.info('trigger 2 executed')
        end
        local f1
        f1 = function()
            log.info('trigger 1 executed')
            box.ctl.on_shutdown(nil, f2)
            box.ctl.on_shutdown(nil, f1)
        end
        box.ctl.on_shutdown(f2)
        box.ctl.on_shutdown(f1)
        -- Killing the server with luatest doesn't trigger the issue. Need
        -- os.exit for that.
        require('fiber').new(os.exit)
    end)

    local logfile = fio.pathjoin(cg.server.workdir, 'server.log')
    t.helpers.retrying({}, function()
        local found = cg.server:grep_log('trigger 1 executed', nil,
                                         {filename = logfile})
        t.assert(found ~= nil, 'first on_shutdown trigger is executed')
    end)
    local found = cg.server:grep_log('trigger 2 executed', nil,
                                     {filename = logfile})
    t.assert(found == nil, 'second on_shutdown trigger is not executed')
end

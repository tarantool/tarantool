local server = require('test.luatest_helpers.server')
local t = require('luatest')
local g = t.group()

g.before_all(function()
    g.server = server:new({
        alias = 'default',
        box_cfg = {
            -- Disable WAL so that replace does not yield
            wal_mode='None'
        }
    })
    g.server:start()
    g.server:exec(function()
        local s = box.schema.create_space('tester')
        s:create_index('pk')
        for i = 1, 1000 do
            s:insert{i}
        end
    end)
end)

g.after_all(function()
    g.server:drop()
end)

g.test_limit_iteration = function()
    g.server:exec(function()
        local fiber = require('fiber')
        local t = require('luatest')

        local check_fiber_slice = function(fiber_f)
            local fib = fiber.new(fiber_f)
            fib:set_joinable(true)
            local _, err = fib:join()
            t.assert_equals(tostring(err), "fiber slice is exceeded")
        end

        fiber.set_max_slice(0.2)
        local s = box.space.tester

        local function endless_select_func()
            while true do
                s:select{}
            end
        end
        check_fiber_slice(endless_select_func)

        local function endless_get_func()
            while true do
                for i = 1, 1000 do
                    s:get(i)
                end
            end
        end
        check_fiber_slice(endless_get_func)

        local function endless_pairs_func()
            while true do
                for _, _ in s:pairs() do end
            end
        end
        check_fiber_slice(endless_pairs_func)

        local function endless_replace_func()
            while true do
                for i = 1, 1000 do
                    s:replace{i}
                end
            end
        end
        check_fiber_slice(endless_replace_func)
    end)
end

local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.dflt = server:new({alias = 'dflt'})
    g.dflt:start()
    g.dflt:exec(function()
        local tarantool = require('tarantool')

        local _, _, enable_bt = string.find(tarantool.build.options,
                                            "-DENABLE_BACKTRACE=(%a+)")
        t.skip_if(enable_bt == "FALSE", "requires backtrace feature")
    end)
end)

g.after_all(function()
    g.dflt:drop()
end)

g.test_fiber_parent_backtrace = function()
    g.dflt:exec(function()
        local fiber = require('fiber')
        local yaml = require('yaml')

        local bt_frames_cnt
        local bt_frames_str
        local function some_lua_fn_name()
            local fiber_info = fiber.info()[fiber.self():id()]
            local bt = fiber_info.backtrace
            t.fail_if(not bt, "nil backtrace table indicates unwinding error")
            bt_frames_cnt = #bt
            bt_frames_str = yaml.encode(bt)
        end
        local function fiber_child()
            some_lua_fn_name()
        end
        local function fiber_parent()
            fiber.create(fiber_child)
        end
        local function fiber_grandparent()
            fiber.create(fiber_parent)
        end

        fiber_grandparent()
        local bt_frames_cnt_dflt = bt_frames_cnt
        t.assert_ge(bt_frames_cnt_dflt, 1)

        -- Match C function: `coro_init` or `coro_start`
        local coro_entry_fn_pattern = "coro_%a%a%a%a%a?"
        -- Match Lua function
        local lua_fn_pattern = "some_lua_fn_name"

        fiber:parent_backtrace_enable()
        fiber_grandparent()
        local bt_frames_cnt_w_parent_bt = bt_frames_cnt
        t.assert_ge(bt_frames_cnt_w_parent_bt, bt_frames_cnt_dflt)
        local coro_entry_fn_matches = 0
        local lua_fn_matches = 0
        for _ in string.gmatch(bt_frames_str, coro_entry_fn_pattern) do
            coro_entry_fn_matches = coro_entry_fn_matches + 1
        end
        t.assert_equals(coro_entry_fn_matches, 2)

        fiber:parent_backtrace_disable()
        fiber_grandparent()
        local bt_frames_cnt_wo_parent_bt = bt_frames_cnt
        t.assert_equals(bt_frames_cnt_wo_parent_bt, bt_frames_cnt_dflt,
                        "parent backtrace is disabled by default")
        coro_entry_fn_matches = 0
        for _ in string.gmatch(bt_frames_str, coro_entry_fn_pattern) do
            coro_entry_fn_matches = coro_entry_fn_matches + 1
        end
        t.assert_equals(coro_entry_fn_matches, 1)
        for _ in string.gmatch(bt_frames_str, lua_fn_pattern) do
            lua_fn_matches = lua_fn_matches + 1
        end
        t.assert_equals(lua_fn_matches, 1)
    end)
end

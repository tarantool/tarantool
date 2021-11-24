local t = require('luatest')
local server = require('test.luatest_helpers.server')

local g = t.group()

g.before_all = function()
    g.server = server:new({alias = 'master'})
    g.server:start()

    g.server:eval("t1 = box.schema.space.create('test1')")
    g.server:eval("_ = t1:create_index('pk')")
    g.server:eval("t1:format({{'f1', 'unsigned'}, {'f2', 'unsigned'}})")

    g.server:eval("t2 = box.schema.space.create('test2')")
    g.server:eval("_ = t2:create_index('pk')")
    g.server:eval("t2:format({{'f1', 'unsigned'}, {'f2', 'unsigned'}})")

    g.server:eval("t3 = box.schema.space.create('test3')")
    g.server:eval("_ = t3:create_index('pk')")
    g.server:eval("t3:format({{'f1', 'unsigned'}, {'f2', 'unsigned'}})")

    g.server:eval("t4 = box.schema.space.create('test4')")
    g.server:eval("_ = t4:create_index('pk')")
    g.server:eval("t4:format({{'f1', 'unsigned'}, {'f2', 'unsigned'}})")

    g.server:exec(function()
        for i = 1, 20 do
            local _ = t1:replace({i, i})
            _ = t2:replace({i, i})
            _ = t3:replace({i, i})
            _ = t4:replace({i, i})
        end
        local body1 = [[
            function(tuple)
            local new_tuple = {}
            new_tuple[1] = tuple[1]
            new_tuple[2] = tostring(tuple[2])
            return new_tuple
        end]]
        local body2   = [[
            function(tuple)
            if tuple[1] >= 15 then error("boom") end
            local new_tuple = {}
            new_tuple[1] = tuple[1]
            new_tuple[2] = tostring(tuple[2])
            return new_tuple
        end]];
        box.schema.func.create("upgrade_func", {body = body1, is_deterministic = true, is_sandboxed = true})
        box.schema.func.create("error_func", {body = [[function(tuple) error("boom") return end]],
                                              is_deterministic = true, is_sandboxed = true})
        box.schema.func.create("error_delayed_func", {body = body2, is_deterministic = true, is_sandboxed = true})
    end)
end

g.after_all = function()
    g.server:stop()
end

local function check_upgrade_status(space_id, status)
    if type(status) == "string" then
        local status_str = string.format("return box.space._space_upgrade:select(%d)[1][2]", space_id)
        t.assert_equals(g.server:eval(status_str), status)
    else
        local status_str = string.format("return box.space._space_upgrade:select(%d)", space_id)
        t.assert_equals(g.server:eval(status_str), {})
    end
end

local function check_data(space_id, key, expected)
    local select_str = string.format("return box.space[%d]:get(%d)[2]", space_id, key)
    t.assert_equals(g.server:eval(select_str), expected)
end

g.test_space_upgrade = function()
    g.server:eval("box.error.injection.set('ERRINJ_SPACE_UPGRADE_DELAY', true)")
    g.server:eval("new_format = {{name='x', type='unsigned'}, {name='y', type='string'}}")
    g.server:eval("t1:upgrade({mode = 'notest', func = 'upgrade_func', format = new_format, background = true})")
    g.server:eval("t2:upgrade({mode = 'test', func = 'upgrade_func', format = new_format, background = true})")
    g.server:eval("t3:upgrade({mode = 'notest', func = 'error_func', format = new_format, background = true})")
    g.server:eval("t4:upgrade({mode = 'notest', func = 'error_delayed_func', format = new_format, background = true})")
    g.server:eval("require('fiber').sleep(0.5)")
    check_upgrade_status(g.server:eval("return t1.id"), "inprogress")
    check_upgrade_status(g.server:eval("return t2.id"), "test")
    check_upgrade_status(g.server:eval("return t3.id"), "error")
    check_upgrade_status(g.server:eval("return t4.id"), "inprogress")

    g.server:restart()

    -- inprogress upgrade should finish.
    check_upgrade_status(g.server:eval("return box.space.test1.id"), nil)
    -- test upgrade should disappear.
    check_upgrade_status(g.server:eval("return box.space.test2.id"), nil)
    -- error upgrade should remain unchanged.
    check_upgrade_status(g.server:eval("return box.space.test3.id"), "error")
    -- ugprade change state to error during recovery.
    check_upgrade_status(g.server:eval("return box.space.test4.id"), "error")
    check_data(g.server:eval("return box.space.test1.id"), 20, "20")
    check_data(g.server:eval("return box.space.test2.id"), 20, 20)
    check_data(g.server:eval("return box.space.test3.id"), 1, 1)
    check_data(g.server:eval("return box.space.test4.id"), 1, "1")
    check_data(g.server:eval("return box.space.test4.id"), 20, 20)

    g.server:eval("new_format = {{name='x', type='unsigned'}, {name='y', type='string'}}")
    g.server:eval("_ = box.space.test3:upgrade({mode = 'notest', func = 'upgrade_func', format = new_format})")
    g.server:eval("_ = box.space.test4:upgrade({mode = 'notest', func = 'upgrade_func', format = new_format})")
end

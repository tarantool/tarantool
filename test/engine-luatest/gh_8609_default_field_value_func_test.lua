local t = require('luatest')
local g = t.group('gh-8609', {{engine = 'memtx'}, {engine = 'vinyl'}})

g.before_all(function(cg)
    local server = require('luatest.server')
    cg.server = server:new()
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.after_each(function(cg)
    cg.server:exec(function()
        if box.space.test then box.space.test:drop() end
        box.schema.func.drop('func1', {if_exists = true})
        box.schema.func.drop('func2', {if_exists = true})
        box.schema.func.drop('func3', {if_exists = true})
        box.schema.func.drop('func4', {if_exists = true})
        box.schema.user.drop('tester', {if_exists = true})
    end)
end)

-- Test functional default field values.
g.test_basics = function(cg)
    cg.server:exec(function(engine)
        local func1_body = "function() return 'noname' end"
        local func2_body = "function(arg) return (arg or 0) + 42 end"
        local func3_body = "function(arg) return {'aa', arg, 'dd'} end"
        local func4_body = "function(arg) return '['..tostring(arg)..']' end"

        box.schema.func.create('func1', {language = 'Lua', body = func1_body,
                                         is_deterministic = false})
        box.schema.func.create('func2', {language = 'Lua', body = func2_body,
                                         is_deterministic = true})
        box.schema.func.create('func3', {language = 'Lua', body = func3_body,
                                         is_deterministic = false})
        box.schema.func.create('func4', {language = 'Lua', body = func4_body,
                                         is_deterministic = true})
        local format = {
            {name='c1', type='string', default_func='func1'},
            {name='id', type='unsigned'},
            {name='c3', type='unsigned', default=100, default_func='func2'},
            {name='c4', type='any', default={'bb', 'cc'}, default_func='func3'},
            {name='c5', type='unsigned', default_func='func2'},
            -- Note that `default' is not a string, it's OK
            {name='c6', type='string', default=12, default_func='func4'}
        }
        local opts = {engine = engine, format = format}
        local s = box.schema.space.create('test', opts)
        s:create_index('pk', {parts={'id'}})

        t.assert_equals(s:insert{box.NULL, 1, 1, 1, box.NULL, ''},
                        {'noname', 1, 1, 1, 42, ''})
        t.assert_equals(s:insert{'', 2},
                        {'', 2, 142, {'aa', {'bb', 'cc'}, 'dd'}, 42, '[12]'})
    end, {cg.params.engine})
end

-- Test function pinning/unpinning.
g.test_pin_unpin = function(cg)
    local function init()
        cg.server:exec(function(engine)
            local func1_body = "function() end"
            box.schema.func.create('func1', {language = 'Lua',
                                             body = func1_body})
            local format = {{name = 'field1', default_func = 'func1'}}
            local opts = {engine = engine, format = format}
            local s = box.schema.space.create('test', opts)
            s:create_index('pk')
        end, {cg.params.engine})
    end

    local function check_references()
        cg.server:exec(function()
            t.assert_error_msg_content_equals(
                "Can't drop function " .. box.func.func1.id .. ": function " ..
                "is referenced by field default value",
                function() box.func.func1:drop() end)
        end)
    end

    -- Check that func1 can not be dropped
    init()
    check_references()

    cg.server:restart()
    check_references()

    cg.server:eval('box.snapshot()')
    cg.server:restart()
    check_references()

    -- Check that func1 is unpinned on space drop
    cg.server:exec(function()
        box.space.test:drop()
        box.func.func1:drop()
    end)

    -- Check that func1 can be dropped in one transaction with space drop
    init()
    check_references()
    cg.server:exec(function()
        box.begin()
        box.space.test:drop()
        box.func.func1:drop()
        box.commit()
    end)

    -- Check that func1 is still pinned after rollback
    init()
    check_references()
    cg.server:exec(function()
        box.begin()
        box.space.test:drop()
        box.func.func1:drop()
        box.rollback()
    end)
    check_references()

    -- Check that func1 is unpinned on space alter
    cg.server:exec(function()
        box.space.test:alter{format = {{name = 'field1'}}}
        box.func.func1:drop()
        box.space.test:drop()
    end)

    -- Check that func1 can be dropped in one transaction with space alter
    init()
    check_references()
    cg.server:exec(function()
        box.begin()
        box.space.test:alter{format = {{name = 'field1'}}}
        box.func.func1:drop()
        box.commit()
        box.space.test:drop()
    end)

    -- Check that func1 is still pinned after rollback
    init()
    check_references()
    cg.server:exec(function()
        box.begin()
        box.space.test:alter{format = {{name = 'field1'}}}
        box.func.func1:drop()
        box.rollback()
    end)
    check_references()
end

-- Test error messages.
g.test_errors = function(cg)
    local function test_func(func_body, error_msg)
        cg.server:exec(function(engine, func_body, error_msg)
            box.schema.func.create('func1', {language = 'Lua',
                                             body = func_body})
            local s = box.schema.space.create('test', {engine = engine})
            local fmt = {{name='id', type='integer'},
                         {name='f2', type='integer', default_func='func1'}}
            s:format(fmt)
            s:create_index('pk')
            t.assert_error_msg_equals(error_msg, s.insert, s, {1})
            box.space.test:drop()
            box.func.func1:drop()
        end, {cg.params.engine, func_body, error_msg})
    end

    test_func("function() error('foobar') end",
              "Error calling field default function 'func1': [string " ..
              "\"return function() error('foobar') end\"]:1: foobar")

    test_func("function() end",
              "Error calling field default function 'func1': expected 1 " ..
              "return value, got 0")

    test_func("function() return 1, 2 end",
              "Error calling field default function 'func1': expected 1 " ..
              "return value, got 2")

    test_func("function() return 'not_integer' end",
              "Tuple field 2 (f2) type does not match one required by " ..
              "operation: expected integer, got string")

    cg.server:exec(function(engine)
        local s = box.schema.space.create('test', {engine = engine})
        local format = {{name = 'f1', default_func = 666}}

        t.assert_error_msg_content_equals(
            "Illegal parameters, format[1]: field default function name is " ..
            "expected to be a string, but got number",
            s.format, s, format)

        t.assert_error_msg_content_equals(
            "Function '666' does not exist",
            function()
                box.space._space:update(s.id, {{'=', 'format', format}})
            end)

        t.assert_error_msg_content_equals(
            "Illegal parameters, format[1]: field default function was not " ..
            "found by name 'func1'",
            s.format, s, {{name = 'f1', default_func = 'func1'}})

        box.schema.func.create('func1', {language = 'C'})
        t.assert_error_msg_content_equals(
            "Failed to create field default function 'func1': " ..
            "unsupported language",
            s.format, s, {{name = 'f1', default_func = 'func1'}})

        box.schema.func.create('func2', {language = 'Lua'})
        t.assert_error_msg_content_equals(
            "Failed to create field default function 'func2': " ..
            "Lua function must have persistent body",
            s.format, s, {{name = 'f1', default_func = 'func2'}})

        local func_body = "function() return 42 end"
        box.schema.func.create('func3', {language = 'Lua', body = func_body})

        -- Switch to a user without execute access to func3
        box.session.su('admin')
        box.schema.user.create('tester')
        box.schema.user.grant('tester', 'read,write,alter', 'universe')
        box.session.su('tester', function()
            t.assert_error_msg_content_equals(
               "Execute access to function 'func3' is denied for user 'tester'",
                s.format, s, {{name = 'f1', default_func = 'func3'}})
        end)
    end, {cg.params.engine})
end

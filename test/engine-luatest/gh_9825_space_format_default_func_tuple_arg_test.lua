local t = require('luatest')
local g = t.group(nil, t.helpers.matrix{engine = {'memtx', 'vinyl'}})

g.before_all(function(cg)
    local server = require('luatest.server')
    cg.server = server:new()
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

-- Test that the tuple argument passed to the `default_func` field option of
-- `space:format` works correctly.
g.test_space_format_default_func_tuple_arg = function(cg)
    cg.server:exec(function(engine)
        rawset(_G, 'test_data', {})

        local nil_arg_body = [[
            function(arg, t)
                _G.test_data.nil_arg = arg
                _G.test_data.nil_arg_tuple = t
                return 5
            end
        ]]
        box.schema.func.create('nil_arg',
                               {language = 'Lua', body = nil_arg_body})
        local test_body = [[
            function(arg, t)
                _G.test_data.arg = arg;
                _G.test_data.arg_tuple = t;
                return t[1] + t[4] + t[7]
            end
        ]]
        box.schema.func.create('test', {language = 'Lua', body = test_body})

        local format = {
            {name='f1'},
            {name='f2', type = 'string'},
            {name='f3', default = 3},
            {name='f4', default = 44},
            {name='f5', default_func = 'nil_arg'},
            {name='f6', default = 6, default_func = 'test'},
        }
        local opts = {engine = engine, format = format}
        local s = box.schema.space.create('test', opts)
        s:create_index('pk')

        local source = box.tuple.new{1, 666, box.NULL, 4, box.NULL, box.NULL, 7}
        local err_msg = "Tuple field 2 (f2) type does not match one " ..
                        "required by operation: expected string, got unsigned"
        t.assert_error_msg_content_equals(err_msg,
                                          function() s:insert(source) end)
        t.assert_equals(_G.test_data.nil_arg, nil)
        t.assert_equals(_G.test_data.arg, 6)
        t.assert_equals(_G.test_data.nil_arg_tuple, _G.test_data.arg_tuple)
        -- The tuple argument is exactly the same as the source tuple.
        t.assert_equals(_G.test_data.arg_tuple, source)
        -- The tuple argument does not have the format dictionary.
        t.assert_equals(_G.test_data.arg_tuple:totable(),
                        _G.test_data.arg_tuple:tomap())
        -- The tuple argument may not comply to the space format.
        t.assert_not_equals(type(_G.test_data.arg_tuple[2]), 'string')
        t.assert_equals(s:insert{1, 's', box.NULL, 4, box.NULL,  box.NULL, 7},
                                {1, 's',        3, 4,        5, 1 + 4 + 7, 7})
    end, {cg.params.engine})
end

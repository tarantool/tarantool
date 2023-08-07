local t = require('luatest')
local server = require('luatest.server')

local g = t.group('gh-7650', {{engine = 'memtx'}, {engine = 'vinyl'}})

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.after_each(function(cg)
    cg.server:exec(function()
        if box.space.city then box.space.city:drop() end
        if box.space.country then box.space.country:drop() end
        if box.func.check_code then box.func.check_code:drop() end
    end)
end)

-- Check that it's not possible to create for a field a check constraint and
-- a foreign key with the same name.
g.test_field_constr = function(cg)
    cg.server:exec(function(engine)
        local func_body = "function(field) return field ~= '' end"
        local func_opts = {language = 'Lua', body = func_body,
                           is_deterministic = true}
        box.schema.func.create('check_code', func_opts)

        local format = {{name = 'code', type = 'string'},
                        {name = 'name', type = 'string'}}
        box.schema.create_space('country', {engine = engine, format = format})

        format = {{name = 'name', type = 'string'},
                  {name = 'country_code', type = 'string',
                   constraint = {some_name = 'check_code'},
                   foreign_key = {some_name = {space = 'country',
                                               field = 'code'}}}}
        local space_opts = {engine = engine, format = format}

        t.assert_error_msg_equals(
            "Wrong space format field 2: duplicate constraint name 'some_name'",
            function() box.schema.create_space('city', space_opts) end)
    end, {cg.params.engine})
end

-- Check that it's not possible to create a tuple check constraint and a foreign
-- key with the same name.
g.test_tuple_constr = function(cg)
    cg.server:exec(function(engine)
        local func_body = "function(tuple) return tuple[1] ~= '' end"
        local func_opts = {language = 'Lua', body = func_body,
                           is_deterministic = true}
        box.schema.func.create('check_code', func_opts)

        local format = {{name = 'code', type = 'string'},
                        {name = 'name', type = 'string'}}
        box.schema.create_space('country', {engine = engine, format = format})

        format = {{name = 'name', type = 'string'},
                  {name = 'country_code', type = 'string'}}
        local space_opts = {engine = engine, format = format,
                            constraint = {some_name = 'check_code'},
                            foreign_key = {some_name = {space = 'country',
                                                        field = {country_code =
                                                                 'code'}}}}
        t.assert_error_msg_equals(
            "Wrong space options: duplicate constraint name 'some_name'",
            function() box.schema.create_space('city', space_opts) end)
    end, {cg.params.engine})
end

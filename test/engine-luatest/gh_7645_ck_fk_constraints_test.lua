-- https://github.com/tarantool/tarantool/issues/7645
local server = require('luatest.server')
local t = require('luatest')

local g = t.group('gh-7645-ck-fk-constraints', {{engine = 'memtx'},
                                                {engine = 'vinyl'}})

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:stop()
    cg.server = nil
end)

-- Check that check and foreign key constraints work well on the same field.
g.test_field_ck_fk_constraints = function(cg)
    local engine = cg.params.engine

    cg.server:exec(function(engine)
        local city_fmt = {{'id', 'integer'}, {'name', 'string'}}
        local city = box.schema.space.create('city', {engine = engine,
                                                      format = city_fmt})
        city:create_index('pk')
        city:replace{1, 'Moscow'}
        city:replace{100, 'Amsterdam'}

        local body = "function(x) return x < 100 end"
        box.schema.func.create('check', {is_deterministic = true, body = body})
        local user_fmt = {{'id', 'integer'},
                          {'city_id', 'integer'},
                          {'name', 'string'}}
        user_fmt[2].constraint = {ck = 'check'}
        user_fmt[2].foreign_key = {city = {space = 'city', field = 'id'}}
        local space_opts = {engine = engine, format = user_fmt}
        local user = box.schema.space.create('user', space_opts)
        user:create_index('pk')
        user:replace{1, 1, 'Alice'}
        t.assert_error_msg_content_equals(
            "Foreign key constraint 'city' failed for field" ..
            " '2 (city_id)': foreign tuple was not found",
            function() user:replace({2, 99, 'Bob'}) end
        )
        t.assert_error_msg_content_equals(
            "Check constraint 'ck' failed for field '2 (city_id)'",
            function() user:replace{2, 100, 'Bob'} end
        )
    end, {engine})
end

g.after_test('test_field_ck_fk_constraints', function(cg)
    cg.server:exec(function()
        if box.space.user then
            box.space.user:drop()
        end
        if box.func.check then
            box.func.check:drop()
        end
        if box.space.city then
            box.space.city:drop()
        end
    end)
end)

-- Check that check and foreign key constraints work well on the same field.
g.test_tuple_ck_fk_constraints = function(cg)
    local engine = cg.params.engine

    cg.server:exec(function(engine)
        local city_fmt = {{'id', 'integer'}, {'name', 'string'}}
        local city = box.schema.space.create('city', {engine = engine,
                                                      format = city_fmt})
        city:create_index('pk')
        city:replace{1, 'Moscow'}
        city:replace{100, 'Amsterdam'}

        local body = "function(tuple) return tuple.city_id < 100 end"
        box.schema.func.create('check', {is_deterministic = true, body = body})
        local user_fmt = {{'id', 'integer'},
                          {'city_id', 'integer'},
                          {'name', 'string'}}
        local foreign_key = {city = {space = 'city', field = {city_id = 'id'}}}
        local space_opts = {engine = engine, format = user_fmt,
                            constraint = {check = 'check'},
                            foreign_key = foreign_key}
        local user = box.schema.space.create('user', space_opts)
        user:create_index('pk')
        user:replace{1, 1, 'Alice'}
        t.assert_error_msg_content_equals(
            "Foreign key constraint 'city' failed: foreign tuple was not found",
            function() user:replace({2, 99, 'Bob'}) end
        )
        t.assert_error_msg_content_equals(
            "Check constraint 'check' failed for a tuple",
            function() user:replace{2, 100, 'Bob'} end
        )
    end, {engine})
end

g.after_test('test_tuple_ck_fk_constraints', function(cg)
    cg.server:exec(function()
        if box.space.user then
            box.space.user:drop()
        end
        if box.func.check then
            box.func.check:drop()
        end
        if box.space.city then
            box.space.city:drop()
        end
    end)
end)

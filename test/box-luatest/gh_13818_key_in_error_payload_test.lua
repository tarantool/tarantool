local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.after_each(function(cg)
    cg.server:exec(function()
        for _, name in ipairs({'test', 'city', 'country'}) do
            if box.space[name] ~= nil then
                box.space[name]:drop()
            end
        end
        for _, name in ipairs({'two_parts', 'wrong_type'}) do
            if box.func[name] ~= nil then
                box.func[name]:drop()
            end
        end
    end)
end)

-- The key returned by a functional index function is in the error.
g.test_func_index = function(cg)
    cg.server:exec(function()
        local s = box.schema.create_space('test')
        s:create_index('pk')
        local opts = {is_deterministic = true, is_sandboxed = true}
        opts.body = 'function(tuple) return {tuple[1], tuple[2]} end'
        box.schema.func.create('two_parts', opts)
        opts.body = 'function(tuple) return {"foo"} end'
        box.schema.func.create('wrong_type', opts)

        local i = s:create_index('two_parts', {
            func = 'two_parts', parts = {{1, 'unsigned'}},
        })
        t.assert_error_covers({
            name = 'FUNC_INDEX_FORMAT',
            key = {1, 2},
        }, s.insert, s, {1, 2})
        i:drop()

        s:create_index('wrong_type', {
            func = 'wrong_type', parts = {{1, 'unsigned'}},
        })
        t.assert_error_covers({
            name = 'FUNC_INDEX_FORMAT',
            key = {'foo'},
        }, s.insert, s, {1, 2})
    end)
end

-- The key looked up in the foreign index is in the error.
g.test_foreign_key = function(cg)
    cg.server:exec(function()
        local country = box.schema.create_space('country', {
            format = {{'id', 'unsigned'}},
        })
        country:create_index('pk')
        local city = box.schema.create_space('city', {
            format = {
                {'id', 'unsigned'},
                {'country_id', 'any',
                 foreign_key = {space = 'country', field = 'id'}},
            },
        })
        city:create_index('pk')

        t.assert_error_covers({
            name = 'FIELD_FOREIGN_KEY_FAILED',
            message = "Foreign key constraint 'country' failed for " ..
                      "field '2 (country_id)': foreign tuple was not found",
            key = {42},
        }, city.insert, city, {1, 42})
        t.assert_error_covers({
            name = 'FIELD_FOREIGN_KEY_FAILED',
            message = "Foreign key constraint 'country' failed for " ..
                      "field '2 (country_id)': wrong key type",
            key = {'foo'},
        }, city.insert, city, {1, 'foo'})

        local i = city:create_index('country', {
            parts = {{'country_id', 'unsigned'}}, unique = false,
        })
        country:insert({42})
        city:insert({1, 42})
        t.assert_error_covers({
            code = box.error.FOREIGN_KEY_INTEGRITY,
            message = "Foreign key 'country' integrity check failed: " ..
                      "tuple is referenced",
            key = {42},
        }, country.delete, country, {42})

        city:delete({1})
        i:alter({parts = {{'country_id', 'string'}}})
        t.assert_error_covers({
            code = box.error.FOREIGN_KEY_INTEGRITY,
            message = "Foreign key 'country' integrity check failed: " ..
                      "wrong key type",
            key = {42},
        }, country.delete, country, {42})
    end)
end

-- The key of a complex foreign key is in the error.
g.test_complex_foreign_key = function(cg)
    cg.server:exec(function()
        local country = box.schema.create_space('country', {
            format = {{'id', 'unsigned'}, {'code', 'string'}},
        })
        country:create_index('pk', {parts = {'id', 'code'}})
        local city = box.schema.create_space('city', {
            format = {
                {'id', 'unsigned'},
                {'country_id', 'unsigned'},
                {'country_code', 'any'},
            },
            foreign_key = {
                country = {
                    space = 'country',
                    field = {country_id = 'id', country_code = 'code'},
                },
            },
        })
        city:create_index('pk')

        t.assert_error_covers({
            name = 'COMPLEX_FOREIGN_KEY_FAILED',
            message = "Foreign key constraint 'country' failed: " ..
                      "foreign tuple was not found",
            key = {42, 'vn'},
        }, city.insert, city, {1, 42, 'vn'})
        t.assert_error_covers({
            name = 'COMPLEX_FOREIGN_KEY_FAILED',
            message = "Foreign key constraint 'country' failed: " ..
                      "wrong key type",
            key = {42, 1},
        }, city.insert, city, {1, 42, 1})

        city:create_index('country', {
            parts = {{'country_id'}, {'country_code', 'string'}},
            unique = false,
        })
        country:insert({42, 'vn'})
        city:insert({1, 42, 'vn'})
        t.assert_error_covers({
            code = box.error.FOREIGN_KEY_INTEGRITY,
            key = {42, 'vn'},
        }, country.delete, country, {42, 'vn'})
    end)
end

-- The position, the search key and the index are in the error.
g.test_iterator_position = function(cg)
    cg.server:exec(function()
        local s = box.schema.create_space('test')
        local i = s:create_index('pk')
        s:insert({1})
        s:insert({10})
        local pos = i:tuple_pos({1})
        t.assert_error_covers({
            name = 'ITERATOR_POSITION',
            position = {1},
            key = {5},
            index = 'pk',
            index_id = i.id,
            space = 'test',
            space_id = s.id,
        }, i.select, i, {5}, {iterator = 'GE', after = pos})
    end)
end

-- Both keys of an invalid range are in the error.
g.test_range = function(cg)
    cg.server:exec(function()
        local s = box.schema.create_space('test')
        local i = s:create_index('pk', {parts = {1, 'unsigned', 2, 'unsigned'}})
        t.assert_error_covers({
            type = 'IllegalParams',
            message = 'begin_key must be < end_key',
            begin_key = {10, 10},
            end_key = {10},
        }, i.delete_range, i, {10, 10}, {10})
        t.assert_error_covers({
            type = 'IllegalParams',
            message = 'begin_key must be < end_key',
            begin_key = {10},
            end_key = {5},
        }, i.quantile, i, 0.5, {10}, {5})
    end)
end

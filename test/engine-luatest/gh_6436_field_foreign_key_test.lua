-- https://github.com/tarantool/tarantool/issues/6436 Foreign keys
local server = require('luatest.server')
local t = require('luatest')

local g = t.group('gh-6436-field-foreign-key-test', {{engine = 'memtx'}, {engine = 'vinyl'}})

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:stop()
    cg.server = nil
end)

g.before_each(function()
end)

g.after_each(function(cg)
    cg.server:exec(function()
        if box.space.city then box.space.city:drop() end
        if box.space.country then box.space.country:drop() end
        if box.space.user then box.space.user:drop() end
        if box.space.card then box.space.card:drop() end
    end)
end)

-- Test with wrong foreign key definitions.
g.test_bad_foreign_key = function(cg)
    local engine = cg.params.engine

    cg.server:exec(function(engine)
        local country = box.schema.create_space('country', {engine=engine})
        country:create_index('pk')
        local function gen_format(foreign_key)
            return {{name='id', type='unsigned'},
                    {name='country_id',type='unsigned',foreign_key=foreign_key},
                    {name='name', type='string'}}
        end
        local fmt = gen_format("hello world")
        t.assert_error_msg_content_equals(
            "Illegal parameters, format[2]: foreign key must be a table",
            function() box.schema.create_space('city', {engine=engine, format=fmt}) end
        )
        fmt = gen_format({fkey="hello world"})
        t.assert_error_msg_content_equals(
            "Illegal parameters, format[2]: foreign key definition must be a table with 'space' and 'field' members",
            function() box.schema.create_space('city', {engine=engine, format=fmt}) end
        )
        fmt = gen_format({space = 'country'})
        t.assert_error_msg_content_equals(
            "Illegal parameters, format[2]: foreign key definition must be a table with 'space' and 'field' members",
            function() box.schema.create_space('city', {engine=engine, format=fmt}) end
        )
        fmt = gen_format({fkey={space = 'country'}})
        t.assert_error_msg_content_equals(
            "Illegal parameters, format[2]: foreign key: field must be specified",
            function() box.schema.create_space('city', {engine=engine, format=fmt}) end
        )
        fmt = gen_format({space = 'planet', field = 'id'})
        t.assert_error_msg_content_equals(
            "Illegal parameters, format[2]: foreign key: space planet was not found",
            function() box.schema.create_space('city', {engine=engine, format=fmt}) end
        )
        fmt = gen_format({fkey={space = 'planet', field = 'id'}})
        t.assert_error_msg_content_equals(
            "Illegal parameters, format[2]: foreign key: space planet was not found",
            function() box.schema.create_space('city', {engine=engine, format=fmt}) end
        )
        fmt = gen_format({space = {'country'}, field = 'id'})
        t.assert_error_msg_content_equals(
            "Illegal parameters, format[2]: foreign key: space must be string or number",
            function() box.schema.create_space('city', {engine=engine, format=fmt}) end
        )
        fmt = gen_format({fkey={space = {'country'}, field = 'id'}})
        t.assert_error_msg_content_equals(
            "Illegal parameters, format[2]: foreign key: space must be string or number",
            function() box.schema.create_space('city', {engine=engine, format=fmt}) end
        )
        fmt = gen_format({space = 'country', field = false})
        t.assert_error_msg_content_equals(
            "Illegal parameters, format[2]: foreign key: field must be string or number",
            function() box.schema.create_space('city', {engine=engine, format=fmt}) end
        )
        fmt = gen_format({fkey={space = 'country', field = false}})
        t.assert_error_msg_content_equals(
            "Illegal parameters, format[2]: foreign key: field must be string or number",
            function() box.schema.create_space('city', {engine=engine, format=fmt}) end
        )
        fmt = gen_format({space = 'country', field = 'id', mood = 'wtf'})
        t.assert_error_msg_content_equals(
            "Illegal parameters, format[2]: foreign key: unexpected parameter 'mood'",
            function() box.schema.create_space('city', {engine=engine, format=fmt}) end
        )
        fmt = gen_format({fkey={space = 'country', field = 'id', mood = 'wtf'}})
        t.assert_error_msg_content_equals(
            "Illegal parameters, format[2]: foreign key: unexpected parameter 'mood'",
            function() box.schema.create_space('city', {engine=engine, format=fmt}) end
        )
        fmt = gen_format({[string.rep('a', 66666)] = {space = 'country', field = 'id'}})
        t.assert_error_msg_content_equals(
            "Wrong space format field 2: foreign key name is too long",
            function() box.schema.create_space('city', {engine=engine, format=fmt}) end
        )
        fmt = gen_format({[''] = {space = 'country', field = 'id'}})
        t.assert_error_msg_content_equals(
            "Wrong space format field 2: foreign key name isn't a valid identifier",
            function() box.schema.create_space('city', {engine=engine, format=fmt}) end
        )
    end, {engine})
end

-- Test with foreign key by primary index.
g.test_foreign_key_primary = function(cg)
    local engine = cg.params.engine

    cg.server:exec(function(engine)
        local country = box.schema.create_space('country', {engine=engine})
        country:create_index('pk')
        country:replace{1, 'ru', 'Russia'}
        country:replace{2, 'fr', 'France'}
        local fmt = {{name='id', type='unsigned'},
                     {name='country_id', type='unsigned',
                      foreign_key={space='country', field='id'}},
                     {name='name', type='string'},
                    }
        local city = box.schema.create_space('city', {engine=engine, format=fmt})
        -- Note that the format was normalized
        t.assert_equals(city:format(),
                        { { name = "id", type = "unsigned"},
                          { foreign_key = {country = {field = "id", space = country.id}},
                            name = "country_id", type = "unsigned"},
                          { name = "name", type = "string"} });
        city:create_index('pk')

        t.assert_equals(country:select{}, {{1, 'ru', 'Russia'}, {2, 'fr', 'France'}})
        t.assert_error_msg_content_equals(
            "Can't modify space 'country': space is referenced by foreign key",
            box.atomic, country.drop, country
        )
        t.assert_equals(country:select{}, {{1, 'ru', 'Russia'}, {2, 'fr', 'France'}})
        t.assert_error_msg_content_equals(
            "Foreign key constraint 'country' failed for field '2 (country_id)': " ..
            "foreign index was not found",
            function() city:replace{1, 1, 'Moscow'} end
        )
        local fmt = {{name = 'id', type = 'unsigned'},
                     {name = 'code', type = 'string'},
                     {name = 'name', type = 'string'},
                    }
        country:format(fmt)
        t.assert_error_msg_content_equals(
            "Foreign key constraint 'country' failed for field '2 (country_id)': " ..
            "foreign tuple was not found",
            function() city:replace{1, 500, 'Moscow'} end
        )
        city:replace{11, 1, 'Moscow'}
    end, {engine})

    cg.server:restart()

    cg.server:exec(function()
        local city = box.space.city
        city:replace{12, 1, 'Tomsk'}
        t.assert_equals(city:select{}, {{11, 1, 'Moscow'}, {12, 1, 'Tomsk'}})
    end, {engine})

    cg.server:eval('box.snapshot()')
    cg.server:restart()

    cg.server:exec(function()
        local country = box.space.country
        local city = box.space.city
        t.assert_error_msg_content_equals(
            "Foreign key 'country' integrity check failed: index was not found",
            function() country:delete{2} end
        )
        city:create_index('country_id', {parts={'country_id'},unique=false})
        country:delete{2}
        t.assert_error_msg_content_equals(
            "Foreign key 'country' integrity check failed: tuple is referenced",
            function() country:delete{1} end
        )
        city:delete{11}
        city:delete{12}
        country:delete{1}
        city:drop()
        country:drop()
    end, {engine})
end

-- Test with foreign key by secondary index and some variations.
g.test_foreign_key_secondary = function(cg)
    local engine = cg.params.engine

    cg.server:exec(function(engine)
        local country = box.schema.create_space('country', {engine=engine})
        country:create_index('pk')
        country:replace{1, 'ru', 'Russia'}
        country:replace{2, 'fr', 'France'}
        local fmt = {{name='id', type='unsigned'},
                     {name='country_code', type='string',
                      foreign_key={country={space='country', field='code'}}},
                     {name='name', type='string'} }
        local city = box.schema.create_space('city', {engine=engine, format=fmt})
        -- Note that the format was normalized
        t.assert_equals(city:format(),
                        { { name = "id", type = "unsigned"},
                          { foreign_key = {country = {field = "code", space = country.id}},
                            name = "country_code", type = "string"},
                          { name = "name", type = "string"} });
        city:create_index('pk')

        t.assert_equals(country:select{}, {{1, 'ru', 'Russia'}, {2, 'fr', 'France'}})
        t.assert_error_msg_content_equals(
            "Can't modify space 'country': space is referenced by foreign key",
            box.atomic, country.drop, country
        )
        t.assert_equals(country:select{}, {{1, 'ru', 'Russia'}, {2, 'fr', 'France'}})
        t.assert_error_msg_content_equals(
            "Foreign key constraint 'country' failed for field '2 (country_code)': " ..
            "foreign index was not found",
            function() city:replace{1, 'ru', 'Moscow'} end
        )
        local fmt = {{name='id', type='unsigned'},
                     {name='code', type='string'},
                     {name='name', type='string'}}
        country:format(fmt)
        t.assert_error_msg_content_equals(
            "Foreign key constraint 'country' failed for field '2 (country_code)': " ..
            "foreign index was not found",
            function() city:replace{1, 'ru', 'Moscow'} end
        )
        country:create_index('code', {parts={'code'}})
        t.assert_error_msg_content_equals(
            "Foreign key constraint 'country' failed for field '2 (country_code)': " ..
            "foreign tuple was not found",
            function() city:replace{1, 'de', 'Berlin'} end
        )
        city:replace{1, 'ru', 'Moscow'}
    end, {engine})

    cg.server:restart()

    cg.server:exec(function()
        local city = box.space.city
        city:replace{2, 'ru', 'Tomsk'}
        t.assert_equals(city:select{}, {{1, 'ru', 'Moscow'}, {2, 'ru', 'Tomsk'}})
    end, {engine})

    cg.server:eval('box.snapshot()')
    cg.server:restart()

    cg.server:exec(function()
        local country = box.space.country
        local city = box.space.city
        t.assert_error_msg_content_equals(
            "Foreign key 'country' integrity check failed: index was not found",
            function() country:delete{2} end
        )
        city:create_index('country_code', {parts={'country_code'},unique=false})
        country:delete{2}
        t.assert_error_msg_content_equals(
            "Foreign key 'country' integrity check failed: tuple is referenced",
            function() country:delete{1} end
        )
        city:delete{1}
        city:delete{2}
        country:delete{1}
        city:drop()
        country:drop()
    end, {engine})
end

-- Test with foreign key by numeric space and field
g.test_foreign_key_numeric = function(cg)
    local engine = cg.params.engine

    cg.server:exec(function(engine)
        local country = box.schema.create_space('country', {engine=engine})
        country:create_index('pk')
        country:replace{1, 'ru', 'Russia'}
        country:replace{2, 'fr', 'France'}
        local fmt = {{name='id', type='unsigned'},
                     {name='country_code', type='string',
                      foreign_key={country={space=country.id, field=2}}},
                     {name='name', type='string'} }
        local fmt_copy = table.deepcopy(fmt)
        local city = box.schema.create_space('city', {engine=engine, format=fmt})
        -- Check that fmt is not modified by create_space()
        t.assert_equals(fmt_copy, fmt)
        -- Check that format() returns one-based field number
        t.assert_equals(city:format(), fmt)
        city:create_index('pk')

        t.assert_equals(country:select{}, {{1, 'ru', 'Russia'}, {2, 'fr', 'France'}})
        t.assert_error_msg_content_equals(
            "Can't modify space 'country': space is referenced by foreign key",
            box.atomic, country.drop, country
        )
        t.assert_equals(country:select{}, {{1, 'ru', 'Russia'}, {2, 'fr', 'France'}})
        t.assert_error_msg_content_equals(
            "Foreign key constraint 'country' failed for field '2 (country_code)': " ..
            "foreign index was not found",
            function() city:replace{1, 'ru', 'Moscow'} end
        )
        local fmt = {{name='id', type='unsigned'},
                     {name='code', type='string'},
                     {name='name', type='string'}}
        country:format(fmt)
        t.assert_error_msg_content_equals(
            "Foreign key constraint 'country' failed for field '2 (country_code)': " ..
            "foreign index was not found",
            function() city:replace{1, 'ru', 'Moscow'} end
        )
        country:create_index('code', {parts={'code'}})
        t.assert_error_msg_content_equals(
            "Foreign key constraint 'country' failed for field '2 (country_code)': " ..
            "foreign tuple was not found",
            function() city:replace{1, 'de', 'Berlin'} end
        )
        city:replace{1, 'ru', 'Moscow'}
    end, {engine})

    cg.server:restart()

    cg.server:exec(function()
        local city = box.space.city
        city:replace{2, 'ru', 'Tomsk'}
        t.assert_equals(city:select{}, {{1, 'ru', 'Moscow'}, {2, 'ru', 'Tomsk'}})
    end, {engine})

    cg.server:eval('box.snapshot()')
    cg.server:restart()

    cg.server:exec(function()
        local country = box.space.country
        local city = box.space.city
        t.assert_error_msg_content_equals(
            "Foreign key 'country' integrity check failed: index was not found",
            function() country:delete{2} end
        )
        city:create_index('country_code', {parts={'country_code'},unique=false})
        country:delete{2}
        t.assert_error_msg_content_equals(
            "Foreign key 'country' integrity check failed: tuple is referenced",
            function() country:delete{1} end
        )
        city:delete{1}
        city:delete{2}
        country:delete{1}
        city:drop()
        country:drop()
    end, {engine})
end

-- Test with foreign key and different types of indexes and fields.
g.test_foreign_key_wrong_type = function(cg)
    local engine = cg.params.engine
    cg.server:exec(function(engine)
        local fmt = {{'id', 'unsigned'}, {'code','string'}, {'name','string'}}
        local country = box.schema.create_space('country', {engine=engine, format=fmt})
        country:create_index('pk')
        country:create_index('code', {parts={'code'}})
        country:replace{1,'ru','Russia'}

        fmt = {{'id', 'unsigned'},
               {name='country_id',foreign_key={country1={space='country',field='id'}}},
               {name='country_code',foreign_key={country2={space='country',field='code'}}}}
        local city = box.schema.create_space('city', {engine=engine, format=fmt})
        city:create_index('pk')

        t.assert_error_msg_content_equals(
            "Foreign key constraint 'country2' failed for field '3 (country_code)': " ..
            "wrong key type",
            function() city:replace{1,1,1} end
        )
        t.assert_error_msg_content_equals(
            "Foreign key constraint 'country1' failed for field '2 (country_id)': " ..
            "wrong key type",
            function() city:replace{1,'ru','ru'} end
        )
    end, {engine})

    cg.server:restart()

    cg.server:exec(function()
        local city = box.space.city

        t.assert_error_msg_content_equals(
            "Foreign key constraint 'country2' failed for field '3 (country_code)': " ..
            "wrong key type",
            function() city:replace{1,1,1} end
        )
        t.assert_error_msg_content_equals(
            "Foreign key constraint 'country1' failed for field '2 (country_id)': " ..
            "wrong key type",
            function() city:replace{1,'ru','ru'} end
        )
    end, {engine})

    cg.server:eval('box.snapshot()')
    cg.server:restart()

    cg.server:exec(function()
        local city = box.space.city

        t.assert_error_msg_content_equals(
            "Foreign key constraint 'country2' failed for field '3 (country_code)': " ..
            "wrong key type",
            function() city:replace{1,1,1} end
        )
        t.assert_error_msg_content_equals(
            "Foreign key constraint 'country1' failed for field '2 (country_id)': " ..
            "wrong key type",
            function() city:replace{1,'ru','ru'} end
        )
    end, {engine})

    cg.server:exec(function()
        local country = box.space.country
        local city = box.space.city
        city:create_index('wrong1', {parts={{'country_id', 'string'}}, unique=false})
        t.assert_error_msg_content_equals(
            "Foreign key 'country1' integrity check failed: wrong key type",
            function() country:delete{1} end
        )
    end, {engine})

    cg.server:restart()

    cg.server:exec(function()
        local country = box.space.country
        local city = box.space.city
        t.assert_error_msg_content_equals(
            "Foreign key 'country1' integrity check failed: wrong key type",
            function() country:delete{1} end
        )
        city.index.wrong1:drop()
        city:create_index('wrong2', {parts={{'country_code', 'number'}}, unique=false})
        t.assert_error_msg_content_equals(
            "Foreign key 'country1' integrity check failed: index was not found",
            function() country:delete{1} end
        )
    end, {engine})

    cg.server:eval('box.snapshot()')
    cg.server:restart()

    cg.server:exec(function()
        local country = box.space.country
        local city = box.space.city
        t.assert_error_msg_content_equals(
            "Foreign key 'country1' integrity check failed: index was not found",
            function() country:delete{1} end
        )
        city.index.wrong2:drop()
    end, {engine})
end

-- Test with foreign key and different types of indexes and fields.
g.test_foreign_key_overwrite = function(cg)
    local engine = cg.params.engine
    cg.server:exec(function(engine)
        local SIZE = 10000

        local user = box.schema.create_space(
            'user',
            {
                format = {
                    { name='user_id', type='unsigned' },
                    { name='card_id', type='unsigned', is_nullable=true },
                    { name='name', type='string' },
                },
                engine = engine,
            }
        )
        user:create_index('pk')

        local card = box.schema.create_space(
            'card',
            {
                format = {
                    { name='card_id', type='unsigned' },
                    { name='name', type='string' },
                },
                engine = engine,
            }
        )
        card:create_index('pk')

        for i = 1, SIZE do
            user:insert{i, box.null, string.format('user_%05d', i) }
            card:insert{i, string.format('card_%05d', i) }
        end

        print('ALTER table ADD FOREIGN')

        user:format{
            { name='user_id', type='unsigned' },
            {
                name='card_id',
                type='unsigned',
                is_nullable=true,
                foreign_key={
                    card={space='card', field='card_id'}
                },
            },
            { name='name', type='string' },
        }

        print('done')

        print('Fill fields')

        for i = 1, SIZE do
            user:update(i, {{'=', 'card_id', SIZE - i + 1}})
        end

        print('DROP FOREIGN')
        user:format{
            { name='user_id', type='unsigned' },
            {
                name='card_id',
                type='unsigned',
                is_nullable=true,
            },
            { name='name', type='string' },
        }

        print('ALTER table ADD FOREIGN')
        user:format{
            { name='user_id', type='unsigned' },
            {
                name='card_id',
                type='unsigned',
                is_nullable=true,
                foreign_key={
                    card={space='card', field='card_id'}
                },
            },
            { name='name', type='string' },
        }

        print('UPDATE')
        card:update(1, {{'=', 'name', 'foobar'}})

        user:drop()
        card:drop()

    end, {engine})
end

-- Test upsert of a foreign key.
g.test_foreign_key_upsert = function(cg)
    local engine = cg.params.engine
    cg.server:exec(function(engine)
        local card = box.schema.create_space(
            'card',
            {
                engine = engine,
                format = {
                    { name='card_id', type='unsigned' },
                    { name='name', type='string' },
                }
            }
        )
        card:create_index('pk')

        local user = box.schema.create_space(
            'user',
            {
                engine = engine,
                format = {
                    { name='user_id', type='unsigned' },
                    { name='card_id', type='unsigned', is_nullable=true,
                        foreign_key={
                            card={space='card', field='card_id'}
                        },
                    },
                    { name='name', type='string' },
                }
            }
        )
        user:create_index('pk')

        card:replace{1, "hehe"}
        user:replace{1, 1, "haha"}

        t.assert_error_msg_content_equals(
            "Foreign key constraint 'card' failed for field '2 (card_id)': foreign tuple was not found",
            function() user:upsert({1, 1, "haha"}, {{'=', 2, 2}}) end
        )
    end, {engine})

    cg.server:eval('box.snapshot()')

    cg.server:exec(function()
        local user = box.space.user
        t.assert_error_msg_content_equals(
            "Foreign key constraint 'card' failed for field '2 (card_id)': foreign tuple was not found",
            function() user:upsert({1, 1, "haha"}, {{'=', 2, 2}}) end
        )
    end)

    cg.server:eval('box.snapshot()')
    cg.server:restart()

    cg.server:exec(function()
        local user = box.space.user
        t.assert_error_msg_content_equals(
            "Foreign key constraint 'card' failed for field '2 (card_id)': foreign tuple was not found",
            function() user:upsert({1, 1, "haha"}, {{'=', 2, 2}}) end
        )

        box.space.user:drop()
        box.space.card:drop()
    end)
end

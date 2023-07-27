-- https://github.com/tarantool/tarantool/issues/6436 Foreign keys
local server = require('luatest.server')
local t = require('luatest')

local g = t.group('gh-6436-foreign-key-test', {{engine = 'memtx'}, {engine = 'vinyl'}})

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

-- Test with wrong complex foreign key definitions.
g.test_bad_complex_foreign_key = function(cg)
    local engine = cg.params.engine

    cg.server:exec(function(engine)
        local fmt = {{'planet_id','unsigned'}, {'country_id','unsigned'}, {'name'}}
        local country = box.schema.create_space('country', {engine=engine, format=fmt})
        country:create_index('pk', {parts={{'planet_id'},{'country_id'}}})
        local fmt = {{'city_id'}, {'p_id'}, {'c_id'}}
        local function space_opts(foreign_key)
            return {engine=engine, format=fmt, foreign_key=foreign_key}
        end
        local opts = space_opts({space=false,field={}})
        t.assert_error_msg_content_equals(
            "Illegal parameters, foreign key: space must be string or number",
            function() box.schema.create_space('city', opts) end
        )
        local opts = space_opts({space='country',field='country_id'})
        t.assert_error_msg_content_equals(
            "Illegal parameters, foreign key: field must be a table with local field -> foreign field mapping",
            function() box.schema.create_space('city', opts) end
        )
        opts = space_opts({space='country',field={}})
        t.assert_error_msg_content_equals(
            "Illegal parameters, foreign key: field must be a table with local field -> foreign field mapping",
            function() box.schema.create_space('city', opts) end
        )
        opts = space_opts({space='country',field={[false]='country_id'}})
        t.assert_error_msg_content_equals(
            "Illegal parameters, foreign key: local field must be string or number",
            function() box.schema.create_space('city', opts) end
        )
        opts = space_opts({space='country',field={c_id=false}})
        t.assert_error_msg_content_equals(
            "Illegal parameters, foreign key: foreign field must be string or number",
            function() box.schema.create_space('city', opts) end
        )
        opts = space_opts({[string.rep('a', 66666)]={space='country',field={p_id='planet_id', c_id='country_id'}}})
        t.assert_error_msg_content_equals(
            "Wrong space options: foreign key name is too long",
            function() box.schema.create_space('city', opts) end
        )
        opts = space_opts({['']={space='country',field={p_id='planet_id', c_id='country_id'}}})
        t.assert_error_msg_content_equals(
            "Wrong space options: foreign key name isn't a valid identifier",
            function() box.schema.create_space('city', opts) end
        )
        opts = space_opts({cntr={space='country',field={p_id='planet_id', c_id='country_id'}}})
        box.schema.create_space('city', opts)
        t.assert_equals(box.space.city.foreign_key,
            { cntr = {field = {c_id = "country_id", p_id = "planet_id"}, space = country.id} }
        )
    end, {engine})
end

-- Test with complex foreign key by primary index.
g.test_complex_foreign_key_primary = function(cg)
    local engine = cg.params.engine

    cg.server:exec(function(engine)
        local country = box.schema.create_space('country', {engine=engine})
        country:create_index('pk', {parts={{1},{2}}})
        country:replace{1, 11, 'Russia'}
        country:replace{1, 12, 'France'}

        local function city_space_opts(foreign_key)
            local fmt = {{name='id', type='unsigned'},
                         {name='p_id', type='unsigned'},
                         {name='c_id', type='unsigned'}}
            return {engine=engine, format=fmt, foreign_key=foreign_key}
        end

        local fkey = {space='country',field={p_id='planet_id', c_id='country_id'}}
        local city = box.schema.create_space('city', city_space_opts(fkey))
        -- Note that the foreign_key was normalized
        t.assert_equals(box.space.city.foreign_key,
            { country = {field = {c_id = "country_id", p_id = "planet_id"}, space = country.id} }
        )
        city:create_index('pk')

        t.assert_equals(country:select{}, {{1, 11, 'Russia'}, {1, 12, 'France'}})
        t.assert_error_msg_content_equals(
            "Can't modify space 'country': space is referenced by foreign key",
            country.drop, country
        )
        t.assert_error_msg_content_equals(
            "Foreign key 'country' integrity check failed: wrong foreign field name",
            function() country:delete{1, 11} end
        )
        t.assert_equals(country:select{}, {{1, 11, 'Russia'}, {1, 12, 'France'}})
        t.assert_error_msg_content_equals(
            "Foreign key constraint 'country' failed: foreign index was not found",
            function() city:replace{1, 1, 11, 'Moscow'} end
        )
        local fmt = {{'planet_id','unsigned'}, {'country_id','unsigned'}, {'name'}}
        country:format(fmt)
        t.assert_error_msg_content_equals(
            "Foreign key constraint 'country' failed: foreign tuple was not found",
            function() city:replace{1, 1, 500, 'Moscow'} end
        )
        city:replace{21, 1, 11, 'Moscow'}
    end, {engine})

    cg.server:restart()

    cg.server:exec(function()
        local city = box.space.city
        city:replace{22, 1, 11, 'Tomsk'}
        t.assert_equals(city:select{}, {{21, 1, 11, 'Moscow'}, {22, 1, 11, 'Tomsk'}})
    end, {engine})

    cg.server:eval('box.snapshot()')
    cg.server:restart()

    cg.server:exec(function()
        local country = box.space.country
        local city = box.space.city
        t.assert_error_msg_content_equals(
            "Foreign key 'country' integrity check failed: index was not found",
            function() country:delete{1, 12} end
        )
        city:create_index('country', {parts={{'p_id'},{'c_id'}},unique=false})
        country:delete{1, 12}
        t.assert_error_msg_content_equals(
            "Foreign key 'country' integrity check failed: tuple is referenced",
            function() country:delete{1, 11} end
        )
        city:delete{21}
        city:delete{22}
        country:delete{1, 11}
        city:drop()
        country:drop()
    end, {engine})
end

-- Test with foreign key by secondary index and some variations.
g.test_complex_foreign_key_secondary = function(cg)
    local engine = cg.params.engine

    cg.server:exec(function(engine)
        local country_fmt = {{name='id', type='unsigned'},
                             {name='universe_id', type='unsigned'},
                             {name='planet_name', type='string'},
                             {name='country_code', type='string'},
                             {name='name', type='string'}}
        --Note: format is not set.
        local country = box.schema.create_space('country', {engine=engine})
        country:create_index('pk')
        country:replace{100, 1, 'earth', 'ru', 'Russia'}
        country:replace{101, 1, 'earth', 'rf', 'France'}

        local function city_space_opts(foreign_key)
            local fmt = {{name='id', type='unsigned'},
                         {name='p', type='string'},
                         {name='u', type='unsigned'},
                         {name='c', type='string'},
                         {name='name', type='string'}}
            return {engine=engine, format=fmt, foreign_key=foreign_key}
        end
        local fkey = {cntr = {space='country',
                              field={c='country_code',
                                     u='universe_id',
                                     p='planet_name'}}}
        local city = box.schema.create_space('city', city_space_opts(fkey))
        fkey.cntr.space = country.id
        t.assert_equals(city.foreign_key, fkey);
        city:create_index('pk')

        t.assert_equals(country:select{}, {{100, 1, 'earth', 'ru', 'Russia'},
                                           {101, 1, 'earth', 'rf', 'France'}})
        t.assert_error_msg_content_equals(
            "Can't modify space 'country': space is referenced by foreign key",
            country.drop, country
        )
        t.assert_equals(country:select{}, {{100, 1, 'earth', 'ru', 'Russia'},
                                           {101, 1, 'earth', 'rf', 'France'}})
        t.assert_error_msg_content_equals(
            "Foreign key constraint 'cntr' failed: foreign index was not found",
            function() city:replace{21, 'earth', 1, 'ru', 'Moscow'} end
        )
        country:format(country_fmt)
        t.assert_error_msg_content_equals(
            "Foreign key constraint 'cntr' failed: foreign index was not found",
            function() city:replace{21, 'earth', 1, 'ru', 'Moscow'} end
        )
        country:create_index('name1', {parts={{'universe_id'}},
                                       unique=false})
        country:create_index('name2', {parts={{'country_code'},
                                              {'universe_id'}},
                                       unique=false})
        country:create_index('name3', {parts={{'planet_name'},
                                              {'country_code'},
                                              {'universe_id'}}})
        t.assert_error_msg_content_equals(
            "Foreign key constraint 'cntr' failed: foreign tuple was not found",
            function() city:replace{21, 'earth', 1, 'de', 'Berlin'} end
        )
        city:replace{21, 'earth', 1, 'ru', 'Moscow'}
    end, {engine})

    cg.server:restart()

    cg.server:exec(function()
        local city = box.space.city
        city:replace{22, 'earth', 1, 'ru', 'Tomsk'}
        t.assert_equals(city:select{}, {{21, 'earth', 1, 'ru', 'Moscow'},
                                        {22, 'earth', 1, 'ru', 'Tomsk'}})
    end, {engine})

    cg.server:eval('box.snapshot()')
    cg.server:restart()

    cg.server:exec(function()
        local country = box.space.country
        local city = box.space.city
        t.assert_error_msg_content_equals(
            "Foreign key 'cntr' integrity check failed: index was not found",
            function() country:delete{100} end
        )
        city:create_index('name', {parts={{'u'},{'c'},{'p'}},unique=false})
        country:delete{101}
        t.assert_error_msg_content_equals(
            "Foreign key 'cntr' integrity check failed: tuple is referenced",
            function() country:delete{100} end
        )
        city:delete{21}
        city:delete{22}
        country:delete{100}
        city:drop()
        country:drop()
    end, {engine})
end

-- The same test as above but with foreign key by numeric space and field.
g.test_complex_foreign_key_numeric = function(cg)
    local engine = cg.params.engine

    cg.server:exec(function(engine)
        local country_fmt = {{name='id', type='unsigned'},
                             {name='universe_id', type='unsigned'},
                             {name='planet_name', type='string'},
                             {name='country_code', type='string'},
                             {name='name', type='string'}}
        --Note: format is not set.
        local country = box.schema.create_space('country', {engine=engine})
        country:create_index('pk')
        country:replace{100, 1, 'earth', 'ru', 'Russia'}
        country:replace{101, 1, 'earth', 'rf', 'France'}

        local function city_space_opts(foreign_key)
            local fmt = {{name='id', type='unsigned'},
                         {name='p', type='string'},
                         {name='u', type='unsigned'},
                         {name='c', type='string'},
                         {name='name', type='string'}}
            return {engine=engine, format=fmt, foreign_key=foreign_key}
        end
        local fkey = {cntr = {space=country.id,
                              field={[4]=4, [3]=2, [2]=3}}}
        local city = box.schema.create_space('city', city_space_opts(fkey))
        t.assert_equals(city.foreign_key, fkey)
        city:create_index('pk')

        t.assert_equals(country:select{}, {{100, 1, 'earth', 'ru', 'Russia'},
                                           {101, 1, 'earth', 'rf', 'France'}})
        t.assert_error_msg_content_equals(
            "Can't modify space 'country': space is referenced by foreign key",
            country.drop, country
        )
        t.assert_equals(country:select{}, {{100, 1, 'earth', 'ru', 'Russia'},
                                           {101, 1, 'earth', 'rf', 'France'}})
        t.assert_error_msg_content_equals(
            "Foreign key constraint 'cntr' failed: foreign index was not found",
            function() city:replace{21, 'earth', 1, 'ru', 'Moscow'} end
        )
        country:format(country_fmt)
        t.assert_error_msg_content_equals(
            "Foreign key constraint 'cntr' failed: foreign index was not found",
            function() city:replace{21, 'earth', 1, 'ru', 'Moscow'} end
        )
        country:create_index('name1', {parts={{'universe_id'}},
                                       unique=false})
        country:create_index('name2', {parts={{'country_code'},
                                              {'universe_id'}},
                                       unique=false})
        country:create_index('name3', {parts={{'planet_name'},
                                              {'country_code'},
                                              {'universe_id'}}})
        t.assert_error_msg_content_equals(
            "Foreign key constraint 'cntr' failed: foreign tuple was not found",
            function() city:replace{21, 'earth', 1, 'de', 'Berlin'} end
        )
        city:replace{21, 'earth', 1, 'ru', 'Moscow'}
    end, {engine})

    cg.server:restart()

    cg.server:exec(function()
        local city = box.space.city
        city:replace{22, 'earth', 1, 'ru', 'Tomsk'}
        t.assert_equals(city:select{}, {{21, 'earth', 1, 'ru', 'Moscow'},
                                        {22, 'earth', 1, 'ru', 'Tomsk'}})
    end, {engine})

    cg.server:eval('box.snapshot()')
    cg.server:restart()

    cg.server:exec(function()
        local country = box.space.country
        local city = box.space.city
        t.assert_error_msg_content_equals(
            "Foreign key 'cntr' integrity check failed: index was not found",
            function() country:delete{100} end
        )
        city:create_index('name', {parts={{'u'},{'c'},{'p'}},unique=false})
        country:delete{101}
        t.assert_error_msg_content_equals(
            "Foreign key 'cntr' integrity check failed: tuple is referenced",
            function() country:delete{100} end
        )
        city:delete{21}
        city:delete{22}
        country:delete{100}
        city:drop()
        country:drop()
    end, {engine})
end

-- Test with foreign key and different types of indexes and fields.
g.test_complex_foreign_key_wrong_type = function(cg)
    local engine = cg.params.engine
    cg.server:exec(function(engine)
        local fmt = {{'id', 'unsigned'}, {'planet_id','unsigned'},
                     {'code','string'}, {'name','string'}}
        local country = box.schema.create_space('country', {engine=engine, format=fmt})
        country:create_index('pk')
        country:create_index('code', {parts={{'planet_id'},{'code'}}})
        country:replace{100, 1, 'ru','Russia'}

        local function city_space_opts(foreign_key)
            local fmt = {{'id', 'unsigned'}, {'planet_id'}, {'country_code'}}
            return {engine=engine, format=fmt, foreign_key=foreign_key}
        end
        local fkey = {space='country',field={planet_id='planet_id',country_code='code'}}
        local city = box.schema.create_space('city', city_space_opts(fkey))
        city:create_index('pk')

        t.assert_error_msg_content_equals(
            "Foreign key constraint 'country' failed: wrong key type",
            function() city:replace{1, 1, 1} end
        )
        t.assert_error_msg_content_equals(
            "Foreign key constraint 'country' failed: wrong key type",
            function() city:replace{1,'ru','ru'} end
        )
    end, {engine})

    cg.server:restart()

    cg.server:exec(function()
        local city = box.space.city

        t.assert_error_msg_content_equals(
            "Foreign key constraint 'country' failed: wrong key type",
            function() city:replace{1, 1, 1} end
        )
        t.assert_error_msg_content_equals(
            "Foreign key constraint 'country' failed: wrong key type",
            function() city:replace{1,'ru','ru'} end
        )
    end, {engine})

    cg.server:eval('box.snapshot()')
    cg.server:restart()

    cg.server:exec(function()
        local city = box.space.city

        t.assert_error_msg_content_equals(
            "Foreign key constraint 'country' failed: wrong key type",
            function() city:replace{1, 1, 1} end
        )
        t.assert_error_msg_content_equals(
            "Foreign key constraint 'country' failed: wrong key type",
            function() city:replace{1,'ru','ru'} end
        )
    end, {engine})

    cg.server:exec(function()
        local country = box.space.country
        local city = box.space.city
        city:create_index('wrong1', {parts={{'country_code', 'unsigned'},{'planet_id', 'unsigned'}}, unique=false})
        t.assert_error_msg_content_equals(
            "Foreign key 'country' integrity check failed: wrong key type",
            function() country:delete{100} end
        )
    end, {engine})

    cg.server:restart()

    cg.server:exec(function()
        local country = box.space.country
        local city = box.space.city
        t.assert_error_msg_content_equals(
            "Foreign key 'country' integrity check failed: wrong key type",
            function() country:delete{100} end
        )
        city.index.wrong1:drop()
        city:create_index('wrong2', {parts={{'country_code', 'string'},{'planet_id', 'string'}}, unique=false})
        t.assert_error_msg_content_equals(
            "Foreign key 'country' integrity check failed: wrong key type",
            function() country:delete{100} end
        )
    end, {engine})

    cg.server:eval('box.snapshot()')
    cg.server:restart()

    cg.server:exec(function()
        local country = box.space.country
        local city = box.space.city
        t.assert_error_msg_content_equals(
            "Foreign key 'country' integrity check failed: wrong key type",
            function() country:delete{100} end
        )
        city.index.wrong2:drop()
    end, {engine})
end

-- Test upsert of a comple foreign key.
g.test_complex_foreign_key_upsert = function(cg)
    local engine = cg.params.engine
    cg.server:exec(function(engine)
        local card = box.schema.create_space(
            'card',
            {
                engine = engine,
                format = {
                    { name='card_id1', type='unsigned' },
                    { name='card_id2', type='unsigned' },
                    { name='name', type='string' },
                }
            }
        )
        card:create_index('pk', {parts = {'card_id1', 'card_id2'}})

        local user = box.schema.create_space(
            'user',
            {
                engine = engine,
                format = {
                    { name='user_id', type='unsigned' },
                    { name='card_id1', type='unsigned', is_nullable=true },
                    { name='card_id2', type='unsigned', is_nullable=true },
                    { name='name', type='string' },
                },
                foreign_key = { space = 'card',
                                field = { card_id1 = 'card_id1',
                                          card_id2 = 'card_id2' } }
            }
        )
        user:create_index('pk')

        card:replace{1, 1, "hehe"}
        user:replace{1, 1, 1, "haha"}

        t.assert_error_msg_content_equals(
            "Foreign key constraint 'card' failed: foreign tuple was not found",
            function() user:upsert({1, 1, 1, "haha"}, {{'=', 2, 2}}) end
        )
    end, {engine})

    cg.server:eval('box.snapshot()')

    cg.server:exec(function()
        local user = box.space.user
        t.assert_error_msg_content_equals(
            "Foreign key constraint 'card' failed: foreign tuple was not found",
            function() user:upsert({1, 1, 1, "haha"}, {{'=', 2, 2}}) end
        )
    end)

    cg.server:eval('box.snapshot()')
    cg.server:restart()

    cg.server:exec(function()
        local user = box.space.user
        t.assert_error_msg_content_equals(
            "Foreign key constraint 'card' failed: foreign tuple was not found",
            function() user:upsert({1, 1, 1, "haha"}, {{'=', 2, 2}}) end
        )

        box.space.user:drop()
        box.space.card:drop()
    end)
end

-- https://github.com/tarantool/tarantool/issues/8936
-- Test foreign keys to temporary and local spaces.
local server = require('luatest.server')
local t = require('luatest')

-- The test creates two spaces - 'country' and 'city' which are linked with
-- foreign key - city has country_id field that is linked to country space.
local test_opts = t.helpers.matrix{
    engine = {'memtx', 'vinyl'},
    -- Value of country space option 'temporary' or 'is_local' (depending on
    -- test case).
    country_variant = {false, true},
    -- Value of city space option 'temporary' or 'is_local' (depending on
    -- test case).
    city_variant = {false, true},
}
local g = t.group('gh-8936-foreign-key-wrong-reference-test', test_opts)

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:stop()
    cg.server = nil
end)

g.after_each(function(cg)
    cg.server:exec(function()
        if box.space.city then
            box.space.city:drop()
        end
        if box.space.country then
            box.space.country:drop()
        end
    end)
end)

-- Foreign key must not refer to temporary space from normal space.
g.test_field_foreign_key_temporary = function(cg)
    local engine = cg.params.engine
    local country_is_temporary = cg.params.country_variant
    local city_is_temporary = cg.params.city_variant
    -- vinyl space can't be temporary.
    t.skip_if(engine == 'vinyl')

    cg.server:exec(function(engine, country_is_temporary, city_is_temporary)
        -- foreign key must not point for non-temporary to temporary space.
        local must_be_prohibited =
            country_is_temporary and not city_is_temporary

        local country_fmt = {
            {name = 'id', type = 'unsigned'},
            {name = 'name', type = 'string'},
        }
        local country_opts = {engine = engine, format = country_fmt,
                              temporary = country_is_temporary}

        local city_fmt = {
            {name = 'id', type = 'unsigned'},
            {name = 'country_id', type = 'unsigned',
                foreign_key = {space = 'country', field = 'id'}},
            {name = 'name', type = 'string'},
        }
        local city_opts = {engine = engine, format = city_fmt,
                           temporary = city_is_temporary}

        local country = box.schema.create_space('country', country_opts)
        country:create_index('pk')

        if must_be_prohibited then
            t.assert_error_msg_content_equals(
                "Failed to create foreign key 'country' in space 'city': " ..
                "foreign key from non-temporary space " ..
                "can't refer to temporary space",
                box.schema.create_space, 'city', city_opts
            )
            return nil
        end

        local city = box.schema.create_space('city', city_opts)
        city:create_index('pk')

        -- Alter to wrong state must be prohibited while all other alters
        -- must be allowed.
        if country_is_temporary and city_is_temporary then
            t.assert_error_msg_content_equals(
                "Failed to create foreign key 'country' in space 'city': " ..
                "foreign key from non-temporary space " ..
                "can't refer to temporary space",
                city.alter, city, {temporary = false}
            )
            country:alter{temporary = false}
            country:alter{temporary = true}
        end
        if not country_is_temporary and not city_is_temporary then
            t.assert_error_msg_content_equals(
                "Can't modify space 'country': foreign key 'country' from " ..
                "non-temporary space 'city' can't refer to temporary space",
                country.alter, country, {temporary = true}
            )
            city:alter{temporary = true}
            city:alter{temporary = false}
        end
        t.assert_equals(country_is_temporary, country.temporary)
        t.assert_equals(city_is_temporary, city.temporary)

        -- Check that foreign key still works as expected
        t.assert_error_msg_content_equals(
            "Foreign key constraint 'country' failed for field " ..
             "'2 (country_id)': foreign tuple was not found",
            city.replace, city, {1, 1, 'msk'}
        )
        country:replace{1, 'ru'}
        city:replace{1, 1, 'msk'}
        t.assert_error_msg_content_equals(
            "Foreign key 'country' integrity check failed: index was not found",
            country.delete, country, {1}
        )
        city:create_index('sk', {parts = {{'country_id'}}, unique = false})
        t.assert_error_msg_content_equals(
            "Foreign key 'country' integrity check failed: tuple is referenced",
            country.delete, country, {1}
        )
        t.assert_error_msg_content_equals(
            "Can't modify space 'country': space is referenced by foreign key",
            country.truncate, country
        )
        t.assert_error_msg_content_equals(
            "Can't modify space 'country': space is referenced by foreign key",
            country.drop, country
        )

    end, {engine, country_is_temporary, city_is_temporary})
end

-- Foreign key must not refer to local space from non-local space.
g.test_field_foreign_key_local = function(cg)
    local engine = cg.params.engine
    local country_is_local = cg.params.country_variant
    local city_is_local = cg.params.city_variant

    cg.server:exec(function(engine, country_is_local, city_is_local)
        -- foreign key must not point for non-temporary to temporary space.
        local must_be_prohibited =
            country_is_local and not city_is_local

        local country_fmt = {
            {name = 'id', type = 'unsigned'},
            {name = 'name', type = 'string'},
        }
        local country_opts = {engine = engine, format = country_fmt,
                              is_local = country_is_local}

        local city_fmt = {
            {name = 'id', type = 'unsigned'},
            {name = 'country_id', type = 'unsigned',
                foreign_key = {space = 'country', field = 'id'}},
            {name = 'name', type = 'string'},
        }
        local city_opts = {engine = engine, format = city_fmt,
                           is_local = city_is_local}

        local country = box.schema.create_space('country', country_opts)
        country:create_index('pk')

        if must_be_prohibited then
            t.assert_error_msg_content_equals(
                "Failed to create foreign key 'country' in space 'city': " ..
                "foreign key from non-local space can't refer to local space",
                box.schema.create_space, 'city', city_opts
            )
            return nil
        end

        local city = box.schema.create_space('city', city_opts)
        city:create_index('pk')

        -- Alter of 'is_local' should be prohibited in any case.
        t.assert_error_msg_content_equals(
            "Illegal parameters, unexpected option 'is_local'",
            country.alter, country, {is_local = not country_is_local}
        )
        t.assert_error_msg_content_equals(
            "Illegal parameters, unexpected option 'is_local'",
            city.alter, city, {is_local = not city_is_local}
        )

        -- Check that foreign key still works as expected
        t.assert_error_msg_content_equals(
            "Foreign key constraint 'country' failed for field " ..
             "'2 (country_id)': foreign tuple was not found",
            city.replace, city, {1, 1, 'msk'}
        )
        country:replace{1, 'ru'}
        city:replace{1, 1, 'msk'}
        t.assert_error_msg_content_equals(
            "Foreign key 'country' integrity check failed: index was not found",
            country.delete, country, {1}
        )
        city:create_index('sk', {parts = {{'country_id'}}, unique = false})
        t.assert_error_msg_content_equals(
            "Foreign key 'country' integrity check failed: tuple is referenced",
            country.delete, country, {1}
        )
        t.assert_error_msg_content_equals(
            "Can't modify space 'country': space is referenced by foreign key",
            country.truncate, country
        )
        t.assert_error_msg_content_equals(
            "Can't modify space 'country': space is referenced by foreign key",
            country.drop, country
        )

    end, {engine, country_is_local, city_is_local})
end

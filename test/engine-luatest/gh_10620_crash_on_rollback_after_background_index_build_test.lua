local server = require('luatest.server')
local t = require('luatest')

local g = t.group(nil, {{engine = 'memtx'}, {engine = 'vinyl'}})

g.before_each(function(cg)
    cg.server = server:new()
    cg.server:start()
end)

g.after_each(function(cg)
    cg.server:drop()
end)

g.test_crash_on_wal_failure = function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server:exec(function(engine)
        local fiber = require('fiber')
        local space = box.schema.space.create('test', {engine = engine})
        space:create_index('pk')
        for i = 1, 100 do
            space:replace({i, i})
        end
        local ch = fiber.channel(1)
        fiber.create(function()
            box.begin()
            space:create_index('sk')
            ch:put(true)
            box.error.injection.set('ERRINJ_WAL_DELAY', true)
            box.commit()
        end)
        fiber.create(function()
            box.begin()
            for i = 1, 100 do
                space:replace{i, i}
            end
            ch:put(true)
            box.commit()
        end)
        ch:get()
        ch:get()
        box.error.injection.set('ERRINJ_WAL_WRITE', true)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
    end, {cg.params.engine})
end

-- Cover the case with manual rollback. Bump the space cache version
-- in order to check that implementation doesn't rely on it.
g.test_manual_rollback = function(cg)
    cg.server:exec(function(engine)
        local fiber = require('fiber')
        local space = box.schema.space.create('test', {engine = engine})
        space:create_index('pk')
        for i = 1, 100 do
            space:replace({i, i})
        end
        local select_result = space:select{}
        local ddl = fiber.create(function()
            box.begin()
            space:create_index('sk')
            box.commit()
        end)
        ddl:set_joinable(true)
        fiber.create(function()
            box.begin()
            for i = 1, 100 do
                space:replace{i, i + 100}
            end
            -- Bump space cache version
            box.schema.space.create('test2')
            box.rollback()
        end)
        local ok, err = ddl:join()
        t.assert(ok, err)
        t.assert_equals(space.index.sk:select{}, select_result)
    end, {cg.params.engine})
end

-- A delete skipped by the background index build is rolled back after the
-- build has passed its key, so the index being built misses the tuple. A
-- delete of the tuple from the built range must not fail then.
g.test_delete_tuple_missing_from_built_index = function(cg)
    t.tarantool.skip_if_not_debug()
    t.skip_if(cg.params.engine ~= 'memtx', 'bitset and rtree are memtx only')
    cg.server:exec(function()
        local fiber = require('fiber')
        local errinj = box.error.injection
        for _, type in ipairs({'bitset', 'rtree'}) do
            local s = box.schema.space.create('test')
            s:create_index('pk')
            local function tuple(i)
                return {i, type == 'bitset' and i % 7 or {i % 7, i % 5}}
            end
            for i = 1, 2000 do
                s:replace(tuple(i))
            end
            local builder = fiber.new(function()
                local parts = type == 'bitset' and {{2, 'unsigned'}} or
                              {{2, 'array'}}
                s:create_index('sk', {type = type, parts = parts,
                                      unique = false})
            end)
            builder:set_joinable(true)
            -- Start the background index build.
            fiber.yield()

            -- The tuple is ahead of the build cursor, so the delete is
            -- skipped.
            errinj.set('ERRINJ_WAL_DELAY', true)
            local deleter = fiber.new(function()
                t.assert_error_msg_content_equals('Failed to write to disk',
                                                  s.delete, s, {1000})
            end)
            deleter:set_joinable(true)
            fiber.yield()
            -- Let the build pass the key, but not complete.
            for _ = 1, 150 do
                fiber.yield()
            end
            errinj.set('ERRINJ_BUILD_INDEX_DELAY', true)
            errinj.set('ERRINJ_WAL_WRITE', true)
            errinj.set('ERRINJ_WAL_DELAY', false)
            deleter:join()
            errinj.set('ERRINJ_WAL_WRITE', false)

            s:delete{1000}
            errinj.set('ERRINJ_BUILD_INDEX_DELAY', false)
            local ok, err = builder:join()
            t.assert(ok, err)
            t.assert_equals(s.index.sk:count(), s.index.pk:count())
            s:drop()
        end
    end)
end

-- Same as the previous test, but with a bitset index whose tuple hash is large
-- enough to be allocated with mmap(), so the memory past its end is zeroed. A
-- delete of a tuple missing from the index used to read the value 0 there and
-- delete the first tuple put into the index (gh-13487).
g.test_bitset_delete_of_missing_tuple_keeps_others = function(cg)
    t.tarantool.skip_if_not_debug()
    t.skip_if(cg.params.engine ~= 'memtx', 'bitset is memtx only')
    cg.server:exec(function()
        local fiber = require('fiber')
        local errinj = box.error.injection
        fiber.self():set_max_slice(60)
        local s = box.schema.space.create('test')
        s:create_index('pk')
        box.begin()
        for i = 1, 150000 do
            s:replace{i, i % 7}
        end
        box.commit()
        local builder = fiber.new(function()
            s:create_index('sk', {type = 'bitset', parts = {{2, 'unsigned'}},
                                  unique = false})
        end)
        builder:set_joinable(true)
        -- Start the background index build.
        fiber.yield()

        -- The tuple is ahead of the build cursor, so the delete is skipped.
        errinj.set('ERRINJ_WAL_DELAY', true)
        local deleter = fiber.new(function()
            t.assert_error_msg_content_equals('Failed to write to disk',
                                              s.delete, s, {100000})
        end)
        deleter:set_joinable(true)
        fiber.yield()
        -- Let the build pass the key, but not complete: a debug build inserts
        -- 10 tuples per yield.
        for _ = 1, 10500 do
            fiber.yield()
        end
        errinj.set('ERRINJ_BUILD_INDEX_DELAY', true)
        errinj.set('ERRINJ_WAL_WRITE', true)
        errinj.set('ERRINJ_WAL_DELAY', false)
        deleter:join()
        errinj.set('ERRINJ_WAL_WRITE', false)

        s:delete{100000}
        errinj.set('ERRINJ_BUILD_INDEX_DELAY', false)
        local ok, err = builder:join()
        t.assert(ok, err)
        t.assert_equals(s.index.sk:count(), s.index.pk:count())
        local found = false
        for _, tuple in s.index.sk:pairs({1}, {iterator = 'BITS_ALL_SET'}) do
            if tuple[1] == 1 then
                found = true
            end
        end
        t.assert(found)
        s:drop()
    end)
end

-- A replace removing a key from the built range of a unique index lets the
-- background index build insert another tuple with the key. A rollback of
-- the replace must not overwrite that tuple in the index.
g.test_rollback_of_excluded_key_replace = function(cg)
    t.tarantool.skip_if_not_debug()
    t.skip_if(cg.params.engine ~= 'memtx', 'the build delay is memtx only')
    cg.server:exec(function()
        local fiber = require('fiber')
        local errinj = box.error.injection
        local s = box.schema.space.create('test')
        s:create_index('pk')
        s:replace{1, 10}
        for i = 2, 2000 do
            s:replace{i, i == 1000 and 10 or 1000 + i}
        end
        local builder = fiber.new(function()
            s:create_index('sk', {
                parts = {{2, 'unsigned', is_nullable = true,
                          exclude_null = true}},
            })
        end)
        builder:set_joinable(true)
        -- Start the background index build.
        fiber.yield()

        -- The tuple is in the built range, so the key is removed from it.
        errinj.set('ERRINJ_WAL_DELAY', true)
        local replacer = fiber.new(function()
            t.assert_error_msg_content_equals('Failed to write to disk',
                                              s.replace, s, {1, box.NULL})
        end)
        replacer:set_joinable(true)
        fiber.yield()
        -- Let the build insert the other tuple with the key.
        for _ = 1, 150 do
            fiber.yield()
        end
        errinj.set('ERRINJ_BUILD_INDEX_DELAY', true)
        errinj.set('ERRINJ_WAL_WRITE', true)
        errinj.set('ERRINJ_WAL_DELAY', false)
        replacer:join()
        errinj.set('ERRINJ_WAL_WRITE', false)

        errinj.set('ERRINJ_BUILD_INDEX_DELAY', false)
        local ok, err = builder:join()
        t.assert_not(ok)
        t.assert_equals(err.message,
                        'Duplicate key exists in unique index "sk" in ' ..
                        'space "test" with old tuple - [1000, 10] and ' ..
                        'new tuple - [1, 10]')
        t.assert_equals(s.index.sk, nil)
    end)
end

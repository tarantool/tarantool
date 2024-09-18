local t = require('luatest')
local server = require('luatest.server')

local g = t.group(nil, {{engine = 'memtx'}, {engine = 'vinyl'}})

g.before_each(function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server = server:new()
    cg.server:start()
end)

g.after_each(function(cg)
    cg.server:drop()
end)

-- Reproducer from the issue.
g.test_crash_on_consequent_ddl = function(cg)
    cg.server:exec(function(engine)
        local fiber = require('fiber')
        local space = box.schema.space.create('test', {engine = engine})
        space:create_index('pk')
        for i = 1, 100 do
            space:replace({i, 2})
        end
        local ch = fiber.channel(1)
        fiber.create(function()
            box.begin()
            space:create_index('sk')
            -- Inject WAL error only after index is built in order
            -- to allow vinyl use WAL during the index build
            box.error.injection.set('ERRINJ_WAL_WRITE', true)
            ch:put(true)
            box.commit()
        end)
        t.assert(ch:get(10))
        local function alter_index()
            space:format({{'one', 'unsigned'}, {'two', 'unsigned'}})
        end
        t.assert_error_msg_content_equals(
            "Can't modify space 'test': the space is already being modified",
            alter_index)
    end, {cg.params.engine})
end

-- The test checks if all yielding DDL operations are disallowed while there
-- is another space modification being committed.
g.test_disallow_yielding_ddl_on_not_committed_space = function(cg)
    cg.server:exec(function(engine)
        local fiber = require('fiber')

        local s = box.schema.space.create('foo', {engine = engine})
        s:create_index('pk')
        for i = 1, 100 do
            s:insert({i, i * 10})
        end

        box.error.injection.set('ERRINJ_WAL_DELAY', true)
        local f = fiber.create(s.rename, s, 'bar')
        f:set_joinable(true)

        t.assert_error_msg_content_equals(
            "Can't modify space 'bar': the space is already being modified",
            s.create_index, s, 'sk')
        t.assert_error_msg_content_equals(
            "Can't modify space 'bar': the space is already being modified",
            s.format, s, {{'field1', 'unsigned'}})

        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        local ok = f:join()
        t.assert(ok)
    end, {cg.params.engine})
end

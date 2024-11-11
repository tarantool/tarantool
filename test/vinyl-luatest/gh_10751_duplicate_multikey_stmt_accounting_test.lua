local server = require('luatest.server')
local t = require('luatest')

local g = t.group('vinyl.duplicate_multikey_stmt_accounting',
                  t.helpers.matrix{defer_deletes = {false, true}})

g.before_all(function(cg)
    cg.server = server:new({
        box_cfg = {vinyl_defer_deletes = cg.params.defer_deletes},
    })
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.before_each(function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('test', {engine = 'vinyl'})
        s:create_index('primary')
        s:create_index('secondary', {
            unique = false,
            parts = {{'[2][*]', 'unsigned'}},
        })
    end)
end)

g.after_each(function(cg)
    cg.server:exec(function()
        if box.space.test ~= nil then
            box.space.test:drop()
        end
    end)
end)

g.test_commit = function(cg)
    cg.server:exec(function()
        local s = box.space.test
        local i = s.index.secondary
        s:replace({1, {1, 1}})
        s:delete({1})
        s:replace({1, {1, 1}})
        box.snapshot()
        t.assert_covers(i:stat(), {
            memory = {rows = 0, bytes = 0},
            disk = {rows = 1},
        })
        t.assert_equals(i:len(), 1)
    end)
end

g.test_rollback = function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server:exec(function()
        local s = box.space.test
        local i = s.index.secondary
        s:replace({1, {1, 1}})
        box.begin()
        s:delete({1})
        box.error.injection.set('ERRINJ_WAL_WRITE', true)
        t.assert_error_covers({
            type = 'ClientError',
            code = box.error.WAL_IO,
        }, box.commit)
        box.error.injection.set('ERRINJ_WAL_WRITE', false)
        box.snapshot()
        t.assert_covers(i:stat(), {
            memory = {rows = 0, bytes = 0},
            disk = {rows = 1},
        })
        t.assert_equals(i:len(), 1)
    end)
end

g.after_test('test_rollback', function(cg)
    cg.server:exec(function()
        box.error.injection.set('ERRINJ_WAL_WRITE', false)
    end)
end)

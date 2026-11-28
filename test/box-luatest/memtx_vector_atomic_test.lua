local server = require('luatest.server')
local t = require('luatest')

local g = t.group('memtx_vector_atomic', {
    {mvcc = false},
    {mvcc = true},
})

g.before_all(function(cg)
    cg.server = server:new({box_cfg = {
        memtx_use_mvcc_engine = cg.params.mvcc,
    }})
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.before_each(function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('vector_atomic')
        s:format({{name = 'id', type = 'unsigned'},
                  {name = 'vec', type = 'embedding'},
                  {name = 'tag', type = 'string'}})
        s:create_index('pk')
        s:create_index('vec', {type = 'vector', dimension = 2,
                               unique = false, parts = {{2, 'embedding'}}})
        s:create_index('tag', {unique = true, parts = {{3, 'string'}}})
    end)
end)

g.after_each(function(cg)
    cg.server:exec(function()
        box.space.vector_atomic:drop()
    end)
end)

g.test_later_secondary_failure = function(cg)
    cg.server:exec(function()
        local e = require('embedding').new
        local s = box.space.vector_atomic
        s:insert{1, e({1, 0}), 'a'}
        s:insert{2, e({0, 1}), 'b'}
        local before = s.index.vec:stat()
        local ok = pcall(function() s:replace{1, e({0.5, 0.5}), 'b'} end)
        t.assert_equals(ok, false)
        local after = s.index.vec:stat()
        t.assert_equals(after.slots.nodes, before.slots.nodes)
        t.assert_equals(after.slots.embeddings, before.slots.embeddings)
        t.assert_equals(after.slots.reusable, before.slots.reusable)
        t.assert_equals(s:get{1}:totable(), {1, e({1, 0}), 'a'})
        local found = s.index.vec:select({{1, 0}},
                                         {iterator = 'neighbor', limit = 1})
        t.assert_equals({#found, found[1][1]}, {1, 1})
        t.assert_equals(s.index.vec:len(), 2)
        s:replace{1, e({0.9, 0.1}), 'c'}
        t.assert_equals(s:get{1}:totable(), {1, e({0.9, 0.1}), 'c'})
    end)
end

g.test_transaction_rollback = function(cg)
    cg.server:exec(function()
        local e = require('embedding').new
        local s = box.space.vector_atomic
        s:insert{1, e({1, 0}), 'a'}
        box.begin()
        s:replace{1, e({0, 1}), 'b'}
        box.rollback()
        t.assert_equals(s:get{1}:totable(), {1, e({1, 0}), 'a'})
        local found = s.index.vec:select({{1, 0}},
                                         {iterator = 'neighbor', limit = 1})
        t.assert_equals({#found, found[1][1]}, {1, 1})
    end)
end

g.test_savepoint_rollback = function(cg)
    cg.server:exec(function()
        local e = require('embedding').new
        local s = box.space.vector_atomic
        s:insert{1, e({1, 0}), 'a'}
        box.begin()
        local savepoint = box.savepoint()
        s:replace{1, e({0, 1}), 'b'}
        box.rollback_to_savepoint(savepoint)
        box.commit()
        t.assert_equals(s:get{1}:totable(), {1, e({1, 0}), 'a'})
        local found = s.index.vec:select({{1, 0}},
                                         {iterator = 'neighbor', limit = 1})
        t.assert_equals({#found, found[1][1]}, {1, 1})
    end)
end

g.test_invalid_replace_and_followup = function(cg)
    cg.server:exec(function()
        local e = require('embedding').new
        local s = box.space.vector_atomic
        s:insert{1, e({1, 0}), 'a'}
        t.assert_equals(pcall(function()
            s:replace{1, e({1}), 'b'}
        end), false)
        t.assert_equals(s:get{1}:totable(), {1, e({1, 0}), 'a'})
        t.assert_equals(s.index.vec:select({{1, 0}},
                                           {iterator = 'neighbor',
                                            limit = 1})[1][1], 1)
        s:replace{1, e({0, 1}), 'b'}
        t.assert_equals(s:get{1}:totable(), {1, e({0, 1}), 'b'})
    end)
end

g.test_primary_key_and_nonvector_update = function(cg)
    cg.server:exec(function()
        local e = require('embedding').new
        local s = box.space.vector_atomic
        s:insert{1, e({1, 0}), 'a'}
        s:update({1}, {{'=', 3, 'b'}})
        t.assert_equals(s.index.vec:select({{1, 0}},
                                           {iterator = 'neighbor',
                                            limit = 1})[1]:totable(),
                        {1, e({1, 0}), 'b'})
        t.assert_equals(pcall(function()
            s:update({1}, {{'=', 1, 2}})
        end), false)
        t.assert_equals(s:get{1}:totable(), {1, e({1, 0}), 'b'})
        box.begin()
        s:delete{1}
        s:insert{2, e({1, 0}), 'b'}
        box.commit()
        t.assert_equals(s:get{1}, nil)
        t.assert_equals(s.index.vec:select({{1, 0}},
                                           {iterator = 'neighbor',
                                            limit = 1})[1]:totable(),
                        {2, e({1, 0}), 'b'})
        s:replace{2, e({0, 1}), 'c'}
        t.assert_equals(s.index.vec:select({{0, 1}},
                                           {iterator = 'neighbor',
                                            limit = 1})[1][1], 2)
    end)
end

g.test_wal_failure_restores_vector = function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server:exec(function()
        local e = require('embedding').new
        local s = box.space.vector_atomic
        s:insert{1, e({1, 0}), 'a'}
        box.error.injection.set('ERRINJ_WAL_WRITE', true)
        local ok = pcall(function() s:replace{1, e({0, 1}), 'b'} end)
        box.error.injection.set('ERRINJ_WAL_WRITE', false)
        t.assert_equals(ok, false)
        t.assert_equals(s:get{1}:totable(), {1, e({1, 0}), 'a'})
        t.assert_equals(s.index.vec:select({{1, 0}},
                                           {iterator = 'neighbor',
                                            limit = 1})[1][1], 1)
        s:replace{1, e({0, 1}), 'b'}
        t.assert_equals(s:get{1}:totable(), {1, e({0, 1}), 'b'})
    end)
end

g.test_repeated_replacement_and_delete_rollback = function(cg)
    cg.server:exec(function()
        local e = require('embedding').new
        local s = box.space.vector_atomic
        s:insert{1, e({1, 0}), 'a'}
        for i = 1, 48 do
            local vector = {1 / (i + 1), 1}
            s:replace{1, e(vector), 'a'}
            local found = s.index.vec:select({vector},
                                             {iterator = 'neighbor', limit = 1})
            t.assert_equals({#found, found[1][1]}, {1, 1})
        end
        box.begin()
        s:delete{1}
        box.rollback()
        t.assert_equals(s:get{1}[1], 1)
        t.assert_equals(s.index.vec:select({{0, 1}},
                                           {iterator = 'neighbor',
                                            limit = 1})[1][1],
                        1)
        s:delete{1}
        t.assert_equals(#s.index.vec:select({{0, 1}},
                                            {iterator = 'neighbor',
                                            limit = 1}), 0)
        s:insert{1, e({1, 0}), 'a'}
        t.assert_equals(s.index.vec:select({{1, 0}},
                                           {iterator = 'neighbor',
                                            limit = 1})[1][1],
                        1)
    end)
end

g.test_vector_allocation_failures = function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server:exec(function()
        local e = require('embedding').new
        local s = box.space.vector_atomic
        local errinj = box.error.injection
        local failures = 0
        s:insert{1, e({1, 0}), 'a'}
        for countdown = 0, 48 do
            box.begin()
            errinj.set('ERRINJ_VECTOR_ALLOC', countdown)
            local ok = pcall(function()
                s:replace{1, e({0, 1}), 'b'}
            end)
            if not ok then
                failures = failures + 1
            end
            errinj.set('ERRINJ_VECTOR_ALLOC', 0)
            box.rollback()
            errinj.set('ERRINJ_VECTOR_ALLOC', -1)
            t.assert_equals(s:get{1}:totable(), {1, e({1, 0}), 'a'})
            local found = s.index.vec:select({{1, 0}},
                                             {iterator = 'neighbor', limit = 1})
            t.assert_equals(found[1][1], 1)
        end
        t.assert_gt(failures, 3)
        s:replace{1, e({0, 1}), 'b'}
        t.assert_equals(s:get{1}:totable(), {1, e({0, 1}), 'b'})
    end)
end

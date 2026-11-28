local server = require('luatest.server')
local t = require('luatest')

local g = t.group('memtx_vector_mvcc')

g.before_all(function(cg)
    cg.server = server:new({box_cfg = {memtx_use_mvcc_engine = true}})
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.before_each(function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('vector_mvcc')
        s:format({{name = 'id', type = 'unsigned'},
                  {name = 'vec', type = 'embedding'}})
        s:create_index('pk')
        s:create_index('vec', {type = 'vector', dimension = 2,
                               unique = false, parts = {{2, 'embedding'}}})
    end)
end)

g.after_each(function(cg)
    cg.server:exec(function()
        box.space.vector_mvcc:drop()
    end)
end)

g.test_len_counts_visible_tuples = function(cg)
    cg.server:exec(function()
        local e = require('embedding').new
        local s = box.space.vector_mvcc
        s:insert{1, e({1, 0})}
        box.begin()
        s:replace{1, e({0, 1})}
        t.assert_equals(s.index.vec:stat().slots.embeddings, 2)
        t.assert_equals(s.index.vec:len(), 1)
        t.assert_equals(s.index.vec:len(), s.index.pk:len())
        box.rollback()
    end)
end

g.test_reader_keeps_vector_version = function(cg)
    cg.server:exec(function()
        local e = require('embedding').new
        local fiber = require('fiber')
        local s = box.space.vector_mvcc
        local complete = fiber.channel(1)
        s:insert{1, e({1, 0})}
        box.begin()
        local before = s.index.vec:select({{1, 0}},
                                          {iterator = 'neighbor', limit = 1})
        t.assert_equals(before[1][2], e({1, 0}))
        fiber.create(function()
            complete:put(pcall(function()
                s:replace{1, e({0, 1})}
                local current = s.index.vec:select({{1, 0}},
                                                   {iterator = 'neighbor',
                                                    limit = 2})
                t.assert_equals(#current, 1)
                t.assert_equals(current[1][2], e({0, 1}))
            end))
        end)
        t.assert_equals(complete:get(), true)
        local after = s.index.vec:select({{1, 0}},
                                         {iterator = 'neighbor', limit = 1})
        box.rollback()
        t.assert_equals(after[1][2], e({1, 0}))
    end)
end

g.test_reader_keeps_deleted_version = function(cg)
    cg.server:exec(function()
        local e = require('embedding').new
        local fiber = require('fiber')
        local s = box.space.vector_mvcc
        local complete = fiber.channel(1)
        s:insert{1, e({1, 0})}
        box.begin()
        t.assert_equals(s.index.vec:select({{1, 0}},
                                           {iterator = 'neighbor',
                                            limit = 1})[1][1],
                        1)
        fiber.create(function()
            complete:put(pcall(function()
                s:delete{1}
                local current = s.index.vec:select({{1, 0}},
                                                    {iterator = 'neighbor',
                                                     limit = 1})
                t.assert_equals(current, {})
            end))
        end)
        t.assert_equals(complete:get(), true)
        t.assert_equals(s.index.vec:select({{1, 0}},
                                           {iterator = 'neighbor',
                                            limit = 1})[1][1],
                        1)
        box.rollback()
        box.internal.memtx_tx_gc(100)
        t.assert_equals(s.index.vec:select({{1, 0}},
                                           {iterator = 'neighbor',
                                            limit = 1}), {})
    end)
end

g.test_bucket_and_primary_key_versions = function(cg)
    cg.server:exec(function()
        local e = require('embedding').new
        local fiber = require('fiber')
        local s = box.space.vector_mvcc
        local complete = fiber.channel(1)
        s:format({{name = 'id', type = 'unsigned'},
                  {name = 'vec', type = 'embedding'},
                  {name = 'bucket_id', type = 'unsigned'}})
        s:insert{1, e({1, 0}), 1}
        box.begin()
        t.assert_equals(s.index.vec:select({{1, 0}},
                                           {iterator = 'neighbor',
                                            limit = 1})[1][3],
                        1)
        fiber.create(function()
            complete:put(pcall(function()
                s:replace{1, e({1, 0}), 2}
                s:delete{1}
                s:insert{2, e({1, 0}), 3}
                local current = s.index.vec:select({{1, 0}},
                                                   {iterator = 'neighbor',
                                                    limit = 2})
                t.assert_equals(#current, 1)
                t.assert_equals(current[1]:totable(), {2, e({1, 0}), 3})
            end))
        end)
        t.assert_equals(complete:get(), true)
        local old = s.index.vec:select({{1, 0}},
                                       {iterator = 'neighbor', limit = 2})
        t.assert_equals(#old, 1)
        t.assert_equals(old[1]:totable(), {1, e({1, 0}), 1})
        box.rollback()
    end)
end

g.test_read_your_writes = function(cg)
    cg.server:exec(function()
        local e = require('embedding').new
        local s = box.space.vector_mvcc
        box.begin()
        s:insert{1, e({1, 0})}
        t.assert_equals(s.index.vec:select({{1, 0}},
                                           {iterator = 'neighbor',
                                            limit = 1})[1][1],
                        1)
        s:replace{1, e({0, 1})}
        local current = s.index.vec:select({{1, 0}},
                                           {iterator = 'neighbor', limit = 2})
        t.assert_equals(#current, 1)
        t.assert_equals(current[1][2], e({0, 1}))
        s:delete{1}
        t.assert_equals(s.index.vec:select({{1, 0}},
                                           {iterator = 'neighbor',
                                            limit = 1}), {})
        box.rollback()
        t.assert_equals(s.index.vec:select({{1, 0}},
                                           {iterator = 'neighbor',
                                            limit = 1}), {})
    end)
end

g.test_uncommitted_insert_is_invisible = function(cg)
    cg.server:exec(function()
        local e = require('embedding').new
        local fiber = require('fiber')
        local s = box.space.vector_mvcc
        local staged = fiber.channel(1)
        local release = fiber.channel(1)
        local complete = fiber.channel(1)
        local isolations = {'best-effort', 'read-committed',
                            'read-confirmed'}
        for id, isolation in ipairs(isolations) do
            fiber.create(function()
                complete:put(pcall(function()
                    box.begin()
                    s:insert{id, e({1, 0})}
                    staged:put(true)
                    release:get()
                    box.commit()
                end))
            end)
            staged:get()
            box.begin({txn_isolation = isolation})
            local visible = s.index.vec:select({{1, 0}},
                                                {iterator = 'neighbor',
                                                 limit = 3})
            t.assert_equals(#visible, id - 1, isolation)
            for _, tuple in ipairs(visible) do
                t.assert_not_equals(tuple[1], id, isolation)
            end
            release:put(true)
            t.assert_equals(complete:get(), true)
            box.rollback()
            t.assert_equals(s:get{id}[1], id)
        end
    end)
end

g.test_empty_search_tracks_index = function(cg)
    cg.server:exec(function()
        local e = require('embedding').new
        local fiber = require('fiber')
        local s = box.space.vector_mvcc
        local complete = fiber.channel(1)
        box.begin()
        t.assert_equals(s.index.vec:select({{1, 0}},
                                           {iterator = 'neighbor',
                                            limit = 1}), {})
        fiber.create(function()
            complete:put(pcall(function() s:insert{1, e({1, 0})} end))
        end)
        t.assert_equals(complete:get(), true)
        local after = s.index.vec:select({{1, 0}},
                                         {iterator = 'neighbor', limit = 1})
        box.rollback()
        t.assert_equals(after, {})
    end)
end

g.test_distant_update_conflicts_with_index_read = function(cg)
    cg.server:exec(function()
        local e = require('embedding').new
        local fiber = require('fiber')
        local s = box.space.vector_mvcc
        local complete = fiber.channel(1)
        s:insert{1, e({0.8, 0.2})}
        s:insert{2, e({-1, 0})}
        box.begin()
        local before = s.index.vec:select({{1, 0}},
                                          {iterator = 'neighbor', limit = 1})
        t.assert_equals(before[1][1], 1)
        fiber.create(function()
            complete:put(pcall(function() s:replace{2, e({1, 0})} end))
        end)
        t.assert_equals(complete:get(), true)
        local after = s.index.vec:select({{1, 0}},
                                         {iterator = 'neighbor', limit = 1})
        box.rollback()
        t.assert_equals(after[1][1], 1)
    end)
end

g.test_two_empty_readers_cannot_both_insert = function(cg)
    cg.server:exec(function()
        local proxy = require('test.box.lua.txn_proxy')
        local a = proxy.new()
        local b = proxy.new()
        a:begin()
        b:begin()
        t.assert_equals(a('box.space.vector_mvcc.index.vec:' ..
                          'select({{1, 0}}, {iterator="neighbor", limit=1})'),
                        {{}})
        t.assert_equals(b('box.space.vector_mvcc.index.vec:' ..
                          'select({{1, 0}}, {iterator="neighbor", limit=1})'),
                        {{}})
        a('box.space.vector_mvcc:insert{1, require("embedding").new({1, 0})}')
        b('box.space.vector_mvcc:insert{2, require("embedding").new({0, 1})}')
        local a_commit = a:commit()
        local b_commit = b:commit()
        t.assert(a_commit ~= '' or b_commit ~= '')
    end)
end

g.test_disjoint_updates_conflict_after_index_read = function(cg)
    cg.server:exec(function()
        local e = require('embedding').new
        local proxy = require('test.box.lua.txn_proxy')
        local s = box.space.vector_mvcc
        s:insert{1, e({1, 0})}
        s:insert{2, e({0, 1})}
        local a = proxy.new()
        local b = proxy.new()
        a:begin()
        b:begin()
        t.assert_equals(a('box.space.vector_mvcc.index.vec:' ..
                          'select({{1, 0}}, {iterator="neighbor", limit=1})')
                        [1][1][1], 1)
        t.assert_equals(b('box.space.vector_mvcc.index.vec:' ..
                          'select({{0, 1}}, {iterator="neighbor", limit=1})')
                        [1][1][1], 2)
        a('box.space.vector_mvcc:replace{1, require("embedding").new({0.9, 0.1})}')
        b('box.space.vector_mvcc:replace{2, require("embedding").new({0.1, 0.9})}')
        local a_commit = a:commit()
        local b_commit = b:commit()
        t.assert(a_commit ~= '' or b_commit ~= '')
    end)
end

g.test_distant_delete_conflicts_with_index_read = function(cg)
    cg.server:exec(function()
        local e = require('embedding').new
        local proxy = require('test.box.lua.txn_proxy')
        local s = box.space.vector_mvcc
        s:insert{1, e({1, 0})}
        s:insert{2, e({-1, 0})}
        local reader = proxy.new()
        local writer = proxy.new()
        reader:begin()
        t.assert_equals(reader('box.space.vector_mvcc.index.vec:' ..
                               'select({{1, 0}}, '
                               .. '{iterator="neighbor", limit=1})')[1][1][1],
                        1)
        writer:begin()
        writer('box.space.vector_mvcc:delete{2}')
        t.assert_equals(writer:commit(), '')
        reader('box.space.vector_mvcc:insert{3, require("embedding").new({0, 1})}')
        t.assert_not_equals(reader:commit(), '')
    end)
end

g.test_version_gc_after_reader_finishes = function(cg)
    cg.server:exec(function()
        local e = require('embedding').new
        local fiber = require('fiber')
        local s = box.space.vector_mvcc
        local complete = fiber.channel(1)
        s:insert{1, e({1, 0})}
        box.begin()
        t.assert_equals(s.index.vec:select({{1, 0}},
                                           {iterator = 'neighbor',
                                            limit = 1})[1][2],
                        e({1, 0}))
        fiber.create(function()
            complete:put(pcall(function() s:replace{1, e({0, 1})} end))
        end)
        t.assert_equals(complete:get(), true)
        box.internal.memtx_tx_gc(100)
        t.assert_equals(s.index.vec:select({{1, 0}},
                                           {iterator = 'neighbor',
                                            limit = 1})[1][2],
                        e({1, 0}))
        box.rollback()
        box.internal.memtx_tx_gc(100)
        t.assert_equals(s.index.vec:select({{0, 1}},
                                           {iterator = 'neighbor',
                                            limit = 1})[1][2],
                        e({0, 1}))
        for i = 1, 64 do
            s:replace{1, e({1 / (i + 1), 1})}
            box.internal.memtx_tx_gc(100)
        end
        t.assert_equals(s.index.vec:select({{0, 1}},
                                           {iterator = 'neighbor',
                                            limit = 1})[1][1],
                        1)
    end)
end

g.test_rebuild_preserves_reader_version = function(cg)
    cg.server:exec(function()
        local e = require('embedding').new
        local fiber = require('fiber')
        local s = box.space.vector_mvcc
        local complete = fiber.channel(1)
        s:insert{1, e({1, 0})}
        box.begin()
        t.assert_equals(s.index.vec:select({{1, 0}}, {
            iterator = 'neighbor', limit = 1,
        })[1][2], e({1, 0}))
        fiber.create(function()
            complete:put(pcall(function()
                s:replace{1, e({0, 1})}
                s.index.vec:rebuild()
            end))
        end)
        t.assert_equals(complete:get(), true)
        t.assert_equals(s.index.vec:select({{1, 0}}, {
            iterator = 'neighbor', limit = 1,
        })[1][2], e({1, 0}))
        box.rollback()
        box.internal.memtx_tx_gc(100)
        t.assert_equals(s.index.vec:select({{0, 1}}, {
            iterator = 'neighbor', limit = 1,
        })[1][2], e({0, 1}))
    end)
end

g.test_rebuild_releases_retired_capacity = function(cg)
    cg.server:exec(function()
        local e = require('embedding').new
        local s = box.space.vector_mvcc
        for i = 1, 64 do
            s:replace{1, e({i, 0})}
            box.internal.memtx_tx_gc(100)
        end
        local before = s.index.vec:stat()
        local result = s.index.vec:select({{64, 0}}, {
            iterator = 'neighbor', limit = 1,
        })
        t.assert_equals(result[1][1], 1)
        s.index.vec:rebuild()
        local after = s.index.vec:stat()
        t.assert_equals(after.versions.live, 1)
        t.assert(after.slots.capacity <= before.slots.capacity)
        t.assert_lt(after.memory.total, before.memory.total)
        t.assert_equals(s.index.vec:select({{64, 0}}, {
            iterator = 'neighbor', limit = 1,
        })[1][1], 1)
    end)
end

g.test_deleted_version_gc_and_reinsert = function(cg)
    cg.server:exec(function()
        local e = require('embedding').new
        local s = box.space.vector_mvcc
        s:insert{1, e({1, 0})}
        s:delete{1}
        box.internal.memtx_tx_gc(100)
        local stat = s.index.vec:stat()
        t.assert_equals(stat.slots.reusable, 1)
        t.assert_equals(s.index.vec:select({{1, 0}},
                                           {iterator = 'neighbor',
                                            limit = 1}), {})
        s:insert{1, e({0, 1})}
        t.assert_equals(s.index.vec:stat().slots.reusable, 0)
        t.assert_equals(s.index.vec:select({{0, 1}},
                                           {iterator = 'neighbor',
                                            limit = 1})[1][2],
                        e({0, 1}))
        s.index.vec:rebuild()
        t.assert_equals(s.index.vec:stat().slots.reusable, 0)
        t.assert_equals(s.index.vec:stat().versions.live, 1)
    end)
end

g.test_linearizable_rejected_before_dml = function(cg)
    cg.server:exec(function()
        local e = require('embedding').new
        local s = box.space.vector_mvcc
        s:insert{1, e({1, 0})}
        box.begin({txn_isolation = 'linearizable'})
        t.assert_equals(pcall(function()
            return s.index.vec:select({{1, 0}},
                                      {iterator = 'neighbor', limit = 1})
        end), false)
        t.assert_equals(pcall(function()
            s:replace{1, e({0, 1})}
        end), false)
        box.rollback()
        t.assert_equals(s:get{1}:totable(), {1, e({1, 0})})
        t.assert_equals(s.index.vec:select({{1, 0}},
                                           {iterator = 'neighbor',
                                            limit = 1})[1][2],
                        e({1, 0}))
        local plain = box.schema.space.create('plain_linearizable')
        plain:create_index('pk')
        plain:insert{1}
        box.begin()
        t.assert_equals(plain:get{1}[1], 1)
        box.commit()
        plain:drop()
    end)
end


g.test_gc_during_cooperative_rebuild = function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server:exec(function()
        local e = require('embedding').new
        local fiber = require('fiber')
        local s = box.space.vector_mvcc
        local idx = s.index.vec
        s:insert{1, e({1, 0})}
        local ready, release = fiber.channel(1), fiber.channel(1)
        local reader = fiber.new(function()
            box.begin()
            idx:search({1, 0}, {limit = 1})
            ready:put(true)
            release:get()
            box.rollback()
        end)
        reader:set_joinable(true)
        t.assert(ready:get())
        s:replace{1, e({0, 1})}
        t.assert_gt(idx:stat().versions.retained, 0)
        box.error.injection.set('ERRINJ_VECTOR_REBUILD_DELAY', true)
        local builder = fiber.new(function() idx:rebuild() end)
        builder:set_joinable(true)
        fiber.sleep(0.01)
        release:put(true)
        t.assert(reader:join())
        box.internal.memtx_tx_gc(1000)
        t.assert_equals(idx:stat().versions.retained, 0)
        box.error.injection.set('ERRINJ_VECTOR_REBUILD_DELAY', false)
        t.assert(builder:join())
        t.assert_equals(idx:stat().memory.transient, 0)
        t.assert_equals(idx:search({0, 1}, {limit = 1})[1][2], e({0, 1}))
        idx:rebuild()
        t.assert_equals(idx:stat().slots.reusable, 0)
    end)
end

g.test_shared_embedding_keeps_payload_visibility = function(cg)
    cg.server:exec(function()
        local e = require('embedding').new
        local fiber = require('fiber')
        local s = box.space.vector_mvcc
        local idx = s.index.vec
        s:format({{name = 'id', type = 'unsigned'},
                  {name = 'vec', type = 'embedding'},
                  {name = 'payload', type = 'unsigned'}})
        s:insert{1, e({1, 0}), 10}
        local ready, release = fiber.channel(1), fiber.channel(1)
        local reader = fiber.new(function()
            box.begin()
            t.assert_equals(idx:search({1, 0}, {limit = 1})[1][3], 10)
            ready:put(true)
            release:get()
            t.assert_equals(idx:search({1, 0}, {
                limit = 1, filter = {field = 'payload', values = {10}},
            })[1][3], 10)
            t.assert_equals(idx:search({1, 0}, {
                limit = 1, filter = {field = 'payload', values = {20}},
            }), {})
            box.rollback()
        end)
        reader:set_joinable(true)
        t.assert(ready:get())
        for i = 1, 100 do s:update(1, {{'=', 3, i + 10}}) end
        t.assert_equals(idx:stat().slots.embeddings, 1)
        t.assert_equals(idx:stat().memory.vectors, 0)
        t.assert_equals(idx:stat().memory.borrowed_vectors, 8)
        t.assert_equals(idx:search({1, 0}, {limit = 1})[1][3], 110)
        idx:rebuild()
        release:put(true)
        t.assert(reader:join())
        box.internal.memtx_tx_gc(1000)
        t.assert_equals(idx:stat().versions.retained, 0)
        t.assert_equals(idx:stat().slots.reusable, 0)
    end)
end

g.test_uncommitted_versions_survive_generation_switch = function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server:exec(function()
        local e = require('embedding').new
        local fiber = require('fiber')
        local s = box.space.vector_mvcc
        local idx = s.index.vec
        s:insert{1, e({1, 0}), 'before'}
        local ready, release = fiber.channel(1), fiber.channel(1)
        local writer = fiber.new(function()
            box.begin()
            s:update(1, {{'=', 3, 'uncommitted'}})
            ready:put(true)
            release:get()
            t.assert_equals(idx:search({1, 0}, {limit = 1})[1][3],
                            'uncommitted')
            box.rollback()
        end)
        writer:set_joinable(true)
        t.assert(ready:get())
        idx:rebuild()
        t.assert_equals(idx:search({1, 0}, {limit = 1})[1][3], 'before')
        release:put(true)
        t.assert(writer:join())
        box.internal.memtx_tx_gc(1000)
        t.assert_equals(idx:search({1, 0}, {limit = 1})[1][3], 'before')
        t.assert_equals(idx:stat().slots.embeddings, 1)
    end)
end

g.test_shared_version_scan_obeys_work_limit = function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server:exec(function()
        local e = require('embedding').new
        local fiber = require('fiber')
        local s = box.space.vector_mvcc
        local idx = s.index.vec
        s:insert{1, e({1, 0}), 'old'}
        local ready, release = fiber.channel(1), fiber.channel(1)
        local reader = fiber.new(function()
            box.begin()
            idx:search({1, 0}, {limit = 1})
            ready:put(true)
            release:get()
            box.error.injection.set('ERRINJ_VECTOR_WORK_LIMIT', 32)
            local ok, err = pcall(idx.search, idx, {1, 0}, {limit = 1})
            box.error.injection.set('ERRINJ_VECTOR_WORK_LIMIT', -1)
            t.assert_equals(ok, false)
            t.assert_equals(err.code, box.error.VECTOR_WORK_LIMIT)
            box.rollback()
        end)
        reader:set_joinable(true)
        t.assert(ready:get())
        for i = 1, 100 do s:update(1, {{'=', 3, tostring(i)}}) end
        release:put(true)
        t.assert(reader:join())
    end)
end

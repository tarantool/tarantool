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

g.before_each(function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('vector_lifecycle')
        s:format({{name = 'id', type = 'unsigned'},
                  {name = 'vec', type = 'embedding'}})
        s:create_index('pk')
        s:create_index('vec', {type = 'vector', dimension = 2,
                               distance = 'l2', unique = false,
                               opts = {ef_search = 32},
                               parts = {{2, 'embedding'}}})
    end)
end)

g.after_each(function(cg)
    cg.server:exec(function()
        box.error.injection.set('ERRINJ_VECTOR_REBUILD_DELAY', false)
        if box.space.vector_lifecycle ~= nil then
            box.space.vector_lifecycle:drop()
        end
    end)
end)

g.test_search_width_alter_avoids_rebuild = function(cg)
    cg.server:exec(function()
        local embedding = require('embedding')
        local s = box.space.vector_lifecycle
        s.index.vec:alter({
            opts = {m = 8, ef_construction = 40, EF_SEARCH = 32},
        })
        s:insert{1, embedding.new({1, 0})}
        local before = s.index.vec:bsize()
        local generation = s.index.vec:stat().hnsw.generation
        s.index.vec:alter({opts = {ef_search = 128}})
        t.assert_equals(s.index.vec:bsize(), before)
        t.assert_equals(s.index.vec:stat().hnsw.generation, generation)
        t.assert_equals(s.index.vec:select({{1, 0}}, {
            iterator = 'neighbor', limit = 1,
        })[1][1], 1)
        t.assert_equals(box.space._index:get{s.id, 1}[5].opts,
                        {m = 8, ef_construction = 40, ef_search = 128})
        t.assert_error_msg_contains('invalid HNSW option value',
                                    s.index.vec.alter, s.index.vec,
                                    {opts = {ef_search = 64, EF_SEARCH = 32}})
    end)
end

g.test_statistics_and_read_view = function(cg)
    cg.server:exec(function()
        local embedding = require('embedding')
        local s = box.space.vector_lifecycle
        s:insert{1, embedding.new({1, 0})}
        s:insert{2, embedding.new({0, 1})}
        s.index.vec:search({1, 0}, {
            limit = 1,
            filter = {field = 'id', values = {2}},
        })
        local stat = s.index.vec:stat()
        t.assert_equals(stat.config.dimension, 2)
        t.assert_equals(stat.config.distance, 'l2')
        t.assert_equals(stat.config.scalar, 'float32')
        t.assert_equals(stat.config.numeric_contract, 'f32_f64_v1')
        t.assert_equals(stat.limits, {
            dimension = 4096, limit = 1024, ef_search = 8192,
            filter_values = 65536, timeout = 30, default_timeout = 1,
            work = 2^32,
        })
        t.assert_equals(stat.versions.live, 2)
        t.assert(stat.slots.capacity >= 2)
        t.assert_equals(stat.search.requests, 1)
        t.assert(stat.search.filtered_candidates >= 1)
        t.assert(stat.search.duration_ns > 0)
        local duration = stat.search.duration_ns
        s.index.vec:select({{1, 0}}, {iterator = 'neighbor', limit = 0})
        t.assert(s.index.vec:stat().search.duration_ns > duration)
        duration = s.index.vec:stat().search.duration_ns
        local ok = pcall(function()
            s.index.vec:select({{0/0, 0}}, {
                iterator = 'neighbor', limit = 1,
            })
        end)
        t.assert_equals(ok, false)
        stat = s.index.vec:stat()
        t.assert_equals(stat.search.requests, 3)
        t.assert_equals(stat.search.errors, 1)
        t.assert(stat.search.duration_ns > duration)
        t.assert_equals(stat.memory.total, s.index.vec:bsize())
        local categories = stat.memory.graph + stat.memory.vectors +
                           stat.memory.lookup + stat.memory.retained
        t.assert_equals(categories, stat.memory.total)
        local rv = box.read_view.open()
        rv:with(function()
            t.assert_equals(#rv.space.vector_lifecycle.index.vec:select(
                {{1, 0}}, {iterator = 'neighbor', limit = 2}), 2)
        end)
        rv:close()
    end)
end

g.test_retired_slots_are_reused = function(cg)
    cg.server:exec(function()
        local embedding = require('embedding')
        local s = box.space.vector_lifecycle
        s:insert{1, embedding.new({1, 0})}
        s:delete{1}
        local stat = s.index.vec:stat()
        t.assert_equals(stat.slots.reusable, 1)
        s:insert{2, embedding.new({0, 1})}
        t.assert_equals(s.index.vec:stat().slots.reusable, 0)
        box.begin()
        s:replace{2, embedding.new({2, 0})}
        box.rollback()
        t.assert_equals(s.index.vec:stat().slots.reusable, 1)
        t.assert_equals(s:get{2}[2], embedding.new({0, 1}))
        s.index.vec:rebuild()
        stat = s.index.vec:stat()
        t.assert_equals(stat.slots.reusable, 0)
        t.assert_equals(stat.versions.retired, 0)
        t.assert_equals(stat.versions.live, 1)
        t.assert_equals(s.index.vec:select({{0, 1}}, {
            iterator = 'neighbor', limit = 1,
        })[1][2], embedding.new({0, 1}))
    end)
end

g.test_failed_metric_alter_keeps_old_index = function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server:exec(function()
        local embedding = require('embedding')
        local s = box.space.vector_lifecycle
        s:insert{1, embedding.new({1, 0})}
        s:insert{2, embedding.new({0, 1})}
        box.error.injection.set('ERRINJ_VECTOR_ALLOC', 0)
        local ok = pcall(function()
            s.index.vec:alter({distance = 'ip'})
        end)
        box.error.injection.set('ERRINJ_VECTOR_ALLOC', -1)
        t.assert_equals(ok, false)
        t.assert_equals(box.space._index:get{s.id, 1}[5].distance, 'l2')
        local rows, _, distances = s.index.vec:search({1, 0}, {
            limit = 2, with_distance = true,
        })
        t.assert_equals({rows[1][1], distances[1]}, {1, 0})
    end)
end

g.test_snapshot_then_wal_updates = function(cg)
    cg.server:exec(function()
        local embedding = require('embedding')
        local s = box.space.vector_lifecycle
        s:insert{1, embedding.new({1, 0})}
        s:insert{2, embedding.new({0, 1})}
        box.snapshot()
        s:replace{2, embedding.new({0.9, 0.1})}
        s:delete{1}
        s:insert{3, embedding.new({1, 0})}
    end)
    cg.server:restart()
    cg.server:exec(function()
        local s = box.space.vector_lifecycle
        local rows, _, distances = s.index.vec:search({1, 0}, {
            limit = 3, with_distance = true,
        })
        t.assert_equals(#rows, 2)
        t.assert_equals({rows[1][1], distances[1]}, {3, 0})
        t.assert_equals(rows[2][1], 2)
    end)
end

g.test_large_snapshot_recovers_beyond_client_slice = function(cg)
    cg.server:exec(function()
        local embedding = require('embedding')
        local fiber = require('fiber')
        local s = box.schema.space.create('vector_recovery_large')
        s:format({{name = 'id', type = 'unsigned'},
                  {name = 'vec', type = 'embedding'}})
        s:create_index('pk')
        for id = 1, 1000 do
            local vector = {}
            for j = 1, 16 do
                vector[j] = ((id * 37 + j * 19) % 1000) / 500 - 1
            end
            s:insert{id, embedding.new(vector)}
        end
        fiber.set_slice(120)
        s:create_index('vec', {type = 'vector', dimension = 16,
                               distance = 'l2', unique = false,
                               parts = {{2, 'embedding'}}})
        fiber.set_slice(1)
        box.snapshot()
    end)
    cg.server:restart()
    cg.server:exec(function()
        local s = box.space.vector_recovery_large
        t.assert_equals(s:len(), 1000)
        t.assert_equals(s.index.vec:stat().versions.live, 1000)
        s:drop()
    end)
end

g.test_rebuild_failure_and_generation_switch = function(cg)
    cg.server:exec(function()
        local embedding = require('embedding')
        local s = box.space.vector_lifecycle
        for i = 1, 20 do
            s:insert{i, embedding.new({i, 0})}
        end
        local generation = s.index.vec:stat().hnsw.generation
        if t.tarantool.is_debug_build() then
            local before = s.index.vec:bsize()
            box.error.injection.set('ERRINJ_VECTOR_ALLOC', 0)
            local ok = pcall(function() s.index.vec:rebuild() end)
            box.error.injection.set('ERRINJ_VECTOR_ALLOC', -1)
            t.assert_equals(ok, false)
            t.assert_equals(s.index.vec:stat().hnsw.generation, generation)
            t.assert_equals(s.index.vec:bsize(), before)
            t.assert_equals(s.index.vec:select({{20, 0}}, {
                iterator = 'neighbor', limit = 1,
            })[1][1], 20)
        end
        s.index.vec:rebuild()
        t.assert_not_equals(s.index.vec:stat().hnsw.generation,
                            generation)
        t.assert_equals(s.index.vec:len(), 20)
        t.assert_equals(s.index.vec:select({{20, 0}}, {
            iterator = 'neighbor', limit = 1,
        })[1][1], 20)
    end)
end

g.test_rebuild_rejects_active_transaction = function(cg)
    cg.server:exec(function()
        local s = box.space.vector_lifecycle
        box.begin()
        local ok, err = pcall(function() s.index.vec:rebuild() end)
        box.rollback()
        t.assert_equals(ok, false)
        t.assert_equals(err.code, box.error.ACTIVE_TRANSACTION)
    end)
end

g.test_create_on_nonempty_and_failed_create = function(cg)
    cg.server:exec(function()
        local embedding = require('embedding')
        local s = box.space.vector_lifecycle
        s.index.vec:drop()
        for i = 1, 8 do
            s:insert{i, embedding.new({i, 0})}
        end
        local opts = {type = 'vector', dimension = 2,
                      distance = 'l2', unique = false,
                      parts = {{2, 'embedding'}}}
        if t.tarantool.is_debug_build() then
            box.error.injection.set('ERRINJ_VECTOR_ALLOC', 0)
            local ok = pcall(s.create_index, s, 'vec', opts)
            box.error.injection.set('ERRINJ_VECTOR_ALLOC', -1)
            t.assert_equals(ok, false)
            t.assert_equals(s.index.vec, nil)
            t.assert_equals(s:count(), 8)
        end
        s:create_index('vec', opts)
        t.assert_equals(s.index.vec:len(), 8)
        t.assert_equals(s.index.vec:select({{8, 0}}, {
            iterator = 'neighbor', limit = 1,
        })[1][1], 8)
        s:truncate()
        t.assert_equals(s.index.vec:len(), 0)
        t.assert_equals(s.index.vec:select({{8, 0}}, {
            iterator = 'neighbor', limit = 1,
        }), {})
    end)
end

g.test_structural_alter_rebuilds = function(cg)
    cg.server:exec(function()
        local embedding = require('embedding')
        local s = box.space.vector_lifecycle
        s:insert{1, embedding.new({1, 0})}
        local generation = s.index.vec:stat().hnsw.generation
        s.index.vec:alter({distance = 'ip'})
        t.assert_not_equals(s.index.vec:stat().hnsw.generation,
                            generation)
        t.assert_equals(s.index.vec:stat().config.distance, 'ip')
        local _, _, distances = s.index.vec:search({1, 0}, {
            limit = 1, with_distance = true,
        })
        t.assert_equals(distances, {0})
        local after = s.index.vec:stat().hnsw.generation
        local ok = pcall(function()
            s.index.vec:alter({dimension = 3})
        end)
        t.assert_equals(ok, false)
        t.assert_equals(s.index.vec:stat().hnsw.generation, after)
        t.assert_equals(s.index.vec:stat().config.dimension, 2)
    end)
end

g.test_rebuild_midway_oom_and_slice = function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server:exec(function()
        local embedding = require('embedding')
        local fiber = require('fiber')
        local s = box.space.vector_lifecycle
        for i = 1, 20 do
            s:insert{i, embedding.new({i, 0})}
        end
        local failures = 0
        for countdown = 0, 48 do
            local generation = s.index.vec:stat().hnsw.generation
            box.error.injection.set('ERRINJ_VECTOR_ALLOC', countdown)
            local ok = pcall(function() s.index.vec:rebuild() end)
            box.error.injection.set('ERRINJ_VECTOR_ALLOC', -1)
            if not ok then
                failures = failures + 1
                t.assert_equals(s.index.vec:stat().hnsw.generation,
                                generation)
            end
            t.assert_equals(s.index.vec:select({{20, 0}}, {
                iterator = 'neighbor', limit = 1,
            })[1][1], 20)
        end
        t.assert_gt(failures, 10)
        local generation = s.index.vec:stat().hnsw.generation
        fiber.set_slice(0)
        local ok = pcall(function() s.index.vec:rebuild() end)
        fiber.set_slice(1)
        t.assert_equals(ok, false)
        t.assert_equals(s.index.vec:stat().hnsw.generation, generation)
    end)
end

g.test_dml_during_yielding_index_build = function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server:exec(function()
        local embedding = require('embedding')
        local fiber = require('fiber')
        local s = box.space.vector_lifecycle
        s.index.vec:drop()
        for i = 1, 20 do
            s:insert{i, embedding.new({i, 0})}
        end
        box.error.injection.set('ERRINJ_BUILD_INDEX_DELAY', true)
        local builder = fiber.new(function()
            s:create_index('vec', {type = 'vector', dimension = 2,
                                   distance = 'l2', unique = false,
                                   parts = {{2, 'embedding'}}})
        end)
        builder:set_joinable(true)
        fiber.yield()
        for i = 1, 30 do
            s:replace{1, embedding.new({100 + i, 0})}
        end
        box.begin()
        s:replace{1, embedding.new({500, 0})}
        box.rollback()
        s:replace{20, embedding.new({200, 0})}
        s:delete{1}
        s:insert{21, embedding.new({21, 0})}
        box.error.injection.set('ERRINJ_BUILD_INDEX_DELAY', false)
        local ok, err = builder:join()
        t.assert(ok, err)
        local rows = s.index.vec:select({{1, 0}}, {
            iterator = 'neighbor', limit = 32,
        })
        local ids = {}
        for _, row in ipairs(rows) do
            table.insert(ids, row[1])
        end
        table.sort(ids)
        local expected = {}
        for _, row in ipairs(s.index.pk:select()) do
            table.insert(expected, row[1])
        end
        t.assert_equals(ids, expected)
        t.assert_equals(s.index.vec:len(), s:count())
        local stat = s.index.vec:stat()
        t.assert_equals(stat.versions.retained, 0)
        t.assert_equals(stat.slots.embeddings, s:count())
        t.assert_equals(s.index.vec:select({{200, 0}}, {
            iterator = 'neighbor', limit = 1,
        })[1][1], 20)
    end)
end

g.test_wal_pending_during_index_publication = function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server:exec(function()
        local e = require('embedding').new
        local fiber = require('fiber')
        local inject = box.error.injection.set
        local s = box.space.vector_lifecycle
        s.index.vec:drop()
        for i = 1, 20 do
            s:insert{i, e({i, 0})}
        end
        for _, wal_failure in ipairs({false, true}) do
            local old = s:get{1}
            inject('ERRINJ_BUILD_INDEX_DELAY', true)
            local builder = fiber.new(function()
                s:create_index('vec', {type = 'vector', dimension = 2,
                                       unique = false,
                                       parts = {{2, 'embedding'}}})
            end)
            builder:set_joinable(true)
            fiber.yield()
            inject('ERRINJ_WAL_DELAY', true)
            local writer = fiber.new(function()
                box.begin()
                for i = 1, 4 do
                    s:replace{1, e({100 + i, 0})}
                end
                box.commit()
            end)
            writer:set_joinable(true)
            fiber.yield()
            inject('ERRINJ_BUILD_INDEX_DELAY', false)
            local published, err = pcall(t.helpers.retrying, {timeout = 3},
                                         function() t.assert(s.index.vec) end)
            inject('ERRINJ_WAL_WRITE', wal_failure)
            inject('ERRINJ_WAL_DELAY', false)
            local written, write_err = writer:join()
            local built, build_err = builder:join()
            inject('ERRINJ_WAL_WRITE', false)
            t.assert(published, err)
            if wal_failure then
                t.assert_not(written)
                t.assert_equals(write_err.code, box.error.WAL_IO)
                t.assert_not(built)
                t.assert_equals(build_err.code, box.error.CASCADE_ROLLBACK)
                t.assert_equals(s:get{1}, old)
                t.assert_equals(s.index.vec, nil)
            else
                t.assert(written, write_err)
                t.assert(built, build_err)
                local stat = s.index.vec:stat()
                t.assert_equals(stat.versions.retained, 0)
                t.assert_equals(stat.slots.embeddings, s:count())
                t.assert_equals(s.index.vec:search({104, 0}, {limit = 1}),
                                {{1, e({104, 0})}})
                s.index.vec:drop()
            end
        end
    end)
end

g.test_rebuild_small_graph_at_max_construction_width = function(cg)
    cg.server:exec(function()
        local s = box.space.vector_lifecycle
        local e = require('embedding').new
        s.index.vec:alter({opts = {m = 64, ef_construction = 8192}})
        s:insert{1, e({1, 2})}
        local before = s.index.vec:stat().hnsw.generation
        s.index.vec:rebuild()
        t.assert_not_equals(s.index.vec:stat().hnsw.generation, before)
        t.assert_equals(s.index.vec:search({1, 2}, {limit = 1}),
                        {{1, e({1, 2})}})
    end)
end

g.test_failed_truncate_preserves_index = function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server:exec(function()
        local embedding = require('embedding')
        local s = box.space.vector_lifecycle
        s:insert{1, embedding.new({1, 0})}
        box.error.injection.set('ERRINJ_VECTOR_ALLOC', 0)
        local ok = pcall(function() s:truncate() end)
        box.error.injection.set('ERRINJ_VECTOR_ALLOC', -1)
        t.assert_equals(ok, false)
        t.assert_equals(s:get{1}[2], embedding.new({1, 0}))
        t.assert_equals(s.index.vec:select({{1, 0}}, {
            iterator = 'neighbor', limit = 1,
        })[1][1], 1)
    end)
end


g.test_rebuild_yields_and_accounts_generations = function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server:exec(function()
        local embedding = require('embedding')
        local fiber = require('fiber')
        local idx = box.space.vector_lifecycle.index.vec
        for i = 1, 20 do
            box.space.vector_lifecycle:insert{i, embedding.new({i, 0})}
        end
        local generation = idx:stat().hnsw.generation
        box.error.injection.set('ERRINJ_VECTOR_REBUILD_DELAY', true)
        local builder = fiber.new(function() idx:rebuild() end)
        builder:set_joinable(true)
        fiber.sleep(0.01)
        local stat = idx:stat()
        t.assert_equals(stat.hnsw.generation, generation)
        t.assert_gt(stat.memory.transient, 0)
        t.assert_equals(stat.memory.generations.transient,
                        stat.memory.transient)
        t.assert_equals(stat.memory.total,
                        stat.memory.generations.current + stat.memory.transient)
        t.assert_equals(idx:select({{20, 0}}, {
            iterator = 'neighbor', limit = 1,
        })[1][1], 20)
        local ok, err = pcall(function() idx:rebuild() end)
        t.assert_equals(ok, false)
        t.assert_equals(err.code, box.error.MODIFY_INDEX)
        box.error.injection.set('ERRINJ_VECTOR_REBUILD_DELAY', false)
        t.assert(builder:join())
        t.assert_not_equals(idx:stat().hnsw.generation, generation)
        t.assert_equals(idx:stat().memory.transient, 0)
    end)
end

g.test_rebuild_conflict_cancel_and_drop = function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server:exec(function()
        local embedding = require('embedding')
        local fiber = require('fiber')
        local s = box.space.vector_lifecycle
        s:insert{1, embedding.new({1, 0})}
        local actions = {'write', 'rollback', 'gc', 'truncate',
                         'cancel', 'drop'}
        for _, action in ipairs(actions) do
            local idx = s.index.vec
            local generation = idx:stat().hnsw.generation
            box.error.injection.set('ERRINJ_VECTOR_REBUILD_DELAY', true)
            local builder = fiber.new(function() idx:rebuild() end)
            builder:set_joinable(true)
            fiber.sleep(0.01)
            if action == 'write' then
                s:replace{1, embedding.new({2, 0})}
            elseif action == 'rollback' then
                box.begin()
                s:replace{1, embedding.new({3, 0})}
                box.rollback()
            elseif action == 'gc' then
                box.internal.memtx_tx_gc(1000)
                s:delete{1}
                s:insert{1, embedding.new({2, 0})}
            elseif action == 'truncate' then
                s:truncate()
            elseif action == 'cancel' then
                builder:cancel()
            else
                idx:drop()
            end
            box.error.injection.set('ERRINJ_VECTOR_REBUILD_DELAY', false)
            local ok, err = builder:join()
            if action == 'cancel' then
                t.assert_equals(ok, false)
                t.assert_equals(err.type, 'FiberIsCancelled')
            elseif action == 'truncate' or action == 'drop' then
                t.assert_equals(ok, false)
                t.assert_equals(err.code, box.error.MODIFY_INDEX)
            else
                t.assert_equals(ok, true)
            end
            if action == 'truncate' then
                t.assert_equals(s.index.vec:len(), 0)
                s:insert{1, embedding.new({2, 0})}
            elseif action ~= 'drop' then
                if action == 'cancel' then
                    t.assert_equals(idx:stat().hnsw.generation, generation)
                else
                    t.assert_not_equals(idx:stat().hnsw.generation, generation)
                end
                t.assert_equals(idx:stat().memory.transient, 0)
                t.assert_equals(idx:search({2, 0}, {limit = 1})[1][2],
                                embedding.new({2, 0}))
                idx:rebuild()
            end
        end
    end)
end

g.test_hash_primary_build_policy = function(cg)
    cg.server:exec(function()
        local embedding = require('embedding')
        local s = box.schema.space.create('vector_hash_build')
        s:create_index('pk', {type = 'hash'})
        local opts = {type = 'vector', dimension = 2,
                      unique = false, parts = {{2, 'embedding'}}}
        s:insert{1, embedding.new({1, 0})}
        t.assert_error_covers({code = box.error.VECTOR_UNSUPPORTED},
                              s.create_index, s, 'vec', opts)
        t.assert_equals(s.index.vec, nil)
        s:truncate()
        local idx = s:create_index('vec', opts)
        s:insert{1, embedding.new({1, 0})}
        idx:rebuild()
        t.assert_equals(idx:search({1, 0}, {limit = 1})[1][1], 1)
        s:drop()
    end)
end


g.test_large_build_and_rebuild_complete_with_short_slice = function(cg)
    cg.server:exec(function()
        local embedding = require('embedding')
        local fiber = require('fiber')
        local s = box.schema.space.create('vector_cooperative')
        s:create_index('pk')
        for id = 1, 4000 do
            local vector = {}
            for j = 1, 16 do
                vector[j] = ((id * 37 + j * 19) % 1000) / 500 - 1
            end
            s:insert{id, embedding.new(vector)}
        end
        local ticks = 0
        local running = true
        local heartbeat = fiber.new(function()
            while running do
                ticks = ticks + 1
                fiber.sleep(0)
            end
        end)
        heartbeat:set_joinable(true)
        fiber.set_slice(0.1)
        local idx = s:create_index('vec', {
            type = 'vector', dimension = 16, unique = false,
            parts = {{2, 'embedding'}},
        })
        local after_build = ticks
        idx:rebuild()
        fiber.set_slice(1)
        running = false
        t.assert(heartbeat:join())
        t.assert_gt(after_build, 1)
        t.assert_gt(ticks, after_build + 1)
        t.assert_equals(idx:len(), 4000)
        t.assert_equals(idx:stat().memory.transient, 0)
        t.assert_equals(idx:search(s:get{1}[2], {limit = 1})[1][2], s:get{1}[2])
        s:drop()
    end)
end

g.test_nonvector_updates_share_embedding = function(cg)
    cg.server:exec(function()
        local embedding = require('embedding')
        local s = box.space.vector_lifecycle
        s:insert{1, embedding.new({1, 0}), 'first'}
        s:update(1, {{'=', 3, 'second'}})
        local before = s.index.vec:stat()
        for i = 1, 1000 do s:update(1, {{'=', 3, tostring(i)}}) end
        local after = s.index.vec:stat()
        t.assert_equals(after.slots.embeddings, 1)
        t.assert_equals(after.slots.capacity, before.slots.capacity)
        t.assert_equals(after.memory.vectors, before.memory.vectors)
        t.assert_equals(after.memory.total, before.memory.total)
        t.assert_equals(after.slots.reusable, 0)
        t.assert_equals(s.index.vec:search({1, 0}, {limit = 1})[1][3],
                        '1000')
        s:update(1, {{'=', 2, embedding.new({1 + 1e-9, 0})}})
        t.assert_equals(s.index.vec:stat().slots.embeddings, 1)
        t.assert_equals(s.index.vec:stat().slots.reusable, 0)
    end)
end

g.test_rebuild_mirrors_dml_and_rollback = function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server:exec(function()
        local embedding = require('embedding')
        local fiber = require('fiber')
        local s = box.space.vector_lifecycle
        for i = 1, 20 do s:insert{i, embedding.new({i, 0})} end
        local idx = s.index.vec
        local generation = idx:stat().hnsw.generation
        box.error.injection.set('ERRINJ_VECTOR_REBUILD_DELAY', true)
        local builder = fiber.new(function() idx:rebuild() end)
        builder:set_joinable(true)
        fiber.sleep(0.01)
        s:replace{1, embedding.new({100, 0})}
        s:delete(2)
        s:insert{100, embedding.new({200, 0})}
        s:update(3, {{'=', 3, 'payload'}})
        box.begin()
        s:replace{4, embedding.new({400, 0})}
        box.rollback()
        box.error.injection.set('ERRINJ_VECTOR_REBUILD_DELAY', false)
        t.assert(builder:join())
        t.assert_not_equals(idx:stat().hnsw.generation, generation)
        for _, case in ipairs({{100, 1}, {200, 100}, {4, 4}}) do
            t.assert_equals(idx:search({case[1], 0}, {limit = 1})[1][1],
                            case[2])
        end
        t.assert_equals(idx:search({3, 0}, {limit = 1})[1][3], 'payload')
        t.assert_equals(s:get(2), nil)
        t.assert_equals(idx:stat().memory.transient, 0)
    end)
end

g.test_rebuild_bounds_concurrent_growth = function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server:exec(function()
        local embedding = require('embedding')
        local fiber = require('fiber')
        local s = box.space.vector_lifecycle
        s:insert{1, embedding.new({0, 0})}
        local idx = s.index.vec
        box.error.injection.set('ERRINJ_VECTOR_REBUILD_DELAY', true)
        local builder = fiber.new(function() idx:rebuild() end)
        builder:set_joinable(true)
        fiber.sleep(0.01)
        local limit = idx:stat().maintenance.admission_limit
        for i = 1, limit do s:replace{1, embedding.new({i, 0})} end
        local ok, err = pcall(s.replace, s, {1, embedding.new({limit + 1, 0})})
        t.assert_equals(ok, false)
        t.assert_equals(err.type, 'OutOfMemory')
        s:update(1, {{'=', 3, 'still available'}})
        t.assert_equals(embedding.totable(
            idx:search({limit, 0}, {limit = 1})[1][2])[1],
                        limit)
        box.error.injection.set('ERRINJ_VECTOR_REBUILD_DELAY', false)
        t.assert(builder:join())
        s:replace{1, embedding.new({limit + 1, 0})}
        t.assert_equals(embedding.totable(
            idx:search({limit + 1, 0}, {limit = 1})[1][2])[1],
                        limit + 1)
    end)
end

g.test_reuse_bounds_churn = function(cg)
    cg.server:exec(function()
        local embedding = require('embedding')
        local fiber = require('fiber')
        local s = box.space.vector_lifecycle
        for i = 1, 8 do s:insert{i, embedding.new({i, 0})} end
        for i = 1, 512 do
            s:replace{1, embedding.new({i, 1})}
            if i % 8 == 0 then fiber.sleep(0) end
        end
        t.helpers.retrying({timeout = 10}, function()
            local stat = s.index.vec:stat()
            t.assert_le(stat.slots.nodes, stat.slots.node_limit)
            t.assert_equals(stat.maintenance.running, false)
        end)
        local stat = s.index.vec:stat()
        t.assert_lt(stat.slots.reusable, 128)
        t.assert_le(stat.slots.capacity, 256)
        t.assert_le(stat.memory.vectors + stat.memory.retained, 8 * 256)
        t.assert_equals(s.index.vec:search({512, 1}, {limit = 1})[1][1], 1)
    end)
end

g.test_automatic_compaction_threshold = function(cg)
    cg.server:exec(function()
        local embedding = require('embedding')
        local s = box.space.vector_lifecycle
        for _, size in ipairs({128, 512}) do
            s:truncate()
            local idx = s.index.vec
            for i = 1, size do s:insert{i, embedding.new({i, 0})} end
            local rebuilds = idx:stat().maintenance.rebuilds
            local threshold = math.max(64, size / 4)
            box.error.injection.set('ERRINJ_VECTOR_REBUILD_DELAY', true)
            for i = 1, threshold - 1 do s:delete{i} end
            t.assert_equals(idx:stat().maintenance.rebuilds, rebuilds)
            t.assert_equals(idx:stat().maintenance.running, false)
            s:delete{threshold}
            t.helpers.retrying({}, function()
                t.assert_equals(idx:stat().maintenance.running, true)
            end)
            box.error.injection.set('ERRINJ_VECTOR_REBUILD_DELAY', false)
            t.helpers.retrying({}, function()
                t.assert_equals(idx:stat().maintenance.rebuilds,
                                rebuilds + 1)
                t.assert_equals(idx:stat().maintenance.running, false)
            end)
            t.assert_equals(idx:stat().slots.reusable, 0)
            t.assert_equals(idx:stat().slots.nodes, size - threshold)
        end
    end)
end

g.test_mirrored_insert_failure_is_atomic = function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server:exec(function()
        local embedding = require('embedding')
        local fiber = require('fiber')
        local s = box.space.vector_lifecycle
        local idx = s.index.vec
        s:insert{1, embedding.new({1, 0})}
        box.error.injection.set('ERRINJ_VECTOR_REBUILD_DELAY', true)
        local builder = fiber.new(function() idx:rebuild() end)
        builder:set_joinable(true)
        fiber.sleep(0.01)
        local failures, successes = 0, 0
        for fail_at = 0, 60 do
            box.begin()
            box.error.injection.set('ERRINJ_VECTOR_ALLOC', fail_at)
            local ok = pcall(s.replace, s, {1, embedding.new({2, 0})})
            box.error.injection.set('ERRINJ_VECTOR_ALLOC', -1)
            box.rollback()
            if ok then successes = successes + 1
            else failures = failures + 1 end
            t.assert_equals(idx:search({1, 0}, {limit = 1})[1][2],
                            embedding.new({1, 0}))
        end
        t.assert_gt(failures, 0)
        t.assert_gt(successes, 0)
        box.error.injection.set('ERRINJ_VECTOR_REBUILD_DELAY', false)
        t.assert(builder:join())
        t.assert_equals(idx:search({1, 0}, {limit = 1})[1][2],
                            embedding.new({1, 0}))
        t.assert_equals(idx:stat().memory.transient, 0)
        idx:rebuild()
        t.assert_equals(idx:stat().slots.reusable, 0)
    end)
end

g.test_writes_and_cancel_after_rebuild_publication = function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server:exec(function()
        local embedding = require('embedding')
        local fiber = require('fiber')
        local s = box.space.vector_lifecycle
        local idx = s.index.vec
        s:insert{1, embedding.new({1, 0})}
        local generation = idx:stat().hnsw.generation
        box.error.injection.set('ERRINJ_VECTOR_REBUILD_GC_DELAY', true)
        local builder = fiber.new(function() idx:rebuild() end)
        builder:set_joinable(true)
        fiber.sleep(0.01)
        t.assert_not_equals(idx:stat().hnsw.generation, generation)
        t.assert_equals(idx:stat().maintenance.running, true)
        s:replace{1, embedding.new({2, 0}), 'after publication'}
        s:insert{2, embedding.new({3, 0})}
        t.assert_equals(idx:search({2, 0}, {limit = 1})[1][3],
                        'after publication')
        builder:cancel()
        box.error.injection.set('ERRINJ_VECTOR_REBUILD_GC_DELAY', false)
        t.assert(builder:join())
        t.assert_equals(idx:stat().memory.transient, 0)
        t.assert_equals(idx:search({3, 0}, {limit = 1})[1][1], 2)
        t.assert_equals(idx:search({2, 0}, {limit = 1})[1][2],
                                embedding.new({2, 0}))
    end)
end

local server = require('luatest.server')
local t = require('luatest')

local g = t.group('memtx_vector_read_contract', {
    {mvcc = false, disable_ffi = false},
    {mvcc = false, disable_ffi = true},
    {mvcc = true, disable_ffi = false},
    {mvcc = true, disable_ffi = true},
})

g.before_all(function(cg)
    cg.server = server:new({box_cfg = {memtx_use_mvcc_engine = cg.params.mvcc}})
    cg.server:start()
    cg.server:exec(function(disable_ffi)
        require('internal.tweaks').box_read_view_ffi = not disable_ffi
        if disable_ffi then
            local ffi = require('ffi')
            ffi.cdef('void box_read_ffi_disable(void);')
            ffi.C.box_read_ffi_disable()
        end
    end, {cg.params.disable_ffi})
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.before_each(function(cg)
    cg.server:exec(function()
        local embedding = require('embedding')
        local s = box.schema.space.create('vector_read_contract')
        s:format({{name = 'id', type = 'unsigned'},
                  {name = 'vec', type = 'embedding'}})
        s:create_index('pk')
        s:create_index('vec', {type = 'vector', dimension = 2,
                              unique = false, parts = {{2, 'embedding'}}})
        s:insert{1, embedding.new({1, 0})}
        s:insert{2, embedding.new({0, 1})}
    end)
end)

g.after_each(function(cg)
    cg.server:exec(function()
        box.rollback()
        if box.space.vector_read_contract then
            box.space.vector_read_contract:drop()
        end
    end)
end)

g.test_pairs_rejected_and_primary_scan_available = function(cg)
    cg.server:exec(function()
        local embedding = require('embedding')
        local s = box.space.vector_read_contract
        local idx = s.index.vec
        local requests = idx:stat().search.requests
        t.assert_error_covers({code = box.error.VECTOR_UNSUPPORTED},
                              idx.tuple_pos, idx, s:get{1})
        t.assert_error_covers({code = box.error.VECTOR_UNSUPPORTED},
                              idx.pairs, idx)
        t.assert_error_covers({code = box.error.VECTOR_UNSUPPORTED},
                              idx.pairs, idx, {}, {iterator = 'ALL'})
        t.assert_error_covers({code = box.error.VECTOR_UNSUPPORTED},
                              idx.pairs, idx, {{1, 0}},
                              {iterator = 'NEIGHBOR'})
        local expected = {{1, embedding.new({1, 0})},
                          {2, embedding.new({0, 1})}}
        t.assert_equals(s:pairs():totable(), expected)
        t.assert_equals(s.index.pk:pairs():totable(), expected)
        t.assert_equals(idx:stat().search.requests, requests)
        t.assert_equals(idx:select({{1, 0}},
            {iterator = 'NEIGHBOR', limit = 1}), {{1, embedding.new({1, 0})}})
        t.assert_equals(idx:search({0, 1}, {limit = 1}),
                        {{2, embedding.new({0, 1})}})
    end)
end

g.test_count_all_and_keyed_rejection = function(cg)
    cg.server:exec(function()
        local embedding = require('embedding')
        local s = box.space.vector_read_contract
        local idx = s.index.vec
        t.assert_equals(idx:count(), 2)
        t.assert_equals(idx:count({}), 2)
        t.assert_equals(idx:count({}, {iterator = 'ALL'}), 2)
        t.assert_error_covers({code = box.error.VECTOR_UNSUPPORTED},
                              idx.count, idx, {{1, 0}},
                              {iterator = 'NEIGHBOR'})
        t.assert_error_covers({code = box.error.VECTOR_UNSUPPORTED},
                              idx.count, idx, {{1, 0}}, {iterator = 'ALL'})
        box.begin()
        s:delete{1}
        s:insert{3, embedding.new({1, 1})}
        s:insert{4, embedding.new({2, 1})}
        t.assert_equals(idx:count(), 3)
        t.assert_equals(idx:count(), s:count())
        box.rollback()
        t.assert_equals(idx:count(), 2)
    end)
end

g.test_count_hash_primary = function(cg)
    cg.server:exec(function()
        local embedding = require('embedding')
        local s = box.space.vector_read_contract
        s:truncate()
        s.index.vec:drop()
        s.index.pk:drop()
        s:create_index('pk', {type = 'hash'})
        s:create_index('vec', {type = 'vector', dimension = 2,
                              unique = false, parts = {{2, 'embedding'}}})
        s:insert{1, embedding.new({1, 0})}
        t.assert_equals(s.index.vec:count(), 1)
        t.assert_equals(s.index.vec:count(), s:count())
    end)
end

g.test_count_mvcc_visibility = function(cg)
    if not cg.params.mvcc then return end
    cg.server:exec(function()
        local embedding = require('embedding')
        local fiber = require('fiber')
        local s = box.space.vector_read_contract
        local complete = fiber.channel(1)
        box.begin()
        t.assert_equals(s.index.vec:count(), 2)
        fiber.create(function()
            complete:put(pcall(function()
                s:delete{1}
                s:insert{3, embedding.new({1, 1})}
                s:insert{4, embedding.new({2, 1})}
                t.assert_equals(s.index.vec:count(), 3)
            end))
        end)
        t.assert_equals(complete:get(), true)
        t.assert_equals(s.index.vec:count(), 2)
        t.assert_equals(s.index.vec:count(), s:count())
        box.rollback()
        t.assert_equals(s.index.vec:count(), 3)
    end)
end

g.test_c_iterator_and_count_contract = function(cg)
    cg.server:exec(function()
        local ffi = require('ffi')
        local msgpack = require('msgpack')
        local s = box.space.vector_read_contract
        local idx = s.index.vec
        for _, request in ipairs({
            {box.index.ALL, {}}, {box.index.NEIGHBOR, {{1, 0}}},
        }) do
            local key = msgpack.encode(request[2])
            local begin = ffi.cast('const char *', key)
            local it = ffi.C.box_index_iterator_with_offset(
                s.id, idx.id, request[1], begin, begin + #key, nil, nil, 0)
            t.assert(it == nil)
            t.assert_equals(box.error.last().code, box.error.VECTOR_UNSUPPORTED)
            local count = ffi.C.box_index_count(s.id, idx.id, request[1],
                                                begin, begin + #key)
            if request[1] == box.index.ALL then
                t.assert_equals(tonumber(count), 2)
            else
                t.assert_equals(tonumber(count), -1)
                t.assert_equals(box.error.last().code,
                                box.error.VECTOR_UNSUPPORTED)
            end
        end
    end)
end

g.test_read_view_neighbor_validates_options = function(cg)
    cg.server:exec(function()
        local s = box.space.vector_read_contract
        local e = require('embedding').new
        s:replace{2, e({1, 0})}
        s:insert{0, e({1, 0})}
        local rv = box.read_view.open()
        rv:with(function()
            local idx = rv.space.vector_read_contract.index.vec
            for _, limit in ipairs({0.5, 1.75, -4294967295, 4294967297}) do
                t.assert_error_covers({code = box.error.VECTOR_UNSUPPORTED},
                                      idx.select, idx, {{1, 0}},
                                      {iterator = 'NEIGHBOR', limit = limit})
            end
            for _, iterator in ipairs({11.5, 4294967307, -4294967285}) do
                t.assert_error_covers({code = box.error.ITERATOR_TYPE},
                                      idx.select, idx, {{1, 0}},
                                      {iterator = iterator, limit = 1.75})
            end
            t.assert_error_covers({code = box.error.VECTOR_UNSUPPORTED},
                                  idx.select, idx, {{1, 0}},
                                  {iterator = 'NEIGHBOR', limit = 1,
                                   offset = 0.5})
            t.assert_error_covers({code = box.error.VECTOR_UNSUPPORTED},
                                  idx.select, idx, {{1, 0}},
                                  {iterator = 'NEIGHBOR', limit = 1,
                                   filter = {field = 'id', values = {0}}})
            t.assert_error_covers({code = box.error.VECTOR_UNSUPPORTED},
                                  idx.tuple_pos, idx, s:get{1})
            local rows = idx:select({{1, 0}}, {
                iterator = box.index.NEIGHBOR, limit = 1,
            })
            t.assert_equals(rows, {{0, e({1, 0})}})
        end)
        rv:close()
    end)
end

g.test_read_view_duplicate_nodes_do_not_consume_limit = function(cg)
    if cg.params.mvcc then return end
    cg.server:exec(function()
        local s = box.space.vector_read_contract
        local e = require('embedding').new
        s:insert{3, e({2, 1})}
        s:replace{1, e({3, 1})}
        s:delete{3}
        s:replace{1, e({1, 0})}
        local rv = box.read_view.open()
        local rows = rv:with(function()
            return rv.space.vector_read_contract.index.vec:select({{1, 0}}, {
                iterator = 'NEIGHBOR', limit = 2,
            })
        end)
        rv:close()
        t.assert_equals(rows, {{1, e({1, 0})}, {2, e({0, 1})}})
    end)
end

g.test_read_view_duplicate_nodes_do_not_consume_search_width = function(cg)
    if cg.params.mvcc then return end
    cg.server:exec(function()
        local s = box.space.vector_read_contract
        local e = require('embedding').new
        s:truncate()
        s.index.vec:alter({distance = 'l2',
                          opts = {m = 4, ef_construction = 4, ef_search = 1}})
        s:insert{1, e({0, 0})}
        s:insert{2, e({100, 0})}
        box.begin()
        for _ = 1, 2 do
            s:replace{1, e({1, 0})}
            s:replace{1, e({0, 0})}
        end
        box.commit()
        local expected = {{1, e({0, 0})}, {2, e({100, 0})}}
        t.assert_equals(s.index.vec:search({0, 0}, {limit = 2}), expected)
        local rv = box.read_view.open()
        local rows = rv:with(function()
            return rv.space.vector_read_contract.index.vec:select({{0, 0}}, {
                iterator = 'NEIGHBOR', limit = 2,
            })
        end)
        rv:close()
        t.assert_equals(rows, expected)
    end)
end

g.test_read_view_embedding_cursor = function(cg)
    cg.server:exec(function()
        local embedding = require('embedding')
        local s = box.space.vector_read_contract
        s:drop()
        s = box.schema.space.create('vector_read_contract')
        s:format({{name = 'id', type = 'unsigned'},
                  {name = 'vec', type = 'embedding'}})
        s:create_index('pk')
        s:create_index('vec', {type = 'vector', dimension = 2,
                              distance = 'l2', unique = false,
                              parts = {{2, 'embedding'}}})
        for i = 1, 70 do s:insert{i, embedding.new({i, 0})} end
        local rv = box.read_view.open()
        s:delete{1}
        s:replace{2, embedding.new({100, 0})}
        s.index.vec:rebuild()
        s:drop()
        rv:with(function()
            local idx = rv.space.vector_read_contract.index.vec
            t.assert_error_covers({code = box.error.VECTOR_UNSUPPORTED},
                function()
                    idx:pairs({embedding.new({0, 0})}, {iterator = 'NEIGHBOR'})
                end)
            local rows = idx:select({embedding.new({0, 0})},
                                    {iterator = 'NEIGHBOR', limit = 70})
            t.assert_equals(#rows, 70)
            for i, row in ipairs(rows) do
                t.assert_equals(row[1], i)
                t.assert_equals(embedding.totable(row[2]), {i, 0})
            end
            t.assert_equals(idx:count(), 70)
        end)
        rv:close()
    end)
end

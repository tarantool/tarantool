local server = require('luatest.server')
local t = require('luatest')

local g = t.group(nil, {{disable_ffi = false}, {disable_ffi = true}})

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
    cg.server:exec(function(disable_ffi)
        local embedding = require('embedding')
        if disable_ffi then
            local ffi = require('ffi')
            ffi.cdef('void box_read_ffi_disable(void);')
            ffi.C.box_read_ffi_disable()
        end
        local s = box.schema.space.create('vector_protocol')
        s:create_index('pk')
        s:create_index('vec', {type = 'vector', dimension = 2, distance = 'l2',
                              unique = false, parts = {{2, 'embedding'}}})
        s:insert{1, embedding.new({1, 0})}
        s:insert{2, embedding.new({0, 1})}
    end, {cg.params.disable_ffi})
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test_local_select = function(cg)
    cg.server:exec(function()
        local embedding = require('embedding')
        local idx = box.space.vector_protocol.index.vec
        local opts = {iterator = 'neighbor', limit = 1}
        t.assert_equals(idx:select({{1, 0}}, opts),
                        {{1, embedding.new({1, 0})}})
        t.assert_equals(idx:select({{1, 0}}, {
            iterator = 'neighbor', limit = 0,
        }), {})
        for _, key in ipairs({{}, {1, 0}, {{1}}, {{'x', 0}},
                              {{1, 0}, {0, 1}}, {{0/0, 0}}}) do
            t.assert_error_covers({code = box.error.VECTOR_INVALID},
                                  idx.select, idx, key, opts)
        end
        for _, extra in ipairs({{offset = 0}, {after = ''},
                                {fetch_pos = false}, {with_distance = true},
                                {timeout = 1}, {filter = {}}, {opts = {}}}) do
            extra.iterator, extra.limit = 'neighbor', 1
            t.assert_error_covers({code = box.error.VECTOR_UNSUPPORTED},
                                  idx.select, idx, {{1, 0}}, extra)
        end
        t.assert_error_covers({code = box.error.VECTOR_INVALID},
                              idx.select, idx, {{1, 0}},
                              {iterator = 'neighbor'})
    end)
end

g.test_network_select = function(cg)
    local embedding = require('embedding')
    local idx = cg.server.net_box.space.vector_protocol.index.vec
    local opts = {iterator = 'neighbor', limit = 1, timeout = 10}
    t.assert_equals(idx:select({{1, 0}}, opts), {{1, embedding.new({1, 0})}})
    opts.is_async = true
    local future = idx:select({{0, 1}}, opts)
    t.assert_equals(future:wait_result(10), {{2, embedding.new({0, 1})}})
    for _, extra in ipairs({{offset = 0}, {after = ''}, {fetch_pos = true},
                            {with_distance = true}, {filter = {}},
                            {opts = {ef_search = 32}}}) do
        extra.iterator, extra.limit = 'neighbor', 1
        t.assert_error_covers({code = box.error.VECTOR_UNSUPPORTED},
                              idx.select, idx, {{1, 0}}, extra)
    end
    -- Disabling the client schema exercises the server's own validation.
    local net = require('net.box')
    local conn = net.connect(cg.server.net_box_uri, {fetch_schema = false})
    local sid = cg.server.net_box.space.vector_protocol.id
    t.assert_equals(conn.space[sid].index[1]:select({{1, 0}}, {
        iterator = 'neighbor', limit = 1,
    }), {{1, embedding.new({1, 0})}})
    t.assert_error_covers({code = box.error.VECTOR_UNSUPPORTED}, function()
        conn.space[sid].index[1]:select({{1, 0}}, {
            iterator = 'EQ', limit = 1,
        })
    end)
    t.assert_error_covers({code = box.error.VECTOR_INVALID}, function()
        conn.space[sid].index[1]:select({1, 0}, {
            iterator = 'neighbor', limit = 1,
        })
    end)
    conn:close()
end

g.test_search_contract = function(cg)
    cg.server:exec(function()
        local embedding = require('embedding')
        local idx = box.space.vector_protocol.index.vec
        local tuples, _, distances = idx:search({1, 0}, {
            limit = 2, with_distance = true,
        })
        t.assert_equals(tuples, {{1, embedding.new({1, 0})},
                                 {2, embedding.new({0, 1})}})
        t.assert_equals(distances, {0, 2})
        tuples, _, distances = idx:search({1, 0}, {limit = 1})
        t.assert_equals(tuples, {{1, embedding.new({1, 0})}})
        t.assert_equals(distances, nil)
        t.assert_equals(select('#', idx:search({1, 0}, {limit = 1})), 1)
        t.assert_equals(select('#', idx:search({1, 0}, {
            limit = 0, with_distance = true,
        })), 3)
        for _, query in ipairs({{}, {1}, {{1, 0}}, {'x', 0},
                                {a = 1}, {1, 0/0}, {1, 0, 2}}) do
            t.assert_error_covers({code = box.error.VECTOR_INVALID},
                                  idx.search, idx, query, {limit = 1})
        end
        t.assert_error_covers({code = box.error.VECTOR_UNSUPPORTED},
            box.space.vector_protocol.index.pk.search,
            box.space.vector_protocol.index.pk, {1, 0}, {limit = 1})
    end)
end

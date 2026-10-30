local server = require('luatest.server')
local t = require('luatest')

local g = t.group('gh-5861-exclude-null',
                  {{engine = 'memtx'}, {engine = 'vinyl'}})

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:stop()
    cg.server = nil
end)

g.before_each(function(cg)
    cg.server:exec(function(engine)
        local s = box.schema.space.create('test', {engine = engine})
        s:create_index('pk')
    end, {cg.params.engine})
end)

g.after_each(function(cg)
    cg.server:exec(function()
        box.space.test:drop()
    end)
end)

g.test_unsupported = function(cg)
    t.skip_if(cg.params.engine == 'vinyl', 'Not supported by Vinyl')
    cg.server:exec(function()
        local s = box.space.test
        t.assert_error_msg_equals(
            "HASH does not support nullable parts",
            s.create_index, s, 'sk',
            {type = 'hash', parts = {{2, 'unsigned', exclude_null = true}}})
        t.assert_error_msg_equals(
            "BITSET does not support nullable parts",
            s.create_index, s, 'sk',
            {type = 'bitset', parts = {{2, 'unsigned', exclude_null = true}}})
        t.assert_error_msg_equals(
            "RTREE does not support nullable parts",
            s.create_index, s, 'sk',
            {type = 'rtree', parts = {{2, 'array', exclude_null = true}}})
    end)
end

g.test_basic = function(cg)
    cg.server:exec(function()
        local s = box.space.test
        s:create_index('sk', {
            type = 'tree',
            parts = {{2, 'unsigned', exclude_null = true}},
        })
        s:insert({1, 10})
        s:insert({2})
        s:insert({3, box.NULL})
        s:insert({4, box.NULL, 5})
        t.assert_equals(s.index.sk:select(), {{1, 10}})
    end)
    cg.server:restart()
    cg.server:exec(function()
        local s = box.space.test
        t.assert_equals(s.index.sk:select(), {{1, 10}})
        s:replace({1})
        s:replace({2, 20})
        s:replace({3})
        s:delete({4})
        t.assert_equals(s.index.sk:select(), {{2, 20}})
    end)
end

g.test_multipart = function(cg)
    cg.server:exec(function()
        local s = box.space.test
        s:create_index('sk', {
            type = 'tree',
            parts = {
                {2, 'unsigned', is_nullable = true},
                {3, 'unsigned', exclude_null = true},
            },
        })
        s:insert({1, 10, 100})
        s:insert({2, 20})
        s:insert({3})
        s:insert({4, box.NULL, box.NULL})
        s:insert({5, 50, box.NULL})
        s:insert({6, box.NULL, 600})
        s:insert({7, box.NULL})
        t.assert_equals(s.index.sk:select(), {{6, nil, 600}, {1, 10, 100}})
    end)
    cg.server:restart()
    cg.server:exec(function()
        local s = box.space.test
        t.assert_equals(s.index.sk:select(), {{6, nil, 600}, {1, 10, 100}})
        s:replace({1, 10})
        s:replace({2, 20, 200})
        s:replace({3, 30, box.NULL})
        s:delete({6})
        t.assert_equals(s.index.sk:select(), {{2, 20, 200}})
    end)
end

g.test_json = function(cg)
    cg.server:exec(function()
        local s = box.space.test
        s:create_index('sk', {
            type = 'tree',
            parts = {{'[2].a.b', 'unsigned', exclude_null = true}},
        })
        s:insert({1})
        s:insert({2, {x = 1}})
        s:insert({3, {a = {x = 1}}})
        s:insert({4, {a = {b = box.NULL}}})
        s:insert({5, {a = {b = 50}}})
        t.assert_equals(s.index.sk:select(), {{5, {a = {b = 50}}}})
    end)
    cg.server:restart()
    cg.server:exec(function()
        local s = box.space.test
        t.assert_equals(s.index.sk:select(), {{5, {a = {b = 50}}}})
        s:replace({3, {a = {b = 30}}})
        s:replace({4, {a = {x = 40}}})
        s:delete({5})
        t.assert_equals(s.index.sk:select(), {{3, {a = {b = 30}}}})
    end)
end

g.test_multipart = function(cg)
    cg.server:exec(function()
        local s = box.space.test
        s:create_index('sk', {
            type = 'tree',
            parts = {{'[2][*]', 'unsigned', exclude_null = true}},
        })
        s:insert({1})
        s:insert({2, {box.NULL}})
        s:insert({3, {box.NULL, box.NULL}})
        s:insert({4, {40}})
        s:insert({5, {50, 500}})
        s:insert({6, {60, box.NULL}})
        s:insert({7, {box.NULL, 70, box.NULL}})
        t.assert_error_msg_contains(
            'Duplicate key exists in unique index',
            s.insert, s, {8, {box.NULL, 80, box.NULL, 40, box.NULL, 800}})
        t.assert_equals(s.index.sk:select(), {
            {4, {40}},
            {5, {50, 500}},
            {6, {60, box.NULL}},
            {7, {box.NULL, 70, box.NULL}},
            {5, {50, 500}},
        })
    end)
    cg.server:restart()
    cg.server:exec(function()
        local s = box.space.test
        t.assert_equals(s.index.sk:select(), {
            {4, {40}},
            {5, {50, 500}},
            {6, {60, box.NULL}},
            {7, {box.NULL, 70, box.NULL}},
            {5, {50, 500}},
        })
        s:delete({1})
        s:replace({2, {box.NULL, 20, box.NULL}})
        s:replace({3})
        s:delete({4})
        s:replace({5, {box.NULL}})
        s:replace({6, {60, box.NULL, 600}})
        s:replace({7, {70, 700}})
        t.assert_equals(s.index.sk:select(), {
            {2, {box.NULL, 20, box.NULL}},
            {6, {60, box.NULL, 600}},
            {7, {70, 700}},
            {6, {60, box.NULL, 600}},
            {7, {70, 700}},
        })
    end)
end

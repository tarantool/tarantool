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

g.after_each(function(cg)
    cg.server:exec(function()
        if box.space.embedding_test ~= nil then
            box.space.embedding_test:drop()
        end
    end)
end)

g.test_module = function(cg)
    cg.server:exec(function()
        local embedding = require('embedding')
        local value = embedding.new({1, -2, 0})
        t.assert(embedding.is(value))
        t.assert_not(embedding.is({1, -2, 0}))
        t.assert_not(embedding.is(require('varbinary').new('abcd')))
        t.assert_equals(#value, 3)
        t.assert_equals(value, embedding.new({1, -2, 0}))
        t.assert_not_equals(value, embedding.new({1, -2}))
        t.assert_not_equals(value, embedding.new({1, -2, 1}))
        t.assert_equals(embedding.totable(value), {1, -2, 0})
        t.assert_equals(embedding.totable(embedding.new({1 + 2 ^ -24})), {1})
        t.assert_error(embedding.new, {})
        t.assert_error(embedding.new, 'x')
        t.assert_error(embedding.new, {[1] = 1, [3] = 3})
        t.assert_error(embedding.new, {1, bad = 2})
        t.assert_error(embedding.new, {1, '2'})
        t.assert_error(embedding.new, {math.huge})
        t.assert_error(embedding.new, {0 / 0})
        t.assert_error(embedding.new, {1e100})
        t.assert_error(embedding.totable, {})
        local large = {}
        for i = 1, 4096 do large[i] = 0 end
        t.assert_equals(#embedding.new(large), 4096)
        large[4097] = 0
        t.assert_error(embedding.new, large)
        local lazy = setmetatable({1, 2, 3}, {
            __index = function() return 1 end,
        })
        lazy[2] = nil
        t.assert_error(embedding.new, lazy)
    end)
end

g.test_codecs_and_validation = function(cg)
    cg.server:exec(function()
        local embedding = require('embedding')
        local ffi = require('ffi')
        local msgpack = require('msgpack')
        local msgpackffi = require('msgpackffi')
        local value = embedding.new({1, -2, 0})
        local wire = '\xc7\x0c\x09' ..
                     '\0\0\x80\x3f\0\0\0\xc0\0\0\0\0'
        for _, codec in ipairs({msgpack, msgpackffi}) do
            t.assert_equals(codec.encode(value), wire)
            local decoded = codec.decode(wire)
            t.assert(embedding.is(decoded))
            t.assert_equals(embedding.totable(decoded), {1, -2, 0})
            t.assert_equals(codec.decode(codec.encode({value, 7}))[2], 7)
            for _, invalid in ipairs({
                '\xc7\0\x09',
                '\xc7\x03\x09abc',
                '\xd6\x09\0\0\x80\x7f',
                '\xd6\x09\0\0\xc0\x7f',
                '\xc8\x40\x04\x09' .. string.rep('\0', 16388),
            }) do
                t.assert_error(codec.decode, invalid)
            end
            local bad = ffi.new('struct embedding', 4)
            ffi.copy(bad.data, '\0\0\x80\x7f', 4)
            t.assert_error(codec.encode, bad)
            t.assert_error(embedding.totable, bad)
        end
        t.assert_error(msgpack.object_from_raw,
                       '\x91\xd6\x09\0\0\x80\x7f')
        for _, name in ipairs({'json', 'yaml'}) do
            local codec = require(name).new()
            for _, load_metatables in ipairs({true, false}) do
                codec.cfg({encode_load_metatables = load_metatables})
                t.assert_equals(codec.decode(codec.encode(value)), {1, -2, 0})
            end
        end
    end)
end

g.test_lua_console_output = function(cg)
    cg.server:exec(function()
        local console = require('console')
        console.eval([=[\set output lua]=])
        local output = console.eval(
            "return {require('embedding').new{1, -2, 0}, 7}")
        console.eval([=[\set output yaml]=])
        t.assert_equals(assert(loadstring('return ' .. output))(),
                        {{1, -2, 0}, 7})
    end)
end

g.test_schema_protocol_and_recovery_without_vector_index = function(cg)
    cg.server:exec(function()
        local embedding = require('embedding')
        local s = box.schema.space.create('embedding_test')
        s:format({{name = 'id', type = 'unsigned'},
                  {name = 'vec', type = 'embedding'},
                  {name = 'other', type = 'any'}})
        s:create_index('pk')
        s:insert({1, embedding.new({1, 0}), embedding.new({-2, 3})})
        t.assert_error(s.insert, s, {2, {1, 0}, 1})
        t.assert_error(s.insert, s, {2, require('varbinary').new('abcd'), 1})
        for _, kind in ipairs({'tree', 'hash', 'bitset', 'rtree'}) do
            t.assert_error(s.create_index, s, 'unsupported', {
                type = kind, unique = false, parts = {{2, 'embedding'}},
            })
        end
        local kd = require('key_def').new({{fieldno = 2, type = 'embedding'}})
        local tuple = {1, embedding.new({1, 0})}
        t.assert_error(kd.compare, kd, tuple, tuple)
        local result, err = box.execute(
            'SELECT "vec" FROM "embedding_test" WHERE "id" = 1')
        t.assert_equals(result, nil)
        t.assert_str_contains(tostring(err), 'not supported in SQL')
        result, err = box.execute('SELECT ?', {embedding.new({1, 0})})
        t.assert_equals(result, nil)
        t.assert_equals(err.code, box.error.SQL_BIND_TYPE)
        box.snapshot()
    end)
    local row = cg.server.net_box.space.embedding_test:get({1})
    local embedding = require('embedding')
    t.assert(embedding.is(row[2]))
    t.assert_equals(embedding.totable(row[2]), {1, 0})
    t.assert_equals(embedding.totable(row[3]), {-2, 3})
    cg.server:restart()
    cg.server:exec(function()
        local embedding = require('embedding')
        local row = box.space.embedding_test:get({1})
        t.assert(embedding.is(row[2]))
        t.assert_equals(embedding.totable(row[2]), {1, 0})
        t.assert_equals(embedding.totable(row[3]), {-2, 3})
    end)
end

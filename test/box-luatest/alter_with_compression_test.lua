local server = require('luatest.server')
local t = require('luatest')

local g = t.group("invalid compression type", t.helpers.matrix({
    engine = {'memtx', 'vinyl'},
    compression = {'zstd', 'lz4'}
}))

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:stop()
end)

g.test_invalid_compression_type_during_space_creation = function(cg)
    t.tarantool.skip_if_enterprise()
    cg.server:exec(function(engine, compression)
        local format = {{
            name = 'x', type = 'unsigned', compression = compression
        }}
        t.assert_error_msg_content_equals(
            "Wrong space format field 1: unknown compression type",
            box.schema.space.create, 'T', {engine = engine, format = format})
    end, {cg.params.engine, cg.params.compression})
end

g.before_test('test_invalid_compression_type_during_setting_format', function(cg)
    cg.server:exec(function(engine)
        box.schema.space.create('space', {engine = engine})
    end, {cg.params.engine})
end)

g.test_invalid_compression_type_during_setting_format = function(cg)
    t.tarantool.skip_if_enterprise()
    cg.server:exec(function(compression)
        local format = {{
            name = 'x', type = 'unsigned', compression = compression
        }}
        t.assert_error_msg_content_equals(
            "Wrong space format field 1: unknown compression type",
            box.space.space.format, box.space.space, format)
        t.assert_error_msg_content_equals(
            "Wrong space format field 1: unknown compression type",
            box.space.space.alter, box.space.space, {format = format})
    end, {cg.params.compression})
end

g.after_test('test_invalid_compression_type_during_setting_format', function(cg)
    cg.server:exec(function()
        box.space.space:drop()
    end)
end)

g = t.group("none compression", t.helpers.matrix({
    engine = {'memtx', 'vinyl'},
}))

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:stop()
end)

g.test_none_compression_during_space_creation = function(cg)
    cg.server:exec(function(engine)
        local format = {{
            name = 'x', type = 'unsigned', compression = 'none'
        }}
        t.assert(box.schema.space.create('T', {
            engine = engine, format = format
        }))
    end, {cg.params.engine})
end

g.before_test('test_none_compression_during_setting_format', function(cg)
    cg.server:exec(function(engine)
        box.schema.space.create('space', {engine = engine})
    end, {cg.params.engine})
end)

g.test_none_compression_during_setting_format = function(cg)
    cg.server:exec(function()
        local format = {{
            name = 'x', type = 'unsigned', compression = 'none'
        }}
        t.assert_equals(box.space.space:format(format), nil)
        t.assert_equals(box.space.space:alter({format = format}), nil)
    end)
end

g.after_test('test_none_compression_during_setting_format', function(cg)
    cg.server:exec(function()
        box.space.space:drop()
    end)
end)

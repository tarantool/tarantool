local server = require('luatest.server')
local t = require('luatest')

local g = t.group("compression", {{engine = 'memtx'}, {engine = 'vinyl'}})

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:stop()
end)

g.test_compression_values = function(cg)
    t.tarantool.skip_if_enterprise()
    cg.server:exec(function(engine)
        -- Successfully reformat the test space.
        local function check_ok(compression)
            box.space.test:format({{name = 'id', type = 'unsigned',
                                    compression = compression}})
        end

        -- Fail to reformat the test space.
        local function check_fail(compression, msg)
            t.assert_error_msg_content_equals(
                "Wrong space format field 1: " .. msg,
                box.space.test.format, box.space.test,
                {{name = 'id', type = 'unsigned', compression = compression}})
        end

        -- Create the test space.
        box.schema.space.create('test', {engine = engine})

        -- Do the tests.
        local msg_none_table_expected = "{'none'} compression table expected"
        local msg_unknown_compression = 'unknown compression type'
        check_ok('none')
        check_ok({'none'})
        check_fail('lz4', msg_unknown_compression)
        check_fail('zlib', msg_unknown_compression)
        check_fail('zstd', msg_unknown_compression)
        check_fail({}, msg_none_table_expected)
        check_fail({'lz4'}, msg_unknown_compression)
        check_fail({'zlib'}, msg_unknown_compression)
        check_fail({'zstd'}, msg_unknown_compression)
        check_fail(false, msg_none_table_expected)
        check_fail({false}, msg_none_table_expected)
        check_fail({'one', 'two'}, msg_none_table_expected)
        check_fail({'none', acceleration = 1}, msg_none_table_expected)
    end, {cg.params.engine})
end

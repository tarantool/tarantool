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
        check_ok(nil)
        check_ok({})
        check_ok('none')
        check_ok({type = 'none'})

        -- Non-str nor map compression value.
        local msg_map_or_str = 'compression is expected to be a MAP or STR'
        check_fail(1, msg_map_or_str)
        check_fail(false, msg_map_or_str)

        -- More than two compression table keys.
        local msg_no_options = 'CE supports no compression options'
        check_fail({type = 'none', acceleration = 1}, msg_no_options)
        check_fail({type = 'none', option = true}, msg_no_options)
        check_fail({'none', [true] = false}, msg_no_options)
        check_fail({'none', 'two'}, msg_no_options)

        -- A non-string compression table key.
        local msg_string_option = 'compression option name must be a string'
        check_fail({true}, msg_string_option)
        check_fail({'none'}, msg_string_option)
        check_fail({[true] = 'none'}, msg_string_option)

        -- A string key other than 'type'.
        local msg_type_only = "CE only supports 'type' compression table key"
        check_fail({none = true}, msg_type_only)
        check_fail({acceleration = 1000}, msg_type_only)

        -- Non-string provided by the 'type' key.
        local non_string_type = 'non-string compression type'
        check_fail({type = 1}, non_string_type)
        check_fail({type = true}, non_string_type)
        check_fail({type = {table = true}}, non_string_type)

        -- A compression type other than 'none'.
        local msg_unknown_type = "unknown compression type"
        check_fail('lz4', msg_unknown_type)
        check_fail('zlib', msg_unknown_type)
        check_fail('zstd', msg_unknown_type)
        check_fail('invalid', msg_unknown_type)
        check_fail({type = 'lz4'}, msg_unknown_type)
        check_fail({type = 'zlib'}, msg_unknown_type)
        check_fail({type = 'zstd'}, msg_unknown_type)
        check_fail({type = 'invalid'}, msg_unknown_type)
    end, {cg.params.engine})
end

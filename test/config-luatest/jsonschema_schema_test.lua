local t = require('luatest')

local schema = require('experimental.config.utils.schema')
local cluster_config = require('internal.config.cluster_config')
local config = require('config')

local g = t.group()

local function escape_descriptions(tbl)
    if type(tbl) ~= 'table' then
        return
    end

    for key, value in pairs(tbl) do
        if key == 'description' then
            tbl[key] = nil
        elseif type(value) == 'table' then
            escape_descriptions(value)
        end
    end
end

g.test_json_schema = function()
    local s = schema.new('basic', schema.record({
        foo = schema.record({
            bar = schema.enum({
                '0',
                '1',
            }),
            baz = schema.scalar({
                type = 'string',
                default = '0',
                allowed_values = {'0', '1', '2'},
            })
        }),
        fuz = schema.array({
            items = schema.scalar({
                type = 'string',
            }),
        }),
        buz = schema.map({
            key = schema.scalar({type = 'string'}),
            value = schema.scalar({
                type = 'string, number',
                default = '0'
            }),
        }),
        laz = schema.scalar({type='boolean'}),
        naz = schema.scalar({
            type = 'integer',
            allowed_values = {0, 1},
        })
    }))

    local expected = {
        ['$schema'] = 'https://json-schema.org/draft/2020-12/schema',
        additionalProperties = false,
        properties = {
            buz = {
                additionalProperties = {
                    default = '0',
                    type = {'string', 'number'}
                },
                type = 'object',
            },
            foo = {
                additionalProperties = false,
                properties = {
                    bar = {enum = {'0', '1'}, type = 'string'},
                    baz = {
                        default = '0',
                        enum = {'0', '1', '2'},
                        type = 'string'
                    },
                },
                type = 'object',
            },
            fuz = {items = {type = 'string'}, type = 'array'},
            laz = {type = 'boolean'},
            naz = {enum = {0, 1}, type = 'integer'},
        },
        type = 'object',
    }
    t.assert_equals(s:jsonschema(), expected)
end

g.test_invalid_json_schema = function()
    local s_1 = schema.new('invalid_schema_record', schema.record({
        foo = schema.record({
            [1] = schema.scalar({type = 'integer'}),
        }),
    }))

    local s_2 = schema.new('invalid_schema_map', schema.record({
        foo = schema.map({
            key = schema.scalar({type = 'number'}),
            value = schema.scalar({type = 'integer'}),
        }),
    }))

    local msg_1 = '[invalid_schema_record] foo[1]: JSON does not support ' ..
    'non-string keys.'
    t.assert_error_msg_equals(msg_1, function() s_1:jsonschema()  end)

    local msg_2 = '[invalid_schema_map] foo.<*>: JSON does not support ' ..
    'non-string keys.'
    t.assert_error_msg_equals(msg_2, function() s_2:jsonschema()  end)
end

g.test_json_schema_from_config = function()
    local s = config:jsonschema()
    local wal_section = s['properties']['wal']


    t.assert_equals(wal_section.description,
                    'This section defines configuration parameters related ' ..
                    'to write-ahead log.')

    t.assert_equals(wal_section['properties']['dir_rescan_delay'].description,
                    'The time interval in seconds between periodic scans of ' ..
                    'the write-ahead-log file directory, when checking for ' ..
                    'changes to write-ahead-log files for the sake of ' ..
                    'replication or hot standby.')

    escape_descriptions(wal_section)

    local expected = {
        additionalProperties = false,
        properties = {
            cleanup_delay = {type = 'number'},
            dir = {default = 'var/lib/{{ instance_name }}', type = 'string'},
            dir_rescan_delay = {default = 2, type = 'number'},
            ext = {
                additionalProperties = false,
                default = box.NULL,
                properties = {
                    new = {type = 'boolean'},
                    old = {type = 'boolean'},
                    spaces = {
                        additionalProperties = {
                            additionalProperties = false,
                            properties = {
                                new = {default = false, type = 'boolean'},
                                old = {default = false, type = 'boolean'},
                            },
                            type = 'object',
                        },
                        type = 'object',
                    },
                },
                type = 'object',
            },
            max_size = {default = 268435456, type = 'integer'},
            mode = {
                default = 'write',
                enum = {'none', 'write', 'fsync'},
                type = 'string'
            },
            queue_max_size = {default = 16777216, type = 'integer'},
            retention_period = {default = 0, type = 'number'},
        },
        type = 'object',
    }
    t.assert_equals(wal_section, expected)
end

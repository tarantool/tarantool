local cluster_config = require("internal.config.cluster_config")
local config = require('config')
local t = require('luatest')

local g = t.group()

local function remove_descriptions(tbl)
    if type(tbl) ~= 'table' then
        return
    end

    for key, value in pairs(tbl) do
        if key == 'description' then
            tbl[key] = nil
        elseif type(value) == 'table' then
            remove_descriptions(value)
        end
    end
end

g.test_cluster_config_schema_description_completeness = function()
    local function check_schema_description(schema, ctx)
        local field_path = table.concat(ctx.path, '.')
        t.assert(schema.description ~= nil or field_path == '',
                 string.format("%q is missing description", field_path))
        if schema.type == 'record' then
            for field_name, field_def in pairs(schema.fields) do
                table.insert(ctx.path, field_name)
                check_schema_description(field_def, ctx)
                table.remove(ctx.path)
            end
        elseif schema.type == 'map' then
            table.insert(ctx.path, '*')
            check_schema_description(schema.value, ctx)
            table.remove(ctx.path)
        elseif schema.type == 'array' then
            table.insert(ctx.path, '*')
            check_schema_description(schema.items, ctx)
            table.remove(ctx.path)
        end
    end

    local cluster_config_schema = rawget(cluster_config, 'schema')
    t.assert(cluster_config_schema ~= nil)
    check_schema_description(cluster_config_schema.fields, {path = {}})
end

g.test_json_schema_section_from_config = function()
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

    remove_descriptions(wal_section)

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

local t = require('luatest')
local config = require('config')

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

local function check_config_description(schema, ctx)
    local field_path = table.concat(ctx.path, '.')
    t.assert(schema.description ~= nil,
        string.format('%q is missing description', field_path))
    if schema.type == 'object' then
        if schema.properties ~= nil then
            for x, field in pairs(schema.properties) do
                table.insert(ctx.path, x)
                check_config_description(field, ctx)
                table.remove(ctx.path)
            end
        elseif schema.additionalProperties ~= nil then
            table.insert(ctx.path, '*')
            check_config_description(schema.additionalProperties, ctx)
            table.remove(ctx.path)
        end
    elseif schema.type == 'array' then
        table.insert(ctx.path, '*')
        check_config_description(schema.items, ctx)
        table.remove(ctx.path)
    end
end

g.test_cluster_config_schema_description_completeness = function()
    local s = config:jsonschema()
    check_config_description(s, {path = {}})
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

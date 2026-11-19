local t = require('luatest')
local schema = require('experimental.config.utils.schema')
local instance_config = require('internal.config.instance_config')
local discriminators = require('internal.config.discriminators')
local g = t.group()
local formats = {'prometheus', 'zabbix', 'telegraf', 'json'}

-- Both filters advertise failover and select the built-in group variant.
g.test_group_schema = function()
    local jsonschema = instance_config:jsonschema().properties.metrics.properties
    for _, filter in ipairs({'include', 'exclude'}) do
        local union = instance_config.schema.fields.metrics.fields[filter].items
        local builtin = union.variants[1]
        t.assert_is(discriminators['metrics.' .. filter]('failover', {
            schema = union,
        }), builtin)
        t.assert_items_include(builtin.allowed_values, {'failover'})
        t.assert_items_include(jsonschema[filter].items.anyOf[1].enum,
                               {'failover'})
        instance_config:validate({metrics = {[filter] = {'failover'}}})
    end
end

-- The exporter schema preserves all four formats and the Prometheus default.
g.test_exporter_format = function()
    local node = instance_config.schema.fields.failover.fields.metrics
        .fields.exporters.items.fields.format
    local format = schema.new('failover_format', node)
    t.assert_equals(node.allowed_values, formats)
    t.assert_equals(format:apply_default(nil), 'prometheus')
    local jsonschema = instance_config:jsonschema().properties.failover
        .properties.metrics.properties.exporters.items.properties.format
    t.assert_equals(jsonschema.enum, formats)
    t.assert_equals(jsonschema.default, 'prometheus')
    for _, supported in ipairs(formats) do
        format:validate(supported)
    end
    for _, unsupported in ipairs({'unsupported', 'yaml'}) do
        t.assert_error_msg_contains('prometheus', function()
            format:validate(unsupported)
        end)
    end
end

-- Full exporter validation preserves formats, the edition gate, and default.
g.test_exporter_config = function()
    local config = {failover = {metrics = {exporters = {{path = '/metrics'}}}}}
    if require('tarantool').package ~= 'Tarantool Enterprise' then
        t.assert_error_msg_contains('Tarantool Enterprise Edition', function()
            instance_config:validate(config)
        end)
        for _, supported in ipairs(formats) do
            config.failover.metrics.exporters[1].format = supported
            t.assert_error_msg_contains('Tarantool Enterprise Edition', function()
                instance_config:validate(config)
            end)
        end
        return
    end
    instance_config:validate(config)
    local defaulted = instance_config:apply_default(config)
    t.assert_equals(defaulted.failover.metrics.exporters[1].format, 'prometheus')
    for _, supported in ipairs(formats) do
        config.failover.metrics.exporters[1].format = supported
        instance_config:validate(config)
    end
    for _, unsupported in ipairs({'unsupported', 'yaml'}) do
        config.failover.metrics.exporters[1].format = unsupported
        t.assert_error_msg_contains('prometheus', function()
            instance_config:validate(config)
        end)
    end
end

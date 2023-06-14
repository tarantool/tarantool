local t = require('luatest')
local instance_config = require('internal.config.instance_config')

local g = t.group()

-- Check that all record element names can be found in the table and vice versa.
local function validate_fields(config, record)
    local config_fields = {}
    if type(config) == 'table' then
        for k in pairs(config) do
            table.insert(config_fields, k)
        end
    end

    local record_fields = {}
    for k, v in pairs(record.fields) do
        table.insert(record_fields, k)
    end

    t.assert_items_equals(config_fields, record_fields)
end

g.test_general = function()
    t.assert_equals(instance_config.name, 'instance_config')
end

g.test_config = function()
    local iconfig = {
        config = {
            version = 'dev',
            reload = 'auto',
        },
    }
    instance_config:validate(iconfig)
    validate_fields(iconfig.config, instance_config.schema.fields.config)

    iconfig = {
        config = {
            version = '0.0.0',
            reload = 'auto',
        },
    }
    local err = '[instance_config] config.version: Got 0.0.0, but only the '..
                'following values are allowed: dev'
    t.assert_error_msg_equals(err, function()
        instance_config:validate(iconfig)
    end)

    local exp = {
        reload = 'auto',
    }
    local res = instance_config:apply_default({}).config
    t.assert_equals(res, exp)
end

g.test_process = function()
    local iconfig = {
        process = {
            strip_core = true,
            coredump = true,
            background = true,
            title = 'one',
            username = 'two',
            work_dir = 'three',
            pid_file = 'four',
        },
    }
    instance_config:validate(iconfig)
    validate_fields(iconfig.process, instance_config.schema.fields.process)

    local exp = {
        strip_core = true,
        coredump = false,
        background = false,
        title = 'tarantool - {{ instance_name }}',
        username = box.NULL,
        work_dir = box.NULL,
        pid_file = '{{ instance_name }}.pid',
    }
    local res = instance_config:apply_default({}).process
    t.assert_equals(res, exp)
end

g.test_console = function()
    local iconfig = {
        console = {
            enabled = true,
            socket = 'one',
        },
    }
    instance_config:validate(iconfig)
    validate_fields(iconfig.console, instance_config.schema.fields.console)

    local exp = {
        enabled = true,
        socket = '{{ instance_name }}.control',
    }
    local res = instance_config:apply_default({}).console
    t.assert_equals(res, exp)
end

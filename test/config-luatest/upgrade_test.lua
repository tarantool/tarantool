local t = require('luatest')
local treegen = require('luatest.treegen')
local server = require('test.luatest_helpers.server')
local yaml = require('yaml')
local fio = require('fio')

local g = t.group()

g.before_all(function(g)
    local dir = treegen.prepare_directory({}, {})
    local data = 'test/box-luatest/upgrade/2.11.0/00000000000000000003.snap'
    fio.copyfile(data, dir)

    local config = {
        credentials = {
            roles = {
                manager = {
                    privileges = {{
                        permissions = {'read', 'write'},
                        spaces = {'_space'},
                    }},
                },
            },
            users = {
                guest = {
                    privileges = {{
                        permissions = {'read'},
                        spaces = {'_space'},
                    }},
                },
                admin = {
                    password = 'qwerty',
                    privileges = {{
                        permissions = {'read', 'write'},
                        spaces = {'_space'},
                    }},
                },
            },
        },

        iproto = {
            listen = {{uri = 'unix/:./{{ instance_name }}.iproto'}},
        },

        database = {
            replicaset_uuid = '9f62fe8b-7e70-474d-b051-d6af635f3cae',
            instance_uuid = '2ea3f41a-aae0-4652-8a51-69f8ca303c21',
        },

        snapshot = {
            dir = dir,
        },

        wal = {
            dir = dir,
        },

        groups = {
            ['group-001'] = {
                replicasets = {
                    ['replicaset-001'] = {
                        instances = {
                            ['instance-001'] = {},
                        },
                    },
                },
            },
        },
    }

    local cfg = yaml.encode(config)
    treegen.write_file(dir, 'cfg.yaml', cfg)
    local opts = {config_file = 'cfg.yaml', chdir = dir, alias = 'instance-001'}
    g.server = server:new(opts)

    g.server:start()
end)

g.after_all(function(g)
    g.server:drop()
end)

g.test_upgrade = function()
    g.server:exec(function()
        local netbox = require("net.box")
        local messages = {}
        for _, alert in pairs(require("config"):info().alerts) do
            table.insert(messages, alert.message)
        end
        local exp = 'credentials: the database schema has an old version ' ..
                    'and users/roles/privileges cannot be applied. '..
                    'Consider executing box.schema.upgrade() to perform an '..
                    'upgrade.'
        t.assert_items_include(messages, {exp})

        t.assert_equals(box.space._schema:get("version"), {'version', 2, 11, 0})

        box.schema.upgrade()
        -- After upgrade privileges should be granted automatically.

        exp = { 'read', 'space', '_space' }
        t.assert_items_include(box.schema.user.info('guest'), {exp})

        exp = { 'read,write', 'space', '_space' }
        t.assert_items_include(box.schema.user.info('admin'), {exp})

        -- Checks the password has been applied.
        local conn = netbox.connect('unix/:./instance-001.iproto',
                                    { user = 'admin', password = 'qwerty' })
        t.assert(conn:eval('return true'))
        conn:close()

        exp = { 'read,write', 'space', '_space' }
        t.assert_items_include(box.schema.role.info('manager'), {exp})

        -- Check the alert has been dropped.
        messages = {}
        for _, alert in pairs(require("config"):info().alerts) do
            table.insert(messages, alert.message)
        end

        t.assert_items_equals(messages, {})
    end)
end

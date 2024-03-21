local t = require('luatest')
local treegen = require('test.treegen')
local server = require('test.luatest_helpers.server')
local yaml = require('yaml')
local fio = require('fio')

local g = t.group()

g.before_all(function(g)
    treegen.init(g)

    local dir = treegen.prepare_directory(g, {}, {})
    local data = 'test/box-luatest/upgrade/2.11.0/00000000000000000003.snap'
    fio.copyfile(data, dir)

    local config = {
        credentials = {
            users = {
                guest = {
                    privileges = {{
                        permissions = {'read'},
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
    treegen.write_script(dir, 'cfg.yaml', cfg)
    local opts = {config_file = 'cfg.yaml', chdir = dir, alias = 'instance-001'}
    g.server = server:new(opts)

    g.server:start()
end)

g.after_all(function(g)
    g.server:drop()
    treegen.clean(g)
end)

g.test_upgrade = function()
    g.server:exec(function()
        t.assert_equals(box.space._schema:get("version"), {'version', 2, 11, 0})
        local messages = {}
        for _, alert in pairs(require("config"):info().alerts) do
            table.insert(messages, alert.message)
        end
        local exp = 'box.schema.user.grant("guest", "read", "space", ' ..
            '"_space") has failed because either the object has not been ' ..
            'created yet, a database schema upgrade has not been performed, ' ..
            'or the privilege write has failed (separate alert reported)'
        t.assert_items_include(messages, {exp})
    end)
end

local t = require('luatest')
local treegen = require('luatest.treegen')
local server = require('luatest.server')
local yaml = require('yaml')
local fio = require('fio')

local g = t.group()

g.before_all(function(g)
    local dir = treegen.prepare_directory({}, {})
    local data = 'test/box-luatest/gh_12267_data/00000000000000000027.snap'
    fio.copyfile(data, dir)

    local config = {
        credentials = {
            users = {
                admin = {
                    password = 'secret',
                    roles = {'super'},
                },
                client = {
                    password = 'secret',
                    roles = {'super'},
                },
                replicator = {
                    password = 'secret',
                    roles = {'replication'},
                },
                storage = {
                    password = 'secret',
                    roles = {'sharding'},
                },
                test = {
                    password = 'test',
                    roles = {'super'},
                },
            },
            roles = {
                reader = {
                    privileges = {{
                        permissions = {'read'},
                        universe = true,
                    }},
                },
            },
        },

        iproto = {
            listen = {{ uri = 'unix/:./{{ instance_name }}.iproto' }},
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
    local opts = {config_file = 'cfg.yaml', chdir = dir, alias = 'instance-001',
        net_box_credentials = {
            user = 'admin',
            password = 'secret',
        },
    }
    g.server = server:new(opts)

    g.server:start()
end)

g.after_all(function()
    g.server:drop()
end)

g.test_disown = function()
    local find_orphan_users_script =
        fio.abspath('tools/find-orphan-users.lua')
    t.assert(fio.path.exists(find_orphan_users_script))

    g.server:exec(function(find_orphan_users_script)
        box.schema.user.grant('client', 'read', 'space', '_schema', nil,
            {if_not_exists = true})

        -- Run the helper. It returns a table of lines.
        local lines = dofile(find_orphan_users_script)
        t.assert_type(lines, 'table')

        -- Helper to check that a line exists (order-independent).
        local function has_line(needle)
            for _, s in ipairs(lines) do
                if s == needle then
                    return true
                end
            end
            return false
        end

        -- Validate a few key lines from the sample.
        -- Entries listed as managed by YAML:
        t.assert(has_line('role "sharding"'))
        t.assert(has_line('role "reader"'))
        t.assert(has_line('user "storage"'))
        t.assert(has_line('user "replicator"'))
        t.assert(has_line('user "test"'))
        t.assert(has_line('user "client"'))

        -- Commands to transfer ownership to YAML:
        t.assert(has_line('box.schema.role.disown("sharding")'))
        t.assert(has_line('box.schema.role.disown("reader")'))
        t.assert(has_line('box.schema.user.disown("storage")'))
        t.assert(has_line('box.schema.user.disown("replicator")'))
        t.assert(has_line('box.schema.user.disown("test")'))
        t.assert(has_line('box.schema.user.disown("client")'))

        local function is_system(tuple)
            return tuple.id <= box.schema.SYSTEM_USER_ID_MAX or
                tuple.id == box.schema.SUPER_ROLE_ID
        end

        local function get_users_info()
            local info = {}
            for _, u in box.space._user:pairs() do
                if not is_system(u) then
                    local info_fn = box.schema.user.info
                    if u.type == 'role' then
                        info_fn = box.schema.role.info
                    end
                    local ok, ui = pcall(info_fn, u.name)
                    t.assert(ok)
                    info[u.name] = ui
                end
            end
            return info
        end

        local info_before_disown = get_users_info()

        -- Execute all generated commands.
        -- We find all lines that look like box.schema.*.<op>(...)
        for _, s in ipairs(lines) do
            if s:match('^box%.schema%.[%w_]+%.[%w_]+%(') then
                local fn, err = load(s)
                t.assert(fn, ('failed to compile: %s'):format(err or s))
                fn()
            end
        end

        local info_after_disown = get_users_info()
        t.assert_equals(info_before_disown, info_after_disown)
    end, {find_orphan_users_script})
end

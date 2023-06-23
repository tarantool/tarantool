local t = require('luatest')
local fio = require('fio')
local treegen = require('test.treegen')
local justrun = require('test.justrun')

local g = t.group()

local appliers_script = [[
    local configdata = require('internal.config.configdata')
    local cluster_config = require('internal.config.cluster_config')
    local cconfig = {
        credentials = {
            users = {
                guest = {
                    roles = {'super'},
                },
            },
        },
        memtx = {
            memory = 100000000,
        },
        groups = {
            ['group-001'] = {
                replicasets = {
                    ['replicaset-001'] = {
                        instances = {
                            ['instance-001'] = {
                                database = {
                                    rw = true,
                                },
                            },
                        },
                    },
                },
            },
        },
    }
    local iconfig = cluster_config:instantiate(cconfig, 'instance-001')
    config = {_configdata = configdata.new(iconfig, cconfig, 'instance-001')}
    local mkdir = require('internal.config.applier.mkdir')
    mkdir.apply(config)
    local box_cfg = require('internal.config.applier.box_cfg')
    box_cfg.apply(config)
    local credentials = require('internal.config.applier.credentials')
    credentials.apply(config)
    %s
    os.exit(0)
]]

g.before_all(function()
    treegen.init(g)
end)

g.after_all(function()
    treegen.clean(g)
end)

g.test_applier_mkdir = function()
    local dir = treegen.prepare_directory(g, {}, {})
    local injection = [[
        print(config._configdata:get('wal.dir', {use_default = true}))
    ]]
    treegen.write_script(dir, 'main.lua', appliers_script:format(injection))

    local env = {}
    local opts = {nojson = true, stderr = false}
    local res = justrun.tarantool(dir, env, {'main.lua'}, opts)
    t.assert_equals(res.exit_code, 0)
    t.assert_equals(res.stdout, 'instance-001')
    t.assert(fio.path.is_dir(fio.pathjoin(dir, 'instance-001')))
end

g.test_applier_box_cfg = function()
    local dir = treegen.prepare_directory(g, {}, {})
    local injection = [[
        print(box.cfg.memtx_memory)
    ]]
    treegen.write_script(dir, 'main.lua', appliers_script:format(injection))

    local env = {}
    local opts = {nojson = true, stderr = false}
    local res = justrun.tarantool(dir, env, {'main.lua'}, opts)
    t.assert_equals(res.exit_code, 0)
    t.assert_equals(res.stdout, '100000000')
end

g.test_applier_credentials = function()
    local dir = treegen.prepare_directory(g, {}, {})
    local injection = [[
        local guest_id = box.space._user.index.name:get{'guest'}.id
        local super_id = box.space._user.index.name:get{'super'}.id
        print(box.space._priv:get{guest_id, 'role', super_id} ~= nil)
    ]]
    treegen.write_script(dir, 'main.lua', appliers_script:format(injection))

    local env = {}
    local opts = {nojson = true, stderr = false}
    local res = justrun.tarantool(dir, env, {'main.lua'}, opts)
    t.assert_equals(res.exit_code, 0)
    t.assert_equals(res.stdout, 'true')
end

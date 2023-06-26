local t = require('luatest')
local cluster_config = require('internal.config.cluster_config')
local configdata = require('internal.config.configdata')
local treegen = require('test.treegen')
local justrun = require('test.justrun')
local json = require('json')

local g = t.group()

g.before_all(function()
    treegen.init(g)
end)

g.after_all(function()
    treegen.clean(g)
end)

g.test_configdata = function()
    local cconfig = {
        credentials = {
            users = {
                guest = {
                    roles = {'super'},
                },
            },
        },
        iproto = {
            listen = 'unix/:./{{ instance_name }}.iproto',
        },
        groups = {
            ['group-001'] = {
                memtx = {
                    memory = 1000000,
                },
                replicasets = {
                    ['replicaset-001'] = {
                        sql = {
                            cache_size = 2000,
                        },
                        instances = {
                            ['instance-001'] = {
                                database = {
                                    rw = true,
                                },
                            },
                            ['instance-002'] = {
                                database = {
                                    rw = false,
                                },
                            },
                        },
                    },
                },
            },
        },
    }
    cluster_config:validate(cconfig)
    local iconfig = cluster_config:instantiate(cconfig, 'instance-001')
    local data = configdata.new(iconfig, cconfig, 'instance-001')
    local expected = {
        credentials = {
            users = {
                guest = {
                    roles = {'super'},
                },
            },
        },
        iproto = {
            listen = 'unix/:./instance-001.iproto',
        },
        sql = {
            cache_size = 2000,
        },
        database = {
            rw = true,
        },
        memtx = {
            memory = 1000000,
        },
    }
    t.assert_equals(data:get(), expected)

    t.assert_equals(data:get('iproto'), expected.iproto)

    local f = function(w) return w.schema.type == 'integer' end
    local filtered_data = data:filter(f):totable()
    local expected_filtered_data = {
        {
            data = 2000,
            path = {"sql", "cache_size"},
            schema = {
                box_cfg = "sql_cache_size",
                default = 5242880,
                type = "integer",
            },
        },
        {
            data = 1000000,
            path = {"memtx", "memory"},
            schema = {
                box_cfg = "memtx_memory",
                default = 268435456,
                type = "integer",
            },
        },
    }
    t.assert_items_equals(filtered_data, expected_filtered_data)

    local f2 = function(w) return w.path[#w.path], w.data end
    local mapped_data = data:filter(f):map(f2):tomap()
    local expected_filtered_mapped_data = {
        cache_size = 2000,
        memory = 1000000,
    }
    t.assert_equals(mapped_data, expected_filtered_mapped_data)

    local expected_names = {
        group_name = "group-001",
        instance_name = "instance-001",
        replicaset_name = "replicaset-001",
    }
    t.assert_equals(data:names(), expected_names)

    t.assert_equals(data:peers(), {'instance-001', 'instance-002'})
end

g.test_config_general = function()
    local dir = treegen.prepare_directory(g, {}, {})
    local script = [[
        local json = require('json')
        local config = require('config')
        local file_config = "\
            log:\
              level: 7\
            memtx:\
              min_tuple_size: 16\
              memory: 100000000\
            groups:\
              group-001:\
                replicasets:\
                  replicaset-001:\
                    instances:\
                      instance-001:\
                        database:\
                          rw: true\
        "
        file = io.open('config.yaml', 'w')
        file:write(file_config)
        file:close()
        assert(config:info().status == 'uninitialized')
        config:_startup('instance-001', 'config.yaml')
        assert(config:info().status == 'ready')
        assert(box.cfg.memtx_min_tuple_size == 16)
        assert(box.cfg.memtx_memory == 100000000)
        assert(box.cfg.log_level == 0)
        local res = {old = config:info().alerts}

        file_config = "\
            log:\
              level: 7\
            memtx:\
              min_tuple_size: 32\
              memory: 100000001\
            groups:\
              group-001:\
                replicasets:\
                  replicaset-001:\
                    instances:\
                      instance-001:\
                        database:\
                          rw: true\
        "
        file = io.open('config.yaml', 'w')
        file:write(file_config)
        file:close()
        config:reload()
        assert(box.cfg.memtx_min_tuple_size == 16)
        assert(box.cfg.memtx_memory == 100000001)
        assert(box.cfg.log_level == 0)
        res.new = config:info().alerts
        print(json.encode(res))
        os.exit(0)
    ]]
    treegen.write_script(dir, 'main.lua', script)

    local env = {TT_LOG_LEVEL = 0}
    local opts = {nojson = true, stderr = false}
    local res = justrun.tarantool(dir, env, {'main.lua'}, opts)
    t.assert_equals(res.exit_code, 0)
    local info = json.decode(res.stdout)
    t.assert_equals(info.old, {})
    t.assert_equals(#info.new, 1)
    t.assert_equals(info.new[1].type, 'warn')
    local exp = "box_cfg.apply: non-dynamic option memtx_min_tuple_size will "..
                "not be set until the instance is restarted"
    t.assert_equals(info.new[1].message, exp)
end

g.test_config_broadcast = function()
    local dir = treegen.prepare_directory(g, {}, {})
    local file_config = [[
        app:
          file: 'script.lua'

        groups:
          group-001:
            replicasets:
              replicaset-001:
                instances:
                  instance-001:
                    database:
                      rw: true
    ]]
    treegen.write_script(dir, 'config.yaml', file_config)

    local main = [[
        local fiber = require('fiber')
        local config = require('config')
        local status = ''
        box.watch('config.info', function(_, v) status = v.status end)
        config:_startup('instance-001', 'config.yaml')
        while status ~= 'ready' do
            fiber.sleep(0.1)
        end
        print(status)
        config:reload()
        while status ~= 'ready' do
            fiber.sleep(0.1)
        end
        print(status)
        os.exit(0)
    ]]
    treegen.write_script(dir, 'main.lua', main)

    local script = [[
        local fiber = require('fiber')
        local status = ''
        box.watch('config.info', function(_, v) status = v.status end)
        while status ~= 'startup_in_progress' and
              status ~= 'reload_in_progress' do
            fiber.sleep(0.1)
        end
        print(status)
    ]]
    treegen.write_script(dir, 'script.lua', script)

    local opts = {nojson = true, stderr = false}
    local args = {'main.lua'}
    local res = justrun.tarantool(dir, {}, args, opts)
    t.assert_equals(res.exit_code, 0)
    local exp = {'startup_in_progress', 'ready', 'reload_in_progress', 'ready'}
    t.assert_equals(res.stdout, table.concat(exp, "\n"))
end

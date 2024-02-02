local t = require('luatest')
local treegen = require('test.treegen')
local server = require('test.luatest_helpers.server')
local yaml = require('yaml')
local fun = require('fun')
local fio = require('fio')

local g = t.group()

local has_vshard = pcall(require, 'vshard')

--
-- Test, that Tarantool upgrade with vshard happens without downtime:
--
--     1. Recover cluster from 2.11 xlogs with config.
--     2. Vshard works before schema upgrade, no names are set.
--     3. Schema upgrade on one shard, check vshard, wait for names.
--     4. Vshard works, when names are set on the half of the cluster.
--     5. Schema upgrade on the other shard, check vshard, wait for names.
--     6. Vshard works after all names are applied.
--
g.before_all(function(g)
    t.skip_if(not has_vshard, 'Module "vshard" is not available')
    treegen.init(g)
    local uuids = {
        ['rs-001'] = 'cbf06940-0790-498b-948d-042b62cf3d29',
        ['rs-002'] = 'ac522f65-aa94-4134-9f64-51ee384f1a54',
        ['instance-001'] = '8a274925-a26d-47fc-9e1b-af88ce939412',
        ['instance-002'] = '3de2e3e1-9ebe-4d0d-abb1-26d301b84633',
        ['instance-003'] = '1e02ae8a-afc0-4e91-ba34-843a356b8ed7',
        ['instance-004'] = '001688c3-66f8-4a31-8e19-036c17d489c2',
    }

    local prefix = 'test/box-luatest/upgrade/2.11.0/vshard/'
    local datadirs = {
        fio.abspath(fio.pathjoin(prefix, 'instance-001')),
        fio.abspath(fio.pathjoin(prefix, 'instance-002')),
        fio.abspath(fio.pathjoin(prefix, 'instance-003')),
        fio.abspath(fio.pathjoin(prefix, 'instance-004')),
    }

    local dir = treegen.prepare_directory(g, {}, {})
    local workdirs = {
        fio.pathjoin(dir, 'instance-001'),
        fio.pathjoin(dir, 'instance-002'),
        fio.pathjoin(dir, 'instance-003'),
        fio.pathjoin(dir, 'instance-004'),
    }

    for i, workdir in ipairs(workdirs) do
        fio.mktree(workdir)
        fio.copytree(datadirs[i], workdir)
    end

    local config = {
        iproto = {
            listen = {{uri = 'unix/:./{{ instance_name }}.iproto'}},
            advertise = {
                sharding = {
                  login = 'storage',
                  password = 'storage',
                }
            }
        },
        sharding = { bucket_count = 3000, },
        groups = {
            ['group-001'] = {
                replicasets = {
                    ['replicaset-001'] = {
                        database = {replicaset_uuid = uuids['rs-001']},
                        sharding = {roles = {'storage'}},
                        instances = {
                            ['instance-001'] = {
                                snapshot = {dir = workdirs[1]},
                                wal = {dir = workdirs[1]},
                                database = {
                                    instance_uuid = uuids['instance-001'],
                                    mode = 'rw',
                                },
                            },
                            ['instance-002'] = {
                                snapshot = {dir = workdirs[2]},
                                wal = {dir = workdirs[2]},
                                database = {
                                    instance_uuid = uuids['instance-002']
                                }
                            },
                        },
                    },
                    ['replicaset-002'] = {
                        database = {replicaset_uuid = uuids['rs-002']},
                        sharding = {roles = {'storage'}},
                        instances = {
                            ['instance-003'] = {
                                snapshot = {dir = workdirs[3]},
                                wal = {dir = workdirs[3]},
                                database = {
                                    instance_uuid = uuids['instance-003'],
                                    mode = 'rw',
                                },
                            },
                            ['instance-004'] = {
                                snapshot = {dir = workdirs[4]},
                                wal = {dir = workdirs[4]},
                                database = {
                                    instance_uuid = uuids['instance-004']
                                }
                            },
                        },
                    },
                    ['replicaset-003'] = {
                        sharding = {roles = {'router'}},
                        instances = {
                            ['instance-005'] = {
                                database = {mode = 'rw'},
                                credentials = {
                                    users = {
                                        guest = {roles = {'super'}}
                                    }
                                }
                            }
                        }
                    }
                },
            },
        },
    }

    local cfg = yaml.encode(config)
    local config_file = treegen.write_script(dir, 'cfg.yaml', cfg)
    local opts = {
        env = {LUA_PATH = os.environ()['LUA_PATH']},
        config_file = config_file,
        chdir = dir
    }
    g.instance_1 = server:new(fun.chain(opts, {alias = 'instance-001'}):tomap())
    g.instance_2 = server:new(fun.chain(opts, {alias = 'instance-002'}):tomap())
    g.instance_3 = server:new(fun.chain(opts, {alias = 'instance-003'}):tomap())
    g.instance_4 = server:new(fun.chain(opts, {alias = 'instance-004'}):tomap())
    g.instance_5 = server:new(fun.chain(opts, {alias = 'instance-005'}):tomap())
    g.instance_1:start({wait_until_ready = false})
    g.instance_2:start({wait_until_ready = false})
    g.instance_3:start({wait_until_ready = false})
    g.instance_4:start({wait_until_ready = false})
    g.instance_5:start({wait_until_ready = false})
    g.instance_1:wait_until_ready()
    g.instance_2:wait_until_ready()
    g.instance_3:wait_until_ready()
    g.instance_4:wait_until_ready()
    g.instance_5:wait_until_ready()

    local exec = [[
        function put(v)
            box.space.a:insert({v.id, v.bucket_id})
            return true
        end

        function get(id)
            return box.space.a:get(id)
        end
    ]]
    g.instance_1:eval(exec)
    g.instance_2:eval(exec)
    g.instance_3:eval(exec)
    g.instance_4:eval(exec)
end)

g.after_all(function(g)
    g.instance_1:drop()
    g.instance_2:drop()
    g.instance_3:drop()
    g.instance_4:drop()
    g.instance_5:drop()
    treegen.clean(g)
end)

local function check_vshard(g, id_1, id_2)
    local write_exec = string.format([[
        local ok, err = nil, {}
        local opts = {timeout = 20}
        ok, err[1] = vshard.router.call(1, 'write', 'put',
                                        {{id = %s, bucket_id = 1}}, opts)
        ok, err[2] = vshard.router.call(2000, 'write', 'put',
                                        {{id = %s, bucket_id = 2000}}, opts)
        return err
    ]], id_1, id_2)
    local err = g.instance_5:eval(write_exec)
    t.assert_equals(err[1], nil)
    t.assert_equals(err[2], nil)

    local read_exec = string.format([[
        local t = {}
        t[1] = vshard.router.call(1, 'read', 'get', {%s})
        t[2] = vshard.router.call(2000, 'read', 'get', {%s})
        return t
    ]], id_1, id_2)
    t.helpers.retrying({timeout = 20}, function()
        local ret = g.instance_5:eval(read_exec)
        t.assert_equals(ret[1], {id_1, 1})
        t.assert_equals(ret[2], {id_2, 2000})
    end)
end

local function wait_for_names(instance_name, replicaset_name)
    t.helpers.retrying({timeout = 20}, function()
        local info = box.info
        t.assert_equals(info.name, instance_name)
        t.assert_equals(info.replicaset.name, replicaset_name)
    end)
end

g.test_vshard_upgrade = function(g)
    local version_exec = [[ return box.space._schema:get{'version'} ]]
    local version = g.instance_1:eval(version_exec)
    t.assert_equals(version, {'version', 2, 11, 1})
    -- Assert, that tested buckets are on different shards.
    local bucket_exec = [[ return vshard.storage.buckets_info()[%s] ]]
    local bucket = g.instance_1:eval(string.format(bucket_exec, 2000))
    t.assert_not_equals(bucket, nil)
    bucket = g.instance_3:eval(string.format(bucket_exec, 1))
    t.assert_not_equals(bucket, nil)

    -- Check, that vshard works before schema upgrade.
    check_vshard(g, 1, 2000)

    -- Upgrade schema on one shard, wait for names to be applied.
    local upgrade_exec = [[
        box.ctl.wait_rw()
        box.schema.upgrade()
    ]]
    g.instance_1:eval(upgrade_exec)
    -- Before all names are applied, but after schema upgrade.
    -- Either none, some or all of the names were applied.
    check_vshard(g, 2, 2001)
    local rs_prefix = 'replicaset-00'
    g.instance_1:exec(wait_for_names, {g.instance_1.alias, rs_prefix .. '1'})
    g.instance_2:exec(wait_for_names, {g.instance_2.alias, rs_prefix .. '1'})
    -- Check, that vshard works, when one shard has names, other - doesn't.
    check_vshard(g, 3, 2002)

    -- Upgrade schema on the other shard, wait for names.
    g.instance_3:eval(upgrade_exec)
    -- Before all names are applied, but after schema upgrade.
    check_vshard(g, 4, 2003)
    -- Wait for names to be applied.
    g.instance_3:exec(wait_for_names, {g.instance_3.alias, rs_prefix .. '2'})
    g.instance_4:exec(wait_for_names, {g.instance_4.alias, rs_prefix .. '2'})

    -- Check, that vshard works after all names are set.
    check_vshard(g, 5, 2004)
end

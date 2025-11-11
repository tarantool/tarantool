local t = require('luatest')
local server = require('luatest.server')
local treegen = require('luatest.treegen')
local cbuilder = require('luatest.cbuilder')
local fio = require('fio')
local yaml = require('yaml')

local g = t.group()

local passwd = '123qwe'
local cert_dir = fio.pathjoin(fio.abspath(os.getenv('SOURCEDIR') or '.'),
                              'test/enterprise-luatest/ssl_cert')
local ca_file = fio.pathjoin(cert_dir, 'ca.crt')
local cert_file = fio.pathjoin(cert_dir, 'client.crt')
local key_file = fio.pathjoin(cert_dir, 'client.enc.key')
local ciphers = 'ECDHE-RSA-AES256-GCM-SHA384'

g.before_all(function()
    t.tarantool.skip_if_not_enterprise(
        'The iproto.ssl option is supported only by Tarantool ' ..
        'Enterprise Edition')
end)

local function base_config(g)
    return cbuilder:new()
        :set_global_option('credentials.users.guest', {roles = {'super'}})
        :set_global_option('replication.bootstrap_strategy', 'config')
        :set_global_option('iproto.listen', {
            {
                uri = 'unix/:./{{ instance_name }}.iproto',
                params = {
                    transport = 'ssl',
                },
            },
        })
        :set_global_option('iproto.ssl', g.ssl_opts)
        :add_instance('i-001', {database={mode='rw'}})
        :add_instance('i-002', {})
        :set_replicaset_option('bootstrap_leader', 'i-001')
        :config()
end

g.before_each(function(g)
    g.dir = treegen.prepare_directory({}, {})
    g.passwd_file = fio.pathjoin(g.dir, 'passwd.txt')
    local file = fio.open(g.passwd_file, {'O_WRONLY', 'O_CREAT'},
                          tonumber('666', 8))
    t.assert(file ~= nil)
    file:write(passwd)
    file:close()

    -- ssl configuration section.
    g.ssl_opts = {
        ca_file = ca_file,
        ssl_cert = cert_file,
        ssl_key = key_file,
        ssl_password = passwd,
        ssl_password_file = g.passwd_file,
        ssl_ciphers = ciphers,
    }
    -- ssl net.box connection parameters
    g.ssl_params = {
        transport = 'ssl',
        ssl_ca_file = ca_file,
        ssl_cert_file = cert_file,
        ssl_key_file = key_file,
        ssl_password = passwd,
        ssl_password_file = g.passwd_file,
        ssl_ciphers = ciphers,
    }
end)

g.test_basic = function(g)
    local dir = g.dir
    local config = base_config(g)

    local config_file = treegen.write_file(dir, 'config.yaml',
                                           yaml.encode(config))
    g.server_1 = server:new({
        config_file = config_file,
        chdir = dir,
        alias = 'i-001',
    })
    g.server_2 = server:new({
        config_file = config_file,
        chdir = dir,
        alias = 'i-002',
    })

    g.server_1:start({wait_until_ready = false})
    g.server_2:start({wait_until_ready = false})

    g.server_1.net_box_uri = {
        uri = ('unix/:%s/i-001.iproto'):format(dir),
        params = g.ssl_params,
    }

    g.server_1:wait_until_ready()

    g.server_1:exec(function(exp)
        local config = require('config')

        t.assert_equals(#box.cfg.listen, 1)
        t.assert_equals(box.cfg.listen[1].params, exp)

        t.assert_equals(#box.cfg.replication, 2)
        t.assert_equals(box.cfg.replication[2].params, exp)

        local uri = config:instance_uri('peer', {instance = 'i-002'})
        t.assert_equals(uri.params, exp)
    end, {g.ssl_params})
end

g.test_not_affects_non_ssl = function(g)
    local dir = g.dir

    local listen_no_ssl_uri = {uri = 'unix/:./{{ instance_name }}.iproto'}
    local config = cbuilder:new(base_config(g))
        :set_instance_option('i-002', 'iproto.listen', {listen_no_ssl_uri})
        :config()

    local config_file = treegen.write_file(dir, 'config.yaml',
                                           yaml.encode(config))
    g.server_1 = server:new({
        config_file = config_file,
        chdir = dir,
        alias = 'i-001',
    })

    g.server_1:start({wait_until_ready = false})

    g.server_1.net_box_uri = {
        uri = ('unix/:%s/i-001.iproto'):format(dir),
        params = g.ssl_params,
    }

    g.server_1:wait_until_ready()

    g.server_1:exec(function()
        local config = require('config')

        t.assert_equals(#box.cfg.replication, 2)
        t.assert_equals(box.cfg.replication[2].params, box.NULL)

        local uri = config:instance_uri('peer', {instance = 'i-002'})
        t.assert_equals(uri.params, box.NULL)
    end)
end

g.test_affects_advertise = function(g)
    local dir = g.dir
    local advertised_uri = 'funny-tarantool-instance.ru:3301'
    local config = cbuilder:new(base_config(g))
        :set_instance_option('i-002', 'iproto.advertise.peer', {
            uri = advertised_uri,
            params = {transport = 'ssl'},
        })
        :config()

    local config_file = treegen.write_file(dir, 'config.yaml',
                                           yaml.encode(config))
    g.server_1 = server:new({
        config_file = config_file,
        chdir = dir,
        alias = 'i-001',
    })

    g.server_1:start({wait_until_ready = false})

    g.server_1.net_box_uri = {
        uri = ('unix/:%s/i-001.iproto'):format(dir),
        params = g.ssl_params,
    }

    g.server_1:wait_until_ready()

    g.server_1:exec(function(exp_uri, exp_params)
        local config = require('config')

        t.assert_equals(#box.cfg.replication, 2)
        t.assert_equals(box.cfg.replication[2].uri, exp_uri)
        t.assert_equals(box.cfg.replication[2].params, exp_params)

        local uri = config:instance_uri('peer', {instance = 'i-002'})
        t.assert_equals(uri.uri, exp_uri)
        t.assert_equals(uri.params, exp_params)
    end, {advertised_uri, g.ssl_params})
end

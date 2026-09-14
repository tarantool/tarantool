-- tags: parallel

local checks = require('checks')
local fio = require('fio')
local netbox = require('net.box')
local t = require('luatest')
local cbuilder = require('luatest.cbuilder')
local cluster = require('luatest.cluster')
local log = require('log')
local yaml = require('yaml')
local json = require('json')
local helpers = require('luatest.helpers')
local uri = require('uri')
local fun = require('fun')

-- {{{ Common test config.

local common = {
    -- https://github.com/tarantool/tarantool-ee/issues/1660
    cfg_conn_reset_option = false,
    -- https://github.com/tarantool/tarantool-ee/issues/1667
    gh1667_solved = true,
    -- https://github.com/tarantool/tarantool-ee/issues/1672
    gh1672_solved = true
}

local want_to_skip = {
    test_net_box_conn = false,
    test_replication_ssl_reconfig = false,
    -- https://github.com/tarantool/tarantool/issues/13185
    test_replication_and_connpool_ssl_reconfig = true,
}

-- Common per test config.
local test_cfg = {
    test_net_box_conn = {
        debug_log_conf = false,
        debug_log_net_box = false,
    },
    test_replication_ssl_reconfig = {
        debug_log_conf = false,
        debug_log_net_box = false,
        -- 'all' | 'peer' (reduced) | 'none'
        debug_log_repl_info = 'none',
    },
    test_replication_and_connpool_ssl_reconfig = {
        -- 'repl' | 'connpool' | any = all.
        mode = 'all',
        --
        debug_log_conf = false,
        debug_log_net_box = false,
        -- 'all' | 'peer' (reduced) | 'none'
        debug_log_repl_info = 'none',
        debug_log_call_info = false,
        debug_dump_conf = false,
    },
}

-- }}} General test config.

-- {{{ Configure Lua search path

-- Determine Tarantool CE and Tarantool EE source directories.
local test_suite_dir = debug.__dir__
local project_dir = fio.dirname(fio.dirname(test_suite_dir))
local tarantool_ce_dir
local tarantool_ee_dir
if fio.path.exists(fio.pathjoin(project_dir, 'src/lua/etcd-client')) then
    -- ./tarantool/src/tarantool $(which luatest) -v \
    --     tarantool/test/enterprise-luatest/config_etcd_test.lua
    tarantool_ce_dir = fio.pathjoin(project_dir, 'tarantool')
    tarantool_ee_dir = project_dir
else
    -- (cd tarantool; ./src/tarantool $(which luatest) -v \
    --     test/enterprise-luatest/config_etcd_test.lua)
    tarantool_ce_dir = project_dir
    tarantool_ee_dir = fio.dirname(project_dir)
end

-- Add module search paths for testing helpers from Tarantool CE
-- and Tarantool EE.
package.path = table.concat({
    ('%s/?.lua'):format(tarantool_ce_dir),
    ('%s/?/init.lua'):format(tarantool_ce_dir),
    ('%s/?.lua'):format(tarantool_ee_dir),
    ('%s/?/init.lua'):format(tarantool_ee_dir),
    ('%s/src/lua/?.lua'):format(tarantool_ee_dir),
    ('%s/src/lua/?/init.lua'):format(tarantool_ee_dir),
    ('%s/?.lua'):format(test_suite_dir),
    package.path,
}, ';')

-- }}} Configure Lua search path

local utils = require('ssl_config_test_utils')

local CERT_DIR = fio.pathjoin(fio.abspath(os.getenv('SOURCEDIR') or '.'),
                              'test/ssl_cert')
local CA1_FILE = fio.pathjoin(CERT_DIR, 'ca.crt')
local CA1_SERVER1_CERT_FILE = fio.pathjoin(CERT_DIR, 'server.crt')
local CA1_SERVER1_KEY_FILE = fio.pathjoin(CERT_DIR, 'server.key')
local CA1_SERVER2_CERT_FILE = fio.pathjoin(CERT_DIR, 'server_2.crt')
local CA1_SERVER2_KEY_FILE = fio.pathjoin(CERT_DIR, 'server_2.key')
local CA1_SERVER3_CERT_FILE = fio.pathjoin(CERT_DIR, 'server_3.crt')
local CA1_SERVER3_KEY_FILE = fio.pathjoin(CERT_DIR, 'server_3.key')
local CA1_CLIENT1_CERT_FILE = fio.pathjoin(CERT_DIR, 'client.crt')
local CA1_CLIENT1_KEY_FILE = fio.pathjoin(CERT_DIR, 'client.key')
local CA1_CLIENT2_CERT_FILE = fio.pathjoin(CERT_DIR, 'client_2.crt')
local CA1_CLIENT2_KEY_FILE = fio.pathjoin(CERT_DIR, 'client_2.key')
local CA2_FILE = fio.pathjoin(CERT_DIR, 'ca2.crt')
local CA2_SERVER1_CERT_FILE = fio.pathjoin(CERT_DIR, 'server2.crt')
local CA2_SERVER1_KEY_FILE = fio.pathjoin(CERT_DIR, 'server2.key')
local CA2_SERVER2_CERT_FILE = fio.pathjoin(CERT_DIR, 'server2_2.crt')
local CA2_SERVER2_KEY_FILE = fio.pathjoin(CERT_DIR, 'server2_2.key')
local CA2_SERVER3_CERT_FILE = fio.pathjoin(CERT_DIR, 'server2_3.crt')
local CA2_SERVER3_KEY_FILE = fio.pathjoin(CERT_DIR, 'server2_3.key')
local CA2_CLIENT1_CERT_FILE = fio.pathjoin(CERT_DIR, 'client2.crt')
local CA2_CLIENT1_KEY_FILE = fio.pathjoin(CERT_DIR, 'client2.key')
local CA2_CLIENT2_CERT_FILE = fio.pathjoin(CERT_DIR, 'client2_2.crt')
local CA2_CLIENT2_KEY_FILE = fio.pathjoin(CERT_DIR, 'client2_2.key')

local CERTS = {}
CERTS.NC_CA1_S1 = {
    ssl_key = CA1_SERVER1_KEY_FILE,
    ssl_cert = CA1_SERVER1_CERT_FILE,
}
CERTS.NC_CA1_S2 = {
    ssl_key = CA1_SERVER2_KEY_FILE,
    ssl_cert = CA1_SERVER2_CERT_FILE,
}
CERTS.NC_CA1_S3 = {
    ssl_key = CA1_SERVER3_KEY_FILE,
    ssl_cert = CA1_SERVER3_CERT_FILE,
}
CERTS.NC_CA1_C1 = {
    ssl_key = CA1_CLIENT1_KEY_FILE,
    ssl_cert = CA1_CLIENT1_CERT_FILE,
}
CERTS.NC_CA2_S1 = {
    ssl_key = CA2_SERVER1_KEY_FILE,
    ssl_cert = CA2_SERVER1_CERT_FILE,
}
CERTS.NC_CA2_C1 = {
    ssl_key = CA2_CLIENT1_KEY_FILE,
    ssl_cert = CA2_CLIENT1_CERT_FILE,
}
-- CA1.
CERTS.CA1_S1 = {
    ca_file = CA1_FILE,
    ssl_key = CA1_SERVER1_KEY_FILE,
    ssl_cert = CA1_SERVER1_CERT_FILE,
}
CERTS.CA1_S2 = {
    ca_file = CA1_FILE,
    ssl_key = CA1_SERVER2_KEY_FILE,
    ssl_cert = CA1_SERVER2_CERT_FILE,
}
CERTS.CA1_S3 = {
    ca_file = CA1_FILE,
    ssl_key = CA1_SERVER3_KEY_FILE,
    ssl_cert = CA1_SERVER3_CERT_FILE,
}
CERTS.CA1_C1 = {
    ca_file = CA1_FILE,
    ssl_key = CA1_CLIENT1_KEY_FILE,
    ssl_cert = CA1_CLIENT1_CERT_FILE,
}
CERTS.CA1_C2 = {
    ca_file = CA1_FILE,
    ssl_key = CA1_CLIENT2_KEY_FILE,
    ssl_cert = CA1_CLIENT2_CERT_FILE,
}
-- CA2.
CERTS.CA2_S1 = {
    ca_file = CA2_FILE,
    ssl_key = CA2_SERVER1_KEY_FILE,
    ssl_cert = CA2_SERVER1_CERT_FILE,
}
CERTS.CA2_S2 = {
    ca_file = CA2_FILE,
    ssl_key = CA2_SERVER2_KEY_FILE,
    ssl_cert = CA2_SERVER2_CERT_FILE,
}
CERTS.CA2_S3 = {
    ca_file = CA2_FILE,
    ssl_key = CA2_SERVER3_KEY_FILE,
    ssl_cert = CA2_SERVER3_CERT_FILE,
}
CERTS.CA2_C1 = {
    ca_file = CA2_FILE,
    ssl_key = CA2_CLIENT1_KEY_FILE,
    ssl_cert = CA2_CLIENT1_CERT_FILE,
}
CERTS.CA2_C2 = {
    ca_file = CA2_FILE,
    ssl_key = CA2_CLIENT2_KEY_FILE,
    ssl_cert = CA2_CLIENT2_CERT_FILE,
}

local COMMON_URI = 'unix/:./{{ instance_name }}.iproto'
local PLAIN_SOCK = {
    uri = COMMON_URI
}
local SSL_SOCK = {
    uri = COMMON_URI,
    params = {transport = 'ssl'},
}

-- {{{ Cluster helpers section.

local function cluster_config_base(opts)
    checks('?table')
    opts = opts or {}
    local instances = opts.instances or {'i1'}
    local main_i = instances[1]

    local c = cbuilder:new(nil) -- start from cbuilder base_config
        :set_global_option('iproto.listen', {PLAIN_SOCK})
    for _, i in ipairs(instances) do
        c:add_instance(i, {})
    end
    c:set_replicaset_option('replication.bootstrap_strategy', 'config')
    c:set_replicaset_option('bootstrap_leader', main_i)
    c:set_replicaset_option('replication.failover', 'manual')
    c:set_replicaset_option('leader', main_i)
    if opts.election then
        c:set_replicaset_option('replication.failover', 'election')
        c:set_replicaset_option('leader', nil)
    end
    if opts.json_logs then
        c:set_global_option('log.format', 'json')
    end
    if opts.log_level then
        c:set_global_option('log.level', opts.log_level)
    end

    return c:config()
end

local function cluster_new(cfg, opts)
    checks('table', '?table')
    opts = opts or {}
    local server_opts = opts.server_opts or {}

    local cl = cluster:new(cfg, server_opts)
    cl.test = {
        -- Log cluster servers.net_box after reconnecting
        -- in cluster_wait_until_ready.
        debug_log_net_box = opts.debug_log_net_box or false,
        -- Log cluster config after creation or reloading.
        debug_log_conf = opts.debug_log_conf or false,
    }
    cl.test.state = {}

    if cl.test.debug_log_conf then
        log.info("new cluster conf:\n%s", yaml.encode(cfg))
    end
    return cl
end

-- Returns server's URI in a table form with given opts.
-- opts = nil -- use server opts.
-- opts = {transport} -- apply transport only.
-- opts = {transport, ssl_params} -- apply transport and ssl_params.
-- opts = {ssl_params} -- apply ssl_params and transport = 'ssl'.
local function server_net_box_uri_with_opts(server, opts)
    checks('table', {
        -- Transport.
        [1] = '?string',
        -- SSL opts:
        ca_file = '?string',
        ssl_key = '?string',
        ssl_cert = '?string',
    })
    local uri = {}
    if type(server.net_box_uri) == 'table' then
        uri.uri = server.net_box_uri.uri
        uri.params = server.net_box_uri.params
    elseif type(server.net_box_uri) == 'string' then
        uri.uri = server.net_box_uri
    else
        error("expect table or string in server.net_box_uri, got "..
              type(server.net_box_uri))
    end

    if opts ~= nil then
        local has_ssl_opts = opts.ca_file ~= nil or
            opts.ssl_key ~= nil or opts.ssl_cert ~= nil
        uri.params = {
            transport = opts[1] or (has_ssl_opts and 'ssl' or nil),
            ssl_ca_file = opts.ca_file,
            ssl_key_file = opts.ssl_key,
            ssl_cert_file = opts.ssl_cert,
        }
    end

    return uri
end

local function server_net_box_creds_with_opts(server, opts)
    checks('table', '?table')
    opts = opts or {}
    local sc = server.net_box_credentials
    local creds = {
        user = opts.user or sc.user,
        password = opts.password or sc.password,
    }
    return creds
end

-- Setup net.box connection for luatest server helper object.
-- It's the connection from tarantool instance running luatests
-- to tarantool instance running the server.
local function server_set_net_box_opts(server, opts)
    checks('table', '?table')
    server.net_box_uri = server_net_box_uri_with_opts(server, opts)
end

local function cluster_zero_net_box(cluster)
    checks('table')
    cluster:each(function(server) server.net_box = nil end)
end

local function cluster_set_net_box_opts(cluster, opts)
    checks('table', '?table')
    cluster:each(function(server) server_set_net_box_opts(server, opts) end)
end

local function cluster_log_net_box(cluster, msg, level)
    checks('table', '?string', '?number')
    msg = msg or ''
    level = level or 0
    local loc = utils.get_caller_loc(1 + level)
    cluster:each(function(server)
        log.info("%s: server %s(%s):%s\n%s",
            loc, server.alias, tostring(server), msg,
            yaml.encode({
                net_box_addr = server.net_box and
                    tostring(server.net_box) or nil,
                net_box_uri = server.net_box_uri,
                net_box = server.net_box,
                net_box_creds = server.net_box_credentials,
        }))
    end)
end

-- server.net_box is created by wait_until_ready() fn.
local function cluster_wait_until_ready(cluster, level)
    checks('table', '?number')
    level = level or 0
    if cluster.test.debug_log_net_box then
        cluster_log_net_box(cluster, 'connecting', 1 + level)
    end
    cluster:each(function(server) server:wait_until_ready() end)
    if cluster.test.debug_log_net_box then
        cluster_log_net_box(cluster, 'connected', 1 + level)
    end
end

local function cluster_reload(cluster, new_config, net_box_conn_opts)
    checks('table', '?table', '?table')
    cluster:reload(new_config)
    if cluster.test.debug_log_conf then
        log.info("cluster conf reloaded:\n%s", yaml.encode(new_config))
    end
    if net_box_conn_opts ~= nil then
        cluster_zero_net_box(cluster)
        cluster_set_net_box_opts(cluster, net_box_conn_opts)
        cluster_wait_until_ready(cluster, 1)
    end
end

local function cluster_add_package_path(cl, path)
    checks('table', 'string')
    log.info("cluster_add_package_path: %s", path)
    cl:each(function (server)
        server:exec(function(p)
            package.path = p..';'..package.path
        end, {path})
    end)
end

local function cluster_error_injection_set(cl, key, val)
    checks('table', 'string', 'boolean|number')
    log.info("cluster_error_injection_set: %s=%s", key, val)
    cl:each(function (server)
        server:exec(function(k, v)
            box.error.injection.set(k, v)
        end, {key, val})
    end)
end

local function cluster_collect_instances(cl)
    checks('table')
    local instances = {}
    cl:each(function (server) table.insert(instances, server.alias) end)
    return instances
end

local function cluster_servers_dump_config(cl)
    checks('table')
    cl:each(function(server)
        server:exec(function(alias)
            local log = require('log')
            local yaml = require('yaml')
            local config = require('config')
            log.info("%s.config():\n%s", alias, yaml.encode(config:get()))
        end, {server.alias})
    end)
end

local function cluster_test_state_update_leader(cl)
    checks('table')
    local cc = cl:config()
    local rs1 = cc.groups['group-001'].replicasets['replicaset-001']
    local failover = rs1.replication.failover
    if failover == 'manual' then
        cl.test.state.leader = rs1.leader or '???1'
    elseif failover == 'election' then
        local alias1, _ = next(rs1.instances)
        local election = cl[alias1]:exec(function ()
            return box.info.election
        end)
        cl.test.state.leader = election.leader_name or '???2'
    else
        cl.test.state.leader = '???3'
    end
end

--- Wait until every node is connected to every other node in the replica set.
--- Updates cluster.test.state.leader.
--
-- @tab[opt] opts Table with the entries listed below. (optional)
-- @number[opt] opts.timeout Timeout in seconds to wait for full mesh.
--   Defaults to 60.
-- @number[opt] opts.delay Delay in seconds between attempts to check full mesh.
--   Defaults to 0.1.
-- function ReplicaSet:wait_for_fullmesh(opts)
local function cluster_wait_for_fullmesh(cl, opts)
    checks('table', {timeout = '?number', delay = '?number'})
    if not opts then opts = {} end
    local config = {timeout = opts.timeout or 60, delay = opts.delay or 0.1}
    local instances = cluster_collect_instances(cl)
    helpers.retrying(config, function(cluster, instances)
        for _, server1_alias in ipairs(instances) do
            for _, server2_alias in ipairs(instances) do
                if server1_alias ~= server2_alias then
                    local server1 = cluster[server1_alias]
                    local server2 = cluster[server2_alias]
                    local server1_id = server1:get_instance_id()
                    local server2_id = server2:get_instance_id()
                    if server1_id ~= server2_id then
                        server1:assert_follows_upstream(server2_id)
                    else
                        -- If IDs are equal, nodes are anonymous replicas and
                        -- not registered yet. Raise an error to retry checking
                        -- full mesh again.
                        error()
                    end
                end
            end
        end
    end, cl, instances)

    cluster_test_state_update_leader(cl)
    log.info('Full mesh is ready in cluster %s leader is %s',
        json.encode(instances), cl.test.state.leader)
end

local function cluster_close_net_box_conns(cl, stored_to)
    checks('table', 'string')
    if cl[stored_to] == nil then
        return
    end

    for _, v in pairs(cl[stored_to]) do
        v:close()
    end
    cl[stored_to] = nil
end

-- }}} Cluster helpers section.

-- Tests section.

local g = t.group()

-- {{{ net.box connection tests.

local function cluster_check_net_box_conn(cluster,
                                          conn_opts, checker, store_to)
    checks('table', '?table', '?string', '?string')
    local caller_loc = utils.get_caller_loc(1)
    if store_to ~= nil then
        cluster[store_to] = {}
    end
    cluster:each(function(server)
        local uri = server_net_box_uri_with_opts(server, conn_opts)
        local creds = server_net_box_creds_with_opts(server, conn_opts)
        log.info('cluster_check_net_box_conn at %s'..
            ' for %s checker:%s uri:%s creds:%s',
            caller_loc,
            server.alias, checker,
            json.encode(uri), json.encode(creds))
        local conn = netbox.connect(uri, creds, {
            wait_connected = true,
            fetch_schema = false
        })
        if checker ~= nil then
            utils.conn_checkers[checker](conn)
        end
        if store_to == nil then
            conn:close()
        else
            cluster[store_to][server.alias] = conn
        end
    end)
end

-- Checks existing conn to server is closed
-- after security options changed and 'cfg_conn_reset_option' is enabled.
-- Note: the option and closing are not implemented yet (gh-1660).
g.test_net_box_conn = function()
    t.skip_if(want_to_skip.test_net_box_conn)
    local tc = test_cfg.test_net_box_conn
    local cn_reset_enabled = common.cfg_conn_reset_option

    log.info('Start with plain transport.')
    -- Default cbuilder config provides client:secret user with super role.
    -- It is used by default by cluster's server net_box connection.
    -- We can eval with this user in cluster_check_net_box_conn.
    local cfg_plain = cluster_config_base({instances = {'i1'}})
    local cl = cluster_new(cfg_plain, {
        debug_log_net_box = tc.debug_log_net_box,
        debug_log_conf = tc.debug_log_conf,
    })
    cl:start({wait_until_ready = false})
    cluster_wait_until_ready(cl)
    log.info('Done start with plain transport.')

    -- Test new conn.
    -- Non SSL: OK.
    cluster_check_net_box_conn(cl, {'plain'}, 'assert_conn_active', 'old_cn')
    -- SSL: error.
    cluster_check_net_box_conn(cl, {'ssl'}, 'assert_conn_error')

    log.info('Reload with SSL certs, no CA.')
    local cfg_ssl0 = cbuilder:new(cfg_plain)
        :set_global_option('iproto.listen', {SSL_SOCK})
        :set_instance_option('i1', 'iproto.ssl', CERTS.NC_CA1_S1)
        :config()
    cluster_reload(cl, cfg_ssl0, {'ssl'})
    log.info('Done reload with SSL certs, no CA.')

    -- Test old conn became invalid.
    utils.assert_conn_reset_if_enabled(cl.old_cn.i1, cn_reset_enabled)
    cluster_close_net_box_conns(cl, 'old_cn')
    -- Test new conn.
    -- Non SSL: error.
    cluster_check_net_box_conn(cl, {'plain'}, 'assert_conn_error')
    -- SSL, client has no certs: OK.
    cluster_check_net_box_conn(cl, {'ssl'}, 'assert_conn_active', 'old_cn')
    -- SSL, client with CA1 certs, no client CA: OK, server don't check.
    cluster_check_net_box_conn(cl, CERTS.NC_CA1_C1, 'assert_conn_active')
    -- SSL, client with (other) CA2 certs, no client CA: OK, server don't check.
    cluster_check_net_box_conn(cl, CERTS.NC_CA2_C1, 'assert_conn_active')
    -- SSL, client with CA1 certs, CA1: OK, client trust.
    cluster_check_net_box_conn(cl, CERTS.CA1_C1, 'assert_conn_active')
    -- SSL, client with CA2 certs, CA2: error, client don't trust.
    cluster_check_net_box_conn(cl, CERTS.CA2_C1, 'assert_conn_error')

    log.info('Reload with SSL certs from CA1.')
    local cfg_ssl1 = cbuilder:new(cfg_plain)
        :set_global_option('iproto.listen', {SSL_SOCK})
        :set_instance_option('i1', 'iproto.ssl', CERTS.CA1_S1)
        :config()
    cluster_reload(cl, cfg_ssl1, CERTS.CA1_C1)
    log.info('Done reload with SSL certs from CA1.')

    -- Test old conn became invalid.
    utils.assert_conn_reset_if_enabled(cl.old_cn.i1, cn_reset_enabled)
    cluster_close_net_box_conns(cl, 'old_cn')
    -- Test new conn.
    -- Non SSL: error.
    cluster_check_net_box_conn(cl, {'plain'}, 'assert_conn_error')
    -- SSL, client has no certs: error.
    cluster_check_net_box_conn(cl, {'ssl'}, 'assert_conn_error')
    -- SSL, client with CA1 certs, no client CA: OK, server trust.
    cluster_check_net_box_conn(cl, CERTS.NC_CA1_C1, 'assert_conn_active')
    -- SSL, client with CA2 certs, no client CA: error, server don't trust.
    cluster_check_net_box_conn(cl, CERTS.NC_CA2_C1, 'assert_conn_error')
    -- SSL, client with CA1 certs, CA1: OK, client trust, server trust.
    cluster_check_net_box_conn(cl, CERTS.CA1_C1, 'assert_conn_active', 'old_cn')
    -- SSL, client with (other) CA1 certs, CA1: OK, client trust, server trust.
    cluster_check_net_box_conn(cl, CERTS.CA1_C2, 'assert_conn_active')

    log.info('Reload with SSL certs from CA2.')
    local cfg_ssl2 = cbuilder:new(cfg_plain)
        :set_global_option('iproto.listen', {SSL_SOCK})
        :set_instance_option('i1', 'iproto.ssl', CERTS.CA2_S1)
        :config()
    cluster_reload(cl, cfg_ssl2, CERTS.CA2_C1)
    log.info('Done reload with SSL certs from CA2.')

    -- Test old conn became invalid.
    utils.assert_conn_reset_if_enabled(cl.old_cn.i1, cn_reset_enabled)
    cluster_close_net_box_conns(cl, 'old_cn')
    -- Test new conn.
    -- Non SSL: error.
    cluster_check_net_box_conn(cl, {'plain'}, 'assert_conn_error')
    -- SSL, client has no certs: error.
    cluster_check_net_box_conn(cl, {'ssl'}, 'assert_conn_error')
    -- SSL, client with CA1 certs, no client CA: error, server don't trust.
    cluster_check_net_box_conn(cl, CERTS.NC_CA1_C1, 'assert_conn_error')
    -- SSL, client with CA2 certs, no client CA: OK, server trust.
    cluster_check_net_box_conn(cl, CERTS.NC_CA2_C1, 'assert_conn_active')
    -- SSL, client with CA1 certs, CA1: error, client don't trust,
    -- server don't trust.
    cluster_check_net_box_conn(cl, CERTS.CA1_C1, 'assert_conn_error')
    -- SSL, client with (other) CA1 certs, CA1: error, client don't trust,
    -- server don't trust.
    cluster_check_net_box_conn(cl, CERTS.CA1_S1, 'assert_conn_error')
    -- SSL, client with CA2 certs, CA2: OK, client trust, server trust.
    cluster_check_net_box_conn(cl, CERTS.CA2_C1, 'assert_conn_active')
    -- SSL, client with (other) CA2 certs, CA2: OK, client trust, server trust.
    cluster_check_net_box_conn(cl, CERTS.CA2_C2, 'assert_conn_active')

    cl:drop()
end

-- }}} net.box connection tests.

-- {{{ Replication and connpool tests.

local function cluster_set_box_info_repl_write_secret(cl, val)
    checks('table', 'boolean')
    cluster_error_injection_set(cl, 'ERRINJ_BOX_INFO_REPL_WRITE_SECRET', val)
end

-- Requires: full-mesh established.
-- Collect servers repl info & repl peer config,
-- check that URIs are equal if required.
local function cluster_check_repl_status(cl, opts)
    checks('table', {
        skip = '?boolean',
        check_config = '?boolean',
        gh1667_solved = '?boolean',
        gh1672_solved = '?boolean',
        gh1672_case = '?boolean',
        store_to = '?string',
        -- Log each server replication status in cluster_check_repl_status.
        -- values: 'peer' | 'all' | 'none'.
        debug_log_repl_info = '?string',
    })
    opts = opts or {}
    if opts.skip then
        log.info("cluster_check_repl_status: skipped")
        return
    end

    local check_config = opts.check_config or false
    local gh1667_solved = opts.gh1667_solved or false
    local gh1672_solved = opts.gh1672_solved or false
    local gh1672_case = opts.gh1672_case or false
    local store_to = opts.store_to
    local debug_log_repl_info = opts.debug_log_repl_info or 'none'

    local old_repl_info = store_to and cl[store_to] or nil
    if store_to ~= nil then
        cl[store_to] = {}
    end

    cluster_set_box_info_repl_write_secret(cl, true)
    local master_alias = cl.test.state.leader
    log.info("cluster_check_repl_status for %s master:%s",
        cluster_collect_instances(cl), master_alias)
    cl:each(function(server)
        local repl_info
        -- Get repl_info if needed.
        if debug_log_repl_info ~= 'none' or store_to ~= nil or check_config then
            local cfg_peers
            repl_info, cfg_peers = server:exec(function()
                local config = require('config')
                local repl_info = box.info.replication
                local cfg_peers = {}
                for alias, _ in pairs(config:instances()) do
                    cfg_peers[alias] = config:instance_uri('peer',
                        {instance = alias})
                end
                return repl_info, cfg_peers
            end)
            -- Inject cfg peer info.
            for _, s in pairs(repl_info) do
                s.cfg_peer_comp = cfg_peers[s.name]
                s.cfg_peer = utils.uri_format_config_instance_uri(
                    s.cfg_peer_comp)
            end
            -- Inject old peer info.
            if old_repl_info then
                for id, s in pairs(old_repl_info[server.alias]) do
                    if repl_info[id] and repl_info[id].upstream and
                        s.upstream then
                        local peer_changed = not utils.uri_equal(
                            repl_info[id].upstream.peer, s.upstream.peer)
                        repl_info[id].upstream.old_peer = s.upstream.peer
                        repl_info[id].upstream.peer_changed = peer_changed
                    end
                end
            end
        end

        -- Save info for next conf reload.
        if store_to then
            cl[store_to][server.alias] = repl_info
        end

        -- Prepare info text.
        local repl_info_text = ''
        if debug_log_repl_info == 'peer' then
            local t = {}
            for id, s in pairs(repl_info) do
                t[id] = {
                    id = s.id,
                    name = s.name,
                    cfg_peer = s.cfg_peer,
                    cfg_peer_comp = s.cfg_peer_comp,
                    upstream = s.upstream and {
                        peer = s.upstream.peer,
                        old_peer = s.upstream.old_peer,
                        peer_changed = s.upstream.peer_changed,
                    } or nil,
                }
            end
            repl_info_text = string.format(' (filtered)'..
                'box.info.replication:\n%s', yaml.encode(t))
        elseif debug_log_repl_info == 'all' then
            utils.set_serialize_mapping(repl_info)
            repl_info_text = string.format(' box.info.replication:\n%s',
                yaml.encode(repl_info))
        end

        local is_master = server.alias == master_alias
        log.info('cluster_check_repl_status for %q is_master:%s%s',
            server.alias, is_master, repl_info_text)

        -- Check replication peer parameters equals to defined by config.
        if check_config then
            local instances = cluster_collect_instances(cl)
            local function must_check(name)
                if gh1667_solved and gh1672_solved then
                    return true
                elseif not gh1672_solved and gh1672_case then
                    return false
                end
                -- Check gh-1667 case.
                local name_i, self_i
                for i, n in ipairs(instances) do
                    if n == server.alias then
                        self_i = i
                    elseif n == name then
                        name_i = i
                    end
                end
                t.assert(name_i ~= nil)
                t.assert(self_i ~= nil)
                return name_i > self_i
            end

            for _, repl in pairs(repl_info) do
                -- luacheck: ignore 542
                if repl.name == server.alias then
                elseif not must_check(repl.name) then
                    log.info(string.format('skip check server %s '..
                        'repl peer to %s', server.alias, repl.name))
                else -- repl.name ~= server.alias
                    local peers_eq = utils.uri_equal(
                        repl.upstream.peer, repl.cfg_peer)
                    t.assert(peers_eq, string.format(
                        'server %s repl peer to %s', server.alias, repl.name))
                end
            end
        end
    end)
    cluster_set_box_info_repl_write_secret(cl, false)
end

local g2 = t.group('replication_ssl_reconfig', {
    -- Certs changed, no CA.
    {
        set1 = {
            s1 = 'NC_CA1_S1',
            c1 = 'NC_CA1_C1',
            s2 = 'NC_CA2_S1',
            c2 = 'NC_CA2_C1',
        },
        set2 = {
            s1 = 'NC_CA2_S1',
            c1 = 'NC_CA2_C1',
            s2 = 'NC_CA1_S1',
            c2 = 'NC_CA1_C1',
        },
        i1_peer_changed = false,
    },
    -- Certs not changed, CA added.
    {
        set1 = {
            s1 = 'NC_CA1_S1',
            c1 = 'NC_CA1_C1',
            s2 = 'NC_CA1_S1',
            c2 = 'NC_CA1_C1',
        },
        set2 = {
            s1 = 'CA1_S1',
            c1 = 'CA1_C1',
            s2 = 'CA1_S1',
            c2 = 'CA1_C1',
        },
        i1_peer_changed = false,
    },
    -- Certs changed, CA changed.
    {
        set1 = {
            s1 = 'CA1_S1',
            c1 = 'CA1_C1',
            s2 = 'CA1_S1',
            c2 = 'CA1_C1',
        },
        set2 = {
            s1 = 'CA2_S1',
            c1 = 'CA2_C1',
            s2 = 'CA2_S1',
            c2 = 'CA2_C1',
        },
        i1_peer_changed = true,
    },
})

-- Check leader 'i1' ssl parameters of replication.peer updated
-- to given values after config reload. The gh-1672 cases.
g2.test_replication_ssl_reconfig = function(cg)
    t.skip_if(want_to_skip.test_replication_ssl_reconfig)
    t.skip_if(utils.tarantool_build_ndebug(), 'error injs are not available')
    local tc = test_cfg.test_replication_ssl_reconfig

    local p = cg.params
    local i1_peer_expect_changed = common.gh1672_solved and true or
        p.i1_peer_changed

    local check_repl_common_opts = {
        debug_log_repl_info = tc.debug_log_repl_info,
        store_to = 'old_repl_info',
    }

    local cfg_plain = cluster_config_base({instances = {'i1', 'i2'},
        election = false, log_level = 'info'})

    log.info('Start with SSL certs set 1.')
    local cfg_ssl0 = cbuilder:new(cfg_plain)
        :set_global_option('iproto.listen', {SSL_SOCK})
        :set_instance_option('i1', 'iproto.ssl', CERTS[p.set1.s1])
        :set_instance_option('i2', 'iproto.ssl', CERTS[p.set1.s2])
        :config()
    local cl = cluster_new(cfg_ssl0, {
        debug_log_net_box = tc.debug_log_net_box,
        debug_log_conf = tc.debug_log_conf,
    })
    cl:start({wait_until_ready = false})
    server_set_net_box_opts(cl['i1'], CERTS[p.set1.c1])
    server_set_net_box_opts(cl['i2'], CERTS[p.set1.c2])
    cluster_wait_until_ready(cl)
    cluster_wait_for_fullmesh(cl)
    log.info('Done start with SSL certs set 1.')

    -- Collect repl_info of set1.
    cluster_check_repl_status(cl, check_repl_common_opts)

    log.info('Reload with SSL certs set 2.')
    local cfg_ssl1 = cbuilder:new(cfg_plain)
        :set_global_option('iproto.listen', {SSL_SOCK})
        :set_instance_option('i1', 'iproto.ssl', CERTS[p.set2.s1])
        :set_instance_option('i2', 'iproto.ssl', CERTS[p.set2.s2])
        :config()
    cluster_reload(cl, cfg_ssl1, nil)
    cluster_zero_net_box(cl)
    server_set_net_box_opts(cl['i1'], CERTS[p.set2.c1])
    server_set_net_box_opts(cl['i2'], CERTS[p.set2.c2])
    cluster_wait_until_ready(cl)
    cluster_wait_for_fullmesh(cl)
    log.info('Done reload with SSL certs set 2.')

    -- Collect repl_info of set2.
    cluster_check_repl_status(cl, check_repl_common_opts)
    -- Check i1 to i2 peer changes.
    local i1up2 = cl.old_repl_info.i1[2].upstream
    t.assert_equals(i1up2.peer_changed, i1_peer_expect_changed)
    local u = uri.parse(i1up2.peer)
    t.assert(u ~= nil, 'uri.parse success expected')
    local chk_set = i1_peer_expect_changed and p.set2 or p.set1
    local certs = CERTS[chk_set.s1]
    local actual = {
        ssl_cert = u.params.ssl_cert_file[1],
        ssl_key = u.params.ssl_key_file[1],
        ca_file = u.params.ssl_ca_file and u.params.ssl_ca_file[1] or nil,
    }
    t.assert_equals(actual.ssl_cert, certs.ssl_cert)
    t.assert_equals(actual.ssl_key, certs.ssl_key)
    t.assert_equals(actual.ca_file, certs.ca_file)

    cl:drop()
end

-- Requires: full-mesh established.
-- From each server checks connpool:
-- 1) Test call() 'box.info{}'.
-- 2) Test connect() to all other servers, cache connections
-- to check it at next step (after reloading the config).
local function cluster_check_connpool(cl, opts)
    checks('table', {
        skip = '?boolean',
        store_to = '?string',
        -- Dump servers conf before test.
        debug_dump_conf = '?boolean',
        -- Log each server box.info after call().
        debug_log_call_info = '?boolean',
    })
    opts = opts or {}
    if opts.skip then
        log.info("cluster_check_connpool: skipped")
        return
    end

    -- Arguments to pass to the server instance.
    local a = {
        fetch_schema_stick = test_cfg
            .test_replication_and_connpool_ssl_reconfig
            .fetch_schema_stick,
        cn_reset_enabled = common.cfg_conn_reset_option,
        store_to = opts.store_to,
        debug_log_call_info = opts.debug_log_call_info or false,
        instances = cluster_collect_instances(cl),
    }
    if opts.debug_dump_conf then cluster_servers_dump_config(cl) end

    log.info("cluster_check_connpool for %s", a.instances)
    cl:each(function(server)
        log.info("cluster_check_connpool for %q", server.alias)
        server:exec(function(args, self_alias)
            local connpool = require('experimental.connpool')
            local log = require('log')
            local yaml = require('yaml')
            local json = require('json')
            local utils = require('ssl_config_test_utils')

            -- Prepare table to cache old conns.
            if args.store_to then
                if rawget(_G, args.store_to) == nil then
                    rawset(_G, args.store_to, {})
                end
            end

            -- Check call box.info{} on remote.
            local function call(self, remote)
                log.info("cluster_check_connpool: %q calls %q", self, remote)

                local c_fn = 'box.info'
                local c_args = {}
                local c_opts = {instances = {remote}, is_async = false}
                local ok, info = pcall(connpool.call, c_fn, c_args, c_opts)
                if args.debug_log_call_info then
                    if ok then utils.replace_mt_with_serialize_mapping(info) end
                    local info_dump = yaml.encode(info)
                    log.info("cluster_check_connpool: "..
                        "%q call %q fn:%q args:%s opts:%s -> ok:%s\n%s",
                        self, remote,
                        c_fn, json.encode(c_args), json.encode(c_opts),
                        ok, info_dump)
                end
                t.assert(ok, 'success call expected')
                t.assert_equals(info.name, remote)
            end

            -- Connect remote & check conn, store it and check old conn.
            local function connect(remote)
                -- We must not close conn ourselves,
                -- because it returned from connpool cache.
                local conn = connpool.connect(remote, {fetch_schema = false})
                utils.conn_checkers.assert_conn_active(conn)
                if args.store_to == nil then
                    return
                end
                -- else args.store_to ~= nil
                local old_conn = _G[args.store_to][remote]
                _G[args.store_to][remote] = conn
                if old_conn then
                    utils.assert_conn_reset_if_enabled(old_conn,
                        args.cn_reset_enabled)
                end
            end

            for _, i in ipairs(args.instances) do
                if i ~= self_alias then
                    -- If call come after connect(),
                    -- fetch_schema_stick became useless.
                    call(self_alias, i)
                    connect(i)
                end
            end
        end, {a, server.alias})
    end)
end

-- Check repl peer conn ssl parameters are equal to config values
-- after the config was reloaded.
-- Check connpool old connections became invalid.
g.test_replication_and_connpool_ssl_reconfig = function()
    t.skip_if(want_to_skip.test_replication_and_connpool_ssl_reconfig)
    local tc = test_cfg.test_replication_and_connpool_ssl_reconfig
    -- Error injs are not available -> skip repl test.
    if utils.tarantool_build_ndebug() then
        tc.mode = 'connpool'
    end

    local check_repl_common_opts = {
        skip = tc.mode == 'connpool',
        debug_log_repl_info = tc.debug_log_repl_info,
        -- Informative only, not used in this test.
        store_to = 'old_repl_info',
    }
    local check_connpool_opts = {
        skip = tc.mode == 'repl',
        debug_dump_conf = tc.debug_dump_conf,
        debug_log_call_info = tc.debug_log_call_info,
        store_to = 'old_connpool_cn',
    }

    local cfg_base = cluster_config_base({instances = {'i1', 'i2', 'i3'},
        election = false})

    log.info('Start with plain transport.')
    local cfg_plain = cbuilder:new(cfg_base)
        -- Connpool uses replication connections, so
        -- we need to grant some privileges to replication user
        -- to allow box.info calls and evals in cluster_check_connpool.
        :set_global_option('credentials.users.replicator.privileges', {
            {
                lua_call = {'box.info'}, permissions = {'execute'},
            },
            {
                lua_eval = true, permissions = {'execute'},
            },
        })
        -- To reduce connection error rate in log, set more than 0.1.
        :set_global_option('replication.timeout', 0.1)
        :config()
    local cl = cluster_new(cfg_plain, {
        debug_log_net_box = tc.debug_log_net_box,
        debug_log_conf = tc.debug_log_conf,
    })
    cl:start({wait_until_ready = false})
    cluster_wait_until_ready(cl)
    cluster_add_package_path(cl, ('%s/?.lua'):format(test_suite_dir))
    cluster_wait_for_fullmesh(cl)
    log.info('Done start with plain transport.')

    cluster_check_repl_status(cl, fun.chain({
        check_config = true,
        -- Don't skip checks due to startup.
        gh1667_solved = true,
        -- Don't skip checks due to startup.
        gh1672_solved = true,
        gh1672_case = false,
    }, check_repl_common_opts):tomap())
    cluster_check_connpool(cl, check_connpool_opts)

    log.info('Reload with SSL certs, no CA.')
    local cfg_ssl0 = cbuilder:new(cfg_plain)
        :set_global_option('iproto.listen', {SSL_SOCK})
        :set_instance_option('i1', 'iproto.ssl', CERTS.NC_CA1_S1)
        :set_instance_option('i2', 'iproto.ssl', CERTS.NC_CA1_S2)
        :set_instance_option('i3', 'iproto.ssl', CERTS.NC_CA1_S3)
        :config()
    cluster_reload(cl, cfg_ssl0, {'ssl'})
    cluster_wait_for_fullmesh(cl)
    log.info('Done reload with SSL certs, no CA.')

    cluster_check_repl_status(cl, fun.chain({
        check_config = true,
        gh1667_solved = common.gh1667_solved,
        gh1672_solved = common.gh1672_solved,
        gh1672_case = false,
    }, check_repl_common_opts):tomap())
    cluster_check_connpool(cl, check_connpool_opts)

    log.info('Reload with SSL certs from CA1.')
    local cfg_ssl1 = cbuilder:new(cfg_plain)
        :set_global_option('iproto.listen', {SSL_SOCK})
        :set_instance_option('i1', 'iproto.ssl', CERTS.CA1_S1)
        :set_instance_option('i2', 'iproto.ssl', CERTS.CA1_S2)
        :set_instance_option('i3', 'iproto.ssl', CERTS.CA1_S3)
        :config()
    cluster_reload(cl, cfg_ssl1, CERTS.CA1_C1)
    cluster_wait_for_fullmesh(cl)
    log.info('Done reload with SSL certs from CA1.')

    cluster_check_repl_status(cl, fun.chain({
        check_config = true,
        gh1667_solved = common.gh1667_solved,
        gh1672_solved = common.gh1672_solved,
        gh1672_case = true,
    }, check_repl_common_opts):tomap())
    cluster_check_connpool(cl, check_connpool_opts)

    log.info('Reload with SSL certs from CA2.')
    local cfg_ssl2 = cbuilder:new(cfg_plain)
        :set_global_option('iproto.listen', {SSL_SOCK})
        :set_instance_option('i1', 'iproto.ssl', CERTS.CA2_S1)
        :set_instance_option('i2', 'iproto.ssl', CERTS.CA2_S2)
        :set_instance_option('i3', 'iproto.ssl', CERTS.CA2_S3)
        :config()
    cluster_reload(cl, cfg_ssl2, CERTS.CA2_C1)
    cluster_wait_for_fullmesh(cl)
    log.info('Done reload with SSL certs from CA2.')

    cluster_check_repl_status(cl, fun.chain({
        check_config = true,
        gh1667_solved = common.gh1667_solved,
        gh1672_solved = common.gh1672_solved,
        gh1672_case = false,
    }, check_repl_common_opts):tomap())
    cluster_check_connpool(cl, check_connpool_opts)

    cl:drop()
end

-- }}} Replication and connpool tests.

local fio = require('fio')
local json = require('json')
local socket = require('socket')
local uri = require('uri')
local yaml = require('yaml')
local t = require('luatest')
local justrun = require('luatest.justrun')
local treegen = require('luatest.treegen')
local cluster_config = require('internal.config.cluster_config')

local root = fio.abspath(os.getenv('SOURCEDIR') or '.')
local config_path = fio.pathjoin(root, 'docker/config/default-config.yaml')

local function read_default_config()
    local fh = assert(fio.open(config_path, {'O_RDONLY'}))
    local data = assert(fh:read())
    fh:close()
    return yaml.decode(data)
end

local function get_available_families()
    -- Lua may resolve numeric loopback addresses without AI_ADDRCONFIG,
    -- unlike IProto. Use wildcard results to check configured families.
    local addresses = socket.getaddrinfo(nil, 0, {
        type = 'SOCK_STREAM',
        flags = {'AI_PASSIVE', 'AI_ADDRCONFIG'},
    })
    local available_families = {}
    for _, address in ipairs(addresses or {}) do
        available_families[address.family] = true
    end
    return available_families
end

local g = t.group()

-- A hostless string keeps the shipped default usable with either IP family.
g.test_default_listener_is_family_neutral = function()
    local config = read_default_config()
    cluster_config:validate(config)
    t.assert_equals(config.iproto, {listen = {{uri = '3301'}}})
end

local runtime = t.group('docker_config_listeners', {
    {family = 'ipv4'},
    {family = 'ipv6'},
    {family = 'dual'},
})

local listeners = {
    ipv4 = {{uri = '127.0.0.1:0'}},
    ipv6 = {{uri = '[::1]:0'}},
    dual = {{uri = '127.0.0.1:0'}, {uri = '[::1]:0'}},
}

local host_families = {
    ['127.0.0.1'] = 'AF_INET',
    ['::1'] = 'AF_INET6',
}

runtime.before_each(function(g)
    local available_families = get_available_families()
    for _, endpoint in ipairs(listeners[g.params.family]) do
        local host = uri.parse(endpoint.uri).host
        local family = host_families[host]
        t.skip_if(not available_families[family],
                  family .. ' is unavailable for IProto listeners')
        local probe = socket.tcp_server(host, 0, function() end)
        t.skip_if(probe == nil, host .. ' loopback is unavailable')
        probe:close()
    end
    g.dir = treegen.prepare_directory({}, {})
end)

-- Check that the hostless default accepts connections over available IP families.
g.test_default_listener_connectivity = function()
    local available_families = get_available_families()
    local wildcard_hosts = {AF_INET = '0.0.0.0', AF_INET6 = '::'}
    local expected_hosts = {}
    for host, family in pairs(host_families) do
        if available_families[family] then
            local probe = socket.tcp_server(host, 0, function() end)
            t.skip_if(probe == nil, host .. ' loopback is unavailable')
            probe:close()
            table.insert(expected_hosts, wildcard_hosts[family])
        end
    end
    t.skip_if(#expected_hosts == 0, 'No IP families available for IProto')

    local dir = treegen.prepare_directory({}, {})
    local config = read_default_config()
    config.iproto.listen[1].uri = '0'
    treegen.write_file(dir, 'config.yaml', yaml.encode(config))
    treegen.write_file(dir, 'check.lua', [[
        local t = require('luatest')
        local uri = require('uri')
        local net_box = require('net.box')
        local json = require('json')
        local expected_hosts = json.decode(os.getenv('EXPECTED_HOSTS'))
        local listen = {{uri = '0'}}
        t.assert_equals(require('config'):get('iproto.listen'), listen)
        t.assert_equals(box.cfg.listen, listen)
        t.assert_equals(require('config'):info().status, 'ready')
        t.assert_equals(box.info.status, 'running')
        local addresses = box.info.listen
        if type(addresses) == 'string' then
            addresses = {addresses}
        end
        local actual_hosts = {}
        local loopback_hosts = {['0.0.0.0'] = '127.0.0.1', ['::'] = '::1'}
        for _, address in ipairs(addresses) do
            local endpoint = uri.parse(address)
            table.insert(actual_hosts, endpoint.host)
            t.assert_gt(tonumber(endpoint.service), 0)
            local host = loopback_hosts[endpoint.host]
            t.assert_not_equals(host, nil)
            local conn = net_box.connect(host, tonumber(endpoint.service),
                                         {wait_connected = 2})
            local ok = conn:ping()
            conn:close()
            t.assert(ok)
        end
        t.assert_items_equals(actual_hosts, expected_hosts)
        os.exit(0)
    ]])
    local env = {
        TT_IPROTO_LISTEN = '',
        EXPECTED_HOSTS = json.encode(expected_hosts),
    }
    local args = {'--name', 'instance-001', '--config', 'config.yaml',
                  'check.lua'}
    local opts = {nojson = true, stderr = true, quote_args = true}
    local res = justrun.tarantool(dir, env, args, opts)
    t.assert_equals(res.exit_code, 0, res.stderr)
end

-- Docker users rely on listener overrides without editing the shipped YAML.
runtime.test_listen_from_env = function(g)
    treegen.write_file(g.dir, 'check.lua', [[
        local t = require('luatest')
        local uri = require('uri')
        local net_box = require('net.box')
        local listen = require('json').decode(os.getenv('TT_IPROTO_LISTEN'))
        t.assert_equals(require('config'):get('iproto.listen'), listen)
        t.assert_equals(box.cfg.listen, listen)
        t.assert_equals(require('config'):info().status, 'ready')
        t.assert_equals(box.info.status, 'running')
        local addresses = box.info.listen
        if type(addresses) == 'string' then
            addresses = {addresses}
        end
        t.assert_equals(#addresses, #listen)
        local expected_hosts, actual_hosts = {}, {}
        for _, endpoint in ipairs(listen) do
            table.insert(expected_hosts, uri.parse(endpoint.uri).host)
        end
        for _, address in ipairs(addresses) do
            local endpoint = uri.parse(address)
            table.insert(actual_hosts, endpoint.host)
            t.assert_not_equals(endpoint.service, '0')
            local conn = net_box.connect(address, {wait_connected = 2})
            local ok = conn:ping()
            conn:close()
            t.assert(ok)
        end
        t.assert_items_equals(actual_hosts, expected_hosts)
        os.exit(0)
    ]])
    local env = {TT_IPROTO_LISTEN = json.encode(listeners[g.params.family])}
    local args = {'--name', 'instance-001', '--config', config_path,
                  'check.lua'}
    local opts = {nojson = true, stderr = true, quote_args = true}
    local res = justrun.tarantool(g.dir, env, args, opts)
    t.assert_equals(res.exit_code, 0, res.stderr)
end

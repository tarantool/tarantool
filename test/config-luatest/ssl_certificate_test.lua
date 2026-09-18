-- tags: parallel

local fio = require('fio')
local t = require('luatest')

local g = t.group()

local test_suite_dir = debug.__dir__
local CERT = fio.pathjoin(test_suite_dir, '..', 'ssl_cert', 'server.crt')
local MISSING_CERT = fio.pathjoin(test_suite_dir, 'no-such-certificate.pem')
local CERT_EXPIRATION_DATE = '2121-12-04T11:58:50Z'

local function get_upvalue(func, key)
    for i = 1, debug.getinfo(func).nups do
        local name, value = debug.getupvalue(func, i)
        if name == key then
            return value, i
        end
    end
    error(('SSL certificate checker %s upvalue not found'):format(key))
end

local function set_upvalue(func, key, value)
    local _, i = get_upvalue(func, key)
    debug.setupvalue(func, i, value)
end

g.before_all(function(g)
    local ssl_certificate = require('internal.ssl_certificate')
    local discoveries = get_upvalue(ssl_certificate.register_discovery,
                                    'discoveries')
    g.default_discoveries = table.copy(discoveries)
end)

g.before_each(function(g)
    -- Mocks and registered callbacks must not leak between test cases run
    -- in the same Tarantool process.
    local ssl_certificate = require('internal.ssl_certificate')
    set_upvalue(ssl_certificate.apply, 'alerts', nil)
    set_upvalue(ssl_certificate.register_discovery, 'discoveries',
                table.copy(g.default_discoveries))
end)

local function new_fake_config()
    local state = {
        current_cert = CERT,
        alert_values = {},
        status_updates = 0,
        fail_discovery = false,
    }
    local alert_namespace = {}
    function alert_namespace:set(key, value)
        state.alert_values[key] = value
        return true
    end
    function alert_namespace:unset(key)
        local changed = state.alert_values[key] ~= nil
        state.alert_values[key] = nil
        return changed
    end
    local config = {}
    function config:get(path)
        if state.fail_discovery then
            error('injected discovery failure')
        end
        if path == 'iproto.ssl.ssl_cert' then
            return state.current_cert
        else
            error('unexpected CE configuration path: ' .. path)
        end
    end
    function config:new_alerts_namespace()
        return alert_namespace
    end
    function config:_set_status_based_on_alerts()
        state.status_updates = state.status_updates + 1
    end
    state.config = config
    return state
end

local function apply_fake_config()
    local ssl_certificate = require('internal.ssl_certificate')
    local state = new_fake_config()
    ssl_certificate.apply(state.config)
    return ssl_certificate, state
end

g.test_alert_lifecycle = function()
    local ssl_certificate, state = apply_fake_config()
    t.assert_equals(next(state.alert_values), nil)
    t.assert_equals(ssl_certificate._constants.check_interval, 12 * 60 * 60)

    local expiration = ssl_certificate._read_expiration(CERT)
    local threshold = ssl_certificate._constants.alert_threshold
    local expiration_date = os.date('!%Y-%m-%dT%H:%M:%SZ', expiration)
    t.assert_equals(expiration_date, CERT_EXPIRATION_DATE)
    ssl_certificate._check(expiration - threshold - 1)
    t.assert_equals(next(state.alert_values), nil)
    ssl_certificate._check(expiration - threshold)
    local _, alert = next(state.alert_values)
    t.assert_str_contains(alert.message,
                          ('expires at %s'):format(expiration_date))
    t.assert_equals(alert.expiration_timestamp, expiration)
    ssl_certificate._check(expiration + 1)
    _, alert = next(state.alert_values)
    t.assert_str_contains(alert.message,
                          ('expired at %s'):format(expiration_date))
    t.assert_equals(alert.expiration_timestamp, expiration)
    ssl_certificate._check()
    t.assert_equals(next(state.alert_values), nil)
end

g.test_checker_failure_recovery = function()
    local ssl_certificate, state = apply_fake_config()
    state.fail_discovery = true
    t.assert_not(ssl_certificate._check_noexcept())
    t.assert_not_equals(next(state.alert_values), nil)
    state.fail_discovery = false
    t.assert(ssl_certificate._check_noexcept())
    t.assert_equals(next(state.alert_values), nil)
end

g.test_certificate_discovery = function()
    local ssl_certificate, state = apply_fake_config()
    local records = ssl_certificate._discover()
    t.assert(records[fio.abspath(CERT)].components.iproto)
    state.current_cert = nil
    t.assert_equals(ssl_certificate._discover(), {})
end

g.test_discovery_registration = function()
    local ssl_certificate, state = apply_fake_config()
    local calls = {}
    ssl_certificate.register_discovery('first', function(config, records, add)
        t.assert_equals(config, state.config)
        table.insert(calls, 'first')
        add(records, CERT, 'first')
    end)
    ssl_certificate.register_discovery('second', function(config, records, add)
        t.assert_equals(config, state.config)
        table.insert(calls, 'second')
        add(records, MISSING_CERT, 'second')
    end)

    local records = ssl_certificate._discover()
    t.assert_equals(calls, {'first', 'second'})
    t.assert_equals(records[fio.abspath(CERT)].components,
                    {iproto = true, first = true})
    t.assert_equals(records[fio.abspath(MISSING_CERT)].components,
                    {second = true})

    ssl_certificate._check()
    local _, alert = next(state.alert_values)
    t.assert_equals(alert.path, fio.abspath(MISSING_CERT))
    t.assert_equals(alert.components, {'second'})
end

g.test_discovery_registration_rejects_duplicate = function()
    local ssl_certificate = apply_fake_config()
    local calls = 0
    local function discover()
        calls = calls + 1
    end
    ssl_certificate.register_discovery('test', discover)
    t.assert_error_msg_contains('already registered',
        ssl_certificate.register_discovery, 'test', function()
            error('must not replace the registered callback')
        end)
end

g.test_discovery_unregistration = function()
    local ssl_certificate, state = apply_fake_config()
    local calls = {}
    for _, name in ipairs({'first', 'second', 'third'}) do
        ssl_certificate.register_discovery(name, function(_, records, add)
            table.insert(calls, name)
            add(records, MISSING_CERT, name)
        end)
    end
    ssl_certificate._check()
    local _, alert = next(state.alert_values)
    t.assert_equals(alert.components, {'first', 'second', 'third'})
    t.assert(ssl_certificate.unregister_discovery('second'))
    t.assert_not(ssl_certificate.unregister_discovery('second'))
    calls = {}
    ssl_certificate._check()
    t.assert_equals(calls, {'first', 'third'})
    _, alert = next(state.alert_values)
    t.assert_equals(alert.components, {'first', 'third'})
    t.assert(ssl_certificate.unregister_discovery('first'))
    t.assert(ssl_certificate.unregister_discovery('third'))
    ssl_certificate._check()
    t.assert_equals(state.alert_values, {})
    t.assert(ssl_certificate._discover()[fio.abspath(CERT)].components.iproto)
    ssl_certificate.register_discovery('second', function(_, records, add)
        add(records, MISSING_CERT, 'replacement')
    end)
    ssl_certificate._check()
    _, alert = next(state.alert_values)
    t.assert_equals(alert.components, {'replacement'})
end

g.test_unreadable_certificate_alert_lifecycle = function()
    local ssl_certificate, state = apply_fake_config()
    state.current_cert = MISSING_CERT
    ssl_certificate._check()
    t.assert_not_equals(next(state.alert_values), nil)

    state.current_cert = nil
    ssl_certificate._check()
    t.assert_equals(next(state.alert_values), nil)
    t.assert_gt(state.status_updates, 0)
end

g.test_null_configuration = function()
    local ssl_certificate, state = apply_fake_config()
    state.current_cert = box.NULL
    ssl_certificate._check()
    t.assert_equals(next(state.alert_values), nil)

    state.current_cert = nil
    ssl_certificate._check()
    t.assert_equals(next(state.alert_values), nil)
end

g.test_config_startup_and_reload = function()
    local cbuilder = require('luatest.cbuilder')
    local justrun = require('luatest.justrun')
    local treegen = require('luatest.treegen')
    local yaml = require('yaml')
    local config = cbuilder:new()
        :set_global_option('iproto.ssl.ssl_cert', MISSING_CERT)
        :set_global_option('app.file', 'main.lua')
        :add_instance('i-001', {})
        :config()
    local script = [[
        local config = require('config')
        local function check()
            local info = config:info()
            assert(info.status == 'check_warnings')
            for _, alert in ipairs(info.alerts) do
                if alert.message:find('Unable to check SSL certificate',
                                      1, true) then
                    return
                end
            end
            error('certificate warning is missing')
        end
        require('fiber').create(function()
            require('fiber').sleep(0)
            check()
            config:reload()
            check()
            os.exit(0)
        end)
    ]]
    local dir = treegen.prepare_directory({}, {})
    treegen.write_file(dir, 'config.yaml', yaml.encode(config))
    treegen.write_file(dir, 'main.lua', script)
    local res = justrun.tarantool(dir, {}, {
        '--name', 'i-001', '--config', 'config.yaml',
    }, {nojson = true, stderr = true})
    t.assert_equals(res.exit_code, 0, res.stderr)
end

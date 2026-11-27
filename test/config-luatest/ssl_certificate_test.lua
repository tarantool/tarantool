-- tags: parallel

local fio = require('fio')
local datetime = require('datetime')
local t = require('luatest')
local cbuilder = require('luatest.cbuilder')
local cluster = require('luatest.cluster')
local server = require('luatest.server')

-- Whether the truncated SSL parameter names are accepted
-- alongside the full ones. To remove in Tarantool 4.x.
local SSL_PARAMS_ALLOW_DOUBLE_NAMES = true

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

g.after_each(function(g)
    if g.server ~= nil then
        g.server:drop()
        g.server = nil
    end
    if g.temp_dir ~= nil then
        fio.rmtree(g.temp_dir)
        g.temp_dir = nil
    end
end)

local function new_fake_config()
    local state = {
        -- SSL_PARAMS_ALLOW_DOUBLE_NAMES
        -- By default only full name is defined.
        current_cert_file = CERT, -- ssl_cert_file value
        current_cert = nil,       -- ssl_cert value
        alert_values = {},
        status_updates = 0,
        fail_discovery = false,
    }
    local alert_namespace = {}
    function alert_namespace:set(key, value)
        local alert = table.copy(value)
        alert.type = alert.type or 'warn'
        state.alert_values[key] = alert
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
        if path == 'iproto.ssl.ssl_cert_file' then
            return state.current_cert_file
        elseif SSL_PARAMS_ALLOW_DOUBLE_NAMES and
               path == 'iproto.ssl.ssl_cert' then
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
    local expiration_date = datetime.new({
        timestamp = expiration,
        tzoffset = 0,
    }):format()
    t.assert_equals(expiration_date, CERT_EXPIRATION_DATE)
    t.assert_equals(ssl_certificate._constants.expiry_warning_period,
                    30 * 24 * 60 * 60)
    t.assert_equals(threshold, 30 * 24 * 60 * 60 + 12 * 60 * 60)
    ssl_certificate._check(expiration - threshold - 1)
    t.assert_equals(next(state.alert_values), nil)
    ssl_certificate._check(expiration - threshold)
    local _, alert = next(state.alert_values)
    t.assert_str_contains(alert.message,
                          ('expires at %s'):format(expiration_date))
    t.assert_equals(alert.type, 'warn')
    t.assert_equals(alert.expiration_timestamp, expiration)
    ssl_certificate._check(expiration)
    _, alert = next(state.alert_values)
    t.assert_equals(alert.type, 'warn')
    ssl_certificate._check(expiration + 1)
    _, alert = next(state.alert_values)
    t.assert_str_contains(alert.message,
                          ('expired at %s'):format(expiration_date))
    t.assert_equals(alert.type, 'warn')
    t.assert_equals(alert.expiration_timestamp, expiration)
    ssl_certificate._check()
    t.assert_equals(next(state.alert_values), nil)
end

g.test_checker_failure_recovery = function()
    local ssl_certificate, state = apply_fake_config()
    state.fail_discovery = true
    t.assert_not(ssl_certificate._check_noexcept())
    local _, alert = next(state.alert_values)
    t.assert_equals(alert.type, 'warn')
    state.fail_discovery = false
    t.assert(ssl_certificate._check_noexcept())
    t.assert_equals(next(state.alert_values), nil)
end

g.test_certificate_discovery = function()
    local ssl_certificate, state = apply_fake_config()
    local records = ssl_certificate._discover()
    t.assert(records[fio.abspath(CERT)].components.iproto)
    state.current_cert_file = nil
    t.assert_equals(ssl_certificate._discover(), {})
end

-- Check short name ('ssl_cert') defined only case.
g.test_certificate_discovery_short_cert_name = function()
    t.skip_if(not SSL_PARAMS_ALLOW_DOUBLE_NAMES)
    local ssl_certificate, state = apply_fake_config()
    state.current_cert = state.current_cert_file
    state.current_cert_file = nil
    local records = ssl_certificate._discover()
    t.assert(records[fio.abspath(CERT)].components.iproto)
    state.current_cert = nil
    t.assert_equals(ssl_certificate._discover(), {})
end

-- Check both full ('ssl_cert_file') and short name ('ssl_cert')
-- defined case. The full name overrides short.
g.test_certificate_discovery_both_cert_name = function()
    t.skip_if(not SSL_PARAMS_ALLOW_DOUBLE_NAMES)
    local ssl_certificate, state = apply_fake_config()
    state.current_cert = MISSING_CERT
    local records = ssl_certificate._discover()
    t.assert(records[fio.abspath(CERT)].components.iproto)
    state.current_cert_file = nil
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
    state.current_cert_file = MISSING_CERT
    ssl_certificate._check()
    local _, alert = next(state.alert_values)
    t.assert_equals(alert.type, 'warn')

    state.current_cert_file = nil
    ssl_certificate._check()
    t.assert_equals(next(state.alert_values), nil)
    t.assert_gt(state.status_updates, 0)
end

g.test_null_configuration = function()
    local ssl_certificate, state = apply_fake_config()
    state.current_cert_file = box.NULL
    ssl_certificate._check()
    t.assert_equals(next(state.alert_values), nil)

    state.current_cert_file = nil
    ssl_certificate._check()
    t.assert_equals(next(state.alert_values), nil)
end

g.test_config_startup_and_reload = function()
    local cbuilder = require('luatest.cbuilder')
    local justrun = require('luatest.justrun')
    local treegen = require('luatest.treegen')
    local yaml = require('yaml')
    local config = cbuilder:new()
        :set_global_option('iproto.ssl.ssl_cert_file', MISSING_CERT)
        :set_global_option('app.file', 'main.lua')
        :add_instance('i-001', {})
        :config()
    local script = [[
        local config = require('config')
        -- The reload below executes this file again.
        if config:info().status == 'reload_in_progress' then
            return
        end
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
        local fiber = require('fiber')
        fiber.create(function()
            local ok, err = pcall(function()
                fiber.sleep(0)
                check()
                config:reload()
                check()
            end)
            if not ok then
                io.stderr:write(tostring(err), '\n')
            end
            os.exit(ok and 0 or 1)
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

g.test_periodic_check_during_reload = function()
    local config = cbuilder:new()
        :set_global_option('iproto.ssl.ssl_cert_file', MISSING_CERT)
        :add_instance('i-001', {})
        :config()
    local c = cluster:new(config)
    c:start()

    c['i-001']:exec(function()
        local config = require('config')
        local fiber = require('fiber')
        local ssl_certificate = require('internal.ssl_certificate')
        local t = require('luatest')

        t.assert_equals(config:info().status, 'check_warnings')

        local cond = fiber.cond()
        local apply = config._apply
        config._apply = function(self)
            cond:wait()
            return apply(self)
        end

        local reload = fiber.new(config.reload, config)
        reload:set_joinable(true)
        fiber.yield()
        t.assert_equals(config:info().status, 'reload_in_progress')

        ssl_certificate._check_noexcept()
        local status_after_check = config:info().status

        config._apply = apply
        local ok, err = pcall(config.reload, config)

        cond:signal()
        t.assert(reload:join())
        t.assert_equals(status_after_check, 'reload_in_progress')
        t.assert_not(ok)
        t.assert_equals(err, 'config:reload(): instance ' ..
                             'configuration is already in progress')
        t.assert_equals(config:info().status, 'check_warnings')
    end)
end

g.test_certificate_read_does_not_block_event_loop = function(g)
    local popen = require('popen')
    g.server = server:new({alias = 'ssl_reader'})
    g.server:start()
    local fifo = fio.pathjoin(g.server.workdir, 'certificate.fifo')
    local mkfifo = assert(popen.new({'/usr/bin/env', 'mkfifo', fifo}))
    local status = mkfifo:wait()
    mkfifo:close()
    t.assert_equals(status.exit_code, 0)

    local fh = assert(fio.open(CERT, {'O_RDONLY'}))
    local pem = assert(fh:read())
    assert(fh:close())
    local expected = g.server:exec(function(cert)
        return require('internal.ssl_certificate')._read_expiration(cert)
    end, {CERT})

    local future = g.server:eval([[
        return require('internal.ssl_certificate')._read_expiration(...)
    ]], {fifo}, {is_async = true})
    local writer = assert(fio.open(fifo, {'O_WRONLY'}))
    local pending = not future:is_ready()
    local ok, value = pcall(g.server.eval, g.server, 'return 42', {},
                           {timeout = 5})

    -- Release the reader even if the second request timed out.
    local written, write_err = writer:write(pem)
    local closed, close_err = writer:close()
    local result, read_err = future:wait_result(5)
    t.assert(written, tostring(write_err))
    t.assert(closed, tostring(close_err))
    t.assert(pending, 'certificate read must still be pending')
    t.assert(ok, tostring(value))
    t.assert_equals(value, 42)
    t.assert(result, tostring(read_err))
    t.assert_equals(result[1], expected)
end

g.test_read_expiration_failures = function(g)
    local ffi = require('ffi')
    local ssl_certificate = require('internal.ssl_certificate')
    g.temp_dir = fio.tempdir()
    local path = fio.pathjoin(g.temp_dir, 'invalid.pem')
    for _, contents in ipairs({'', 'not a certificate'}) do
        local fh = assert(fio.open(path, {'O_CREAT', 'O_TRUNC', 'O_WRONLY'},
                                  tonumber('600', 8)))
        assert(fh:write(contents))
        assert(fh:close())
        local expiration, err = ssl_certificate._read_expiration(path)
        t.assert_equals(expiration, nil)
        t.assert_str_contains(err, 'Error loading SSL certificate')
    end

    for _, unreadable in ipairs({g.temp_dir, MISSING_CERT}) do
        local expiration, err = ssl_certificate._read_expiration(unreadable)
        t.assert_equals(expiration, nil)
        t.assert_equals(type(err), 'string')
    end

    local timestamp = ffi.new('int64_t[1]', 42)
    t.assert_equals(ffi.C.ssl_cert_get_not_after('', 2147483648, timestamp), -1)
    t.assert_equals(tonumber(timestamp[0]), 0)
    t.assert_str_contains(tostring(box.error.last()), 'too large')
end

g.test_checks_are_serialized = function()
    local fiber = require('fiber')
    local ssl_certificate, state = apply_fake_config()
    local started = fiber.channel(1)
    local release = fiber.channel(1)
    local calls = 0
    local get = state.config.get
    function state.config:get(path)
        calls = calls + 1
        if calls == 1 then
            started:put(true)
            release:get()
        end
        return get(self, path)
    end

    local first = fiber.new(ssl_certificate._check)
    first:set_joinable(true)
    started:get()
    local second = fiber.new(ssl_certificate._check)
    second:set_joinable(true)
    fiber.sleep(0)
    local calls_before_release = calls
    release:put(true)
    local first_ok, first_err = first:join()
    local second_ok, second_err = second:join()
    t.assert(first_ok, tostring(first_err))
    t.assert(second_ok, tostring(second_err))
    t.assert_equals(calls_before_release, 1)
    t.assert_equals(calls, 2)
end

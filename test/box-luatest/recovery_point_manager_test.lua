local t = require('luatest')
local server = require('luatest.server')
local treegen = require('luatest.treegen')
local justrun = require('luatest.justrun')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.after_each(function(cg)
    cg.server:exec(function()
        t.assert_equals(next(box.backup.recovery_point.managers), nil)
    end)
end)

g.test_api_surface = function(cg)
    cg.server:exec(function()
        local rp = box.backup.recovery_point
        t.assert_type(rp.manager_create, 'function')
        t.assert_type(rp.manager_drop, 'function')
        t.assert_type(rp.managers, 'table')
        t.assert_type(rp.create, 'function')

        local backend = {new = function() return {
            create_point = function() return {} end,
        } end}
        local m = rp.manager_create('surface', {backend = backend})
        t.assert_type(m.cfg, 'function')
        t.assert_type(m.create_point, 'function')
        t.assert_type(m.drop, 'function')
        t.assert_type(m.info, 'function')
        m:drop()
    end)
end

g.test_lifecycle_and_periodic = function(cg)
    cg.server:exec(function()
        local uuid = require('uuid')
        local rp = box.backup.recovery_point

        local created = {}
        local last_opts
        local new_cfg
        local backend = {
            new = function(cfg)
                new_cfg = cfg
                return setmetatable({}, {__index = {
                    create_point = function(_, id, opts)
                        last_opts = opts
                        table.insert(created, id)
                        return {id = id}
                    end,
                }})
            end,
        }

        local m = rp.manager_create('m1', {
            backend = backend,
            backend_cfg = {foo = 'bar'},
            create_interval = 0.01,
            timeout = 7,
        })
        t.assert_equals(rp.managers['m1'], m)
        -- The manager constructed the backend instance with the backend_cfg.
        t.assert_equals(new_cfg, {foo = 'bar'})

        -- The loop creates points periodically.
        t.helpers.retrying({timeout = 10}, function()
            t.assert_ge(#created, 3)
        end)
        -- A fresh uuid (hence a fresh id) per attempt.
        t.assert_not_equals(created[1], created[2])
        -- The per-attempt timeout is forwarded to the backend.
        t.assert_equals(last_opts.timeout, 7)
        -- Id format: <instance>.<manager>.<uuid>.
        local instance = type(box.info.name) == 'string' and box.info.name
                         or box.info.uuid
        for _, id in ipairs(created) do
            local prefix, point_uuid = id:match('^(.+)%.m1%.([^.]+)$')
            t.assert_equals(prefix, instance)
            t.assert_not_equals(uuid.fromstr(point_uuid), nil)
        end

        -- A manual create_point() makes one more point affecting the loop.
        local before = #created
        local res = m:create_point()
        t.assert_equals(res.id, created[#created])
        t.assert_gt(#created, before)

        m:drop()
        t.assert_equals(rp.managers['m1'], nil)
    end)
end

g.test_id_instance_name = function()
    local named = server:new({box_cfg = {instance_name = 'rpm-named'}})
    named:start()
    named:exec(function()
        local rp = box.backup.recovery_point
        t.assert_equals(box.info.name, 'rpm-named')

        local created
        local backend = {new = function()
            return setmetatable({}, {__index = {
                create_point = function(_, id)
                    created = id
                    return {id = id}
                end,
            }})
        end}
        local m = rp.manager_create('named', {backend = backend,
                                              create_interval = 100})
        m:create_point()
        -- The instance segment is the name, not the uuid.
        local prefix = created:match('^(.+)%.named%.[^.]+$')
        t.assert_equals(prefix, 'rpm-named')
        m:drop()
    end)
    named:drop()
end

g.test_failure_backoff_and_alert = function(cg)
    cg.server:exec(function()
        local rp = box.backup.recovery_point

        -- The manager's alert in box.info.config.alerts, if any. The manager
        -- names itself in the alert message, so match on it (the namespace name
        -- is internal and not exposed in the alert).
        local function config_alert(name)
            for _, alert in ipairs(box.info.config.alerts) do
                if alert.message:find(name, 1, true) then
                    return alert
                end
            end
            return nil
        end

        local mode = 'nil_err'
        local attempts = 0
        local backend = {
            new = function()
                return setmetatable({}, {__index = {
                    create_point = function()
                        attempts = attempts + 1
                        if mode == 'nil_err' then
                            return nil, 'boom'
                        end
                        return {ok = true}
                    end,
                }})
            end,
        }

        local m = rp.manager_create('mf', {
            backend = backend,
            backend_cfg = {},
            create_interval = 0.01,
            timeout = 1,
        })

        -- After enough consecutive failures the alert appears in info().alerts
        -- and is mirrored into the config alerts (even with an uninitialized
        -- config). It is not a busy loop (the backoff yields, so this takes a
        -- few seconds). The alert is computed on the fly.
        t.helpers.retrying({timeout = 60}, function()
            t.assert_not_equals(next(m:info().alerts), nil)
        end)
        t.assert_str_contains(m:info().alerts[1].message, 'mf')
        local alert = config_alert('mf')
        t.assert_not_equals(alert, nil)
        t.assert_str_contains(alert.message, 'mf')

        -- On the next success both clear (they track the live counter).
        mode = 'ok'
        t.helpers.retrying({timeout = 60}, function()
            t.assert_equals(next(m:info().alerts), nil)
        end)
        t.helpers.retrying({timeout = 60}, function()
            t.assert_equals(config_alert('mf'), nil)
        end)
        -- The last created point is surfaced in info().
        t.assert_equals(m:info().last_point, {ok = true})

        rp.manager_drop('mf')
    end)
end

g.test_info = function(cg)
    cg.server:exec(function()
        local rp = box.backup.recovery_point
        local backend_alerts = {}
        local backend = {
            new = function()
                return setmetatable({}, {__index = {
                    create_point = function(_, id) return {id = id} end,
                    info = function()
                        return {state = 'ready', alerts = backend_alerts}
                    end,
                }})
            end,
        }
        local m = rp.manager_create('mi', {backend = backend, backend_cfg = {},
                                           create_interval = 0.01})

        -- last_point reflects the last point created by the loop.
        t.helpers.retrying({timeout = 10}, function()
            t.assert_not_equals(m:info().last_point, nil)
        end)

        -- The backend's info shows under the backend key, without its alerts;
        -- with no alerts anywhere info.alerts is empty.
        local info = m:info()
        t.assert_equals(info.backend.state, 'ready')
        t.assert_equals(info.backend.alerts, nil)
        t.assert_equals(info.alerts, {})

        -- The backend's alerts are merged into info.alerts (not info.backend).
        table.insert(backend_alerts, {message = 'backend is unhappy'})
        info = m:info()
        t.assert_equals(info.backend.alerts, nil)
        t.assert_equals(#info.alerts, 1)
        t.assert_equals(info.alerts[1].message, 'backend is unhappy')

        m:drop()
    end)
end

g.test_backend_raise_is_a_failure = function(cg)
    cg.server:exec(function()
        local rp = box.backup.recovery_point
        local attempts = 0
        local backend = {
            new = function()
                return setmetatable({}, {__index = {
                    create_point = function()
                        attempts = attempts + 1
                        error('kaboom')
                    end,
                }})
            end,
        }
        rp.manager_create('mr', {
            backend = backend, backend_cfg = {},
            create_interval = 0.01, timeout = 1,
        })
        -- A raising backend is treated as a failure; the fiber survives and
        -- keeps retrying.
        t.helpers.retrying({timeout = 30}, function()
            t.assert_ge(attempts, 2)
        end)
        rp.manager_drop('mr')
    end)
end

g.test_backend_info_misbehaves = function(cg)
    cg.server:exec(function()
        local rp = box.backup.recovery_point
        local created = 0
        local mode = 'raise'
        local backend = {
            new = function()
                return setmetatable({}, {__index = {
                    create_point = function(_, id)
                        created = created + 1
                        return {id = id}
                    end,
                    info = function()
                        if mode == 'raise' then
                            error('info boom')
                        end
                        -- An alert type the board rejects (must be nil/'warn').
                        return {alerts = {{message = 'bad', type = 'error'}}}
                    end,
                }})
            end,
        }
        local m = rp.manager_create('mib', {
            backend = backend, backend_cfg = {}, create_interval = 0.01,
        })
        -- info() raises every sync, yet the loop keeps creating points, and a
        -- public :info() surfaces the failure as an alert instead of raising.
        t.helpers.retrying({timeout = 10}, function()
            t.assert_ge(created, 3)
        end)
        t.assert_str_contains(m:info().alerts[1].message,
                             'backend info() failed')

        -- A rejected-alert-type info() also survives: pushing it to the config
        -- alerts board fails, but the add is guarded so the bad alert is just
        -- dropped there -- the fiber keeps running and info() still reports it.
        local base = created
        mode = 'bad_alert'
        t.helpers.retrying({timeout = 10}, function()
            t.assert_gt(created, base + 2)
        end)
        local alerts = m:info().alerts
        t.assert_equals(#alerts, 1)
        t.assert_str_contains(alerts[1].message, 'bad')

        m:drop()
    end)
end

g.test_drop_tears_down_backend = function(cg)
    cg.server:exec(function()
        local rp = box.backup.recovery_point
        local dropped = false
        local backend = {
            new = function()
                return setmetatable({}, {__index = {
                    create_point = function() return {} end,
                    drop = function() dropped = true end,
                }})
            end,
        }
        rp.manager_create('md', {
            backend = backend, backend_cfg = {},
            create_interval = 100, timeout = 1,
        })
        rp.managers['md']:drop()
        t.assert(dropped)
        t.assert_equals(rp.managers['md'], nil)
    end)
end

g.test_misc = function(cg)
    cg.server:exec(function()
        local rp = box.backup.recovery_point
        -- A backend without a :drop method (the teardown is optional).
        local backend = {
            new = function()
                return setmetatable({}, {__index = {
                    create_point = function() return {} end,
                }})
            end,
        }

        -- manager_drop of an absent manager is a no-op.
        rp.manager_drop('absent')

        -- A manager named "create" lands at managers["create"] and never
        -- collides with the recovery_point.create() point function.
        rp.manager_create('create', {
            backend = backend, backend_cfg = {}, create_interval = 100,
        })
        t.assert_type(rp.managers['create'], 'table')
        t.assert_type(rp.create, 'function')

        -- A duplicate name is an error.
        t.assert_error_msg_contains('already exists', function()
            rp.manager_create('create', {
                backend = backend, backend_cfg = {}, create_interval = 100,
            })
        end)

        -- Drop must not error even though the backend has no :drop.
        rp.manager_drop('create')
        t.assert_equals(rp.managers['create'], nil)
    end)
end

g.test_validation = function(cg)
    cg.server:exec(function()
        local rp = box.backup.recovery_point
        -- A real backend module has fields beside new() (e.g. a `config` for
        -- the role) and its instances hold their own state (a connection, ...).
        -- Only the interface members are validated; extra fields are ignored.
        local backend = {
            config = {},
            new = function()
                return setmetatable({rs = {}}, {__index = {
                    create_point = function() return {} end,
                }})
            end,
        }
        -- Every invalid (name, cfg) is rejected and the error names the bad
        -- field. backend.new()'s result is validated right away too: it must be
        -- a table with a create_point() method, and an optional drop() must be
        -- a function.
        local cases = {
            {
                name = 42,
                cfg = {backend = backend, create_interval = 1},
                err = 'name should be a string'
            },
            {
                name = 'v',
                err = 'cfg should be a table'
            },
            {
                name = 'v',
                cfg = {create_interval = 1},
                err = 'cfg.backend should be a table'
            },
            {
                name = 'v',
                cfg = {backend = backend, create_interval = -1},
                err = "create_interval' should be of type positive number"
            },
            {
                name = 'v',
                cfg = {backend = backend, create_interval = 1, timeout = 0},
                err = "timeout' should be of type positive number"
            },
            {
                name = 'v',
                cfg = {backend = {}, create_interval = 1},
                err = 'cfg.backend.new should be a function'
            },
            {
                name = 'v',
                cfg = {backend = {new = function() end}},
                err = 'backend instance should be a table'
            },
            {
                name = 'v',
                cfg = {backend = {new = function() return {} end}},
                err = 'backend instance.create_point should be a function'
            },
            {
                name = 'v',
                cfg = {backend = {new = function()
                    return {create_point = function() end, drop = 5}
                end}},
                err = "'drop' should be of type function"
            },
        }
        for _, case in ipairs(cases) do
            t.assert_error_msg_contains(case.err, function()
                rp.manager_create(case.name, case.cfg)
            end)
        end
        -- Nothing was created by the failed attempts.
        t.assert_equals(next(rp.managers), nil)
        -- Only backend is compulsory; the rest fall back to defaults.
        local m = rp.manager_create('defaults', {backend = backend})
        t.assert_equals(m.create_interval, 600)
        t.assert_equals(m.timeout, 300)
        m:drop()
        t.assert_equals(next(rp.managers), nil)
    end)
end

-- manager:cfg() recreates the backend instance only when the backend changes;
-- scheduling-only changes are applied to the live loop.
g.test_reconfigure = function(cg)
    cg.server:exec(function()
        local rp = box.backup.recovery_point
        local new_calls = 0
        local drops = 0
        local backend = {
            new = function()
                new_calls = new_calls + 1
                return setmetatable({}, {__index = {
                    create_point = function() return {} end,
                    drop = function() drops = drops + 1 end,
                }})
            end,
        }
        local m = rp.manager_create('rc', {
            backend = backend, backend_cfg = {target = 'a'},
            create_interval = 100, timeout = 50,
        })
        local worker0 = m.worker
        local inst0 = m.backend_instance
        t.assert_equals(new_calls, 1)
        t.assert_type(m.cfg, 'function')

        -- Scheduling-only change: same fiber, same backend instance.
        m:cfg({backend = backend, backend_cfg = {target = 'a'},
               create_interval = 7, timeout = 9})
        t.assert_equals(m.create_interval, 7)
        t.assert_equals(m.timeout, 9)
        t.assert_is(m.worker, worker0)
        t.assert_is(m.backend_instance, inst0)
        t.assert_equals(new_calls, 1)
        t.assert_equals(drops, 0)

        -- backend_cfg change: the old instance is dropped and a new one is
        -- built, but the scheduling fiber is kept running.
        m:cfg({backend = backend, backend_cfg = {target = 'b'},
               create_interval = 7, timeout = 9})
        t.assert_equals(new_calls, 2)
        t.assert_equals(drops, 1)
        t.assert_is(m.worker, worker0)
        t.assert_is_not(m.backend_instance, inst0)

        -- Partial update: omitted members keep their current value. Change only
        -- the timeout; create_interval, the backend and the instance are kept.
        local inst_b = m.backend_instance
        m:cfg({timeout = 5})
        t.assert_equals(m.timeout, 5)
        t.assert_equals(m.create_interval, 7)
        t.assert_is(m.worker, worker0)
        t.assert_is(m.backend_instance, inst_b)
        t.assert_equals(new_calls, 2)
        t.assert_equals(drops, 1)

        -- A bad cfg value is rejected and the manager keeps running unchanged.
        t.assert_error_msg_contains(
            "create_interval' should be of type positive number", function()
                m:cfg({create_interval = -1})
            end)
        t.assert_not_equals(m.worker, nil)
        t.assert_equals(m.create_interval, 7)

        -- A backend whose new() returns a bad instance is rejected on
        -- reconfigure, and nothing is committed: the manager keeps its old
        -- backend, instance and fiber.
        local inst1 = m.backend_instance
        t.assert_error_msg_contains(
            'backend instance.create_point should be a function', function()
                m:cfg({backend = {new = function() return {} end},
                       backend_cfg = {target = 'b'}})
            end)
        t.assert_is(m.backend, backend)
        t.assert_is(m.worker, worker0)
        t.assert_is(m.backend_instance, inst1)

        m:drop()
        t.assert_equals(drops, 2)
        t.assert_equals(next(rp.managers), nil)
    end)
end

-- The manager API is available (and usable) before box.cfg(), unlike the rest
-- of the backup API.
g.test_available_before_box_cfg = function()
    local dir = treegen.prepare_directory({}, {})
    local script = [[
        local t = require('luatest')
        local rp = box.backup.recovery_point
        t.assert_type(rp.manager_create, 'function')
        t.assert_type(rp.managers, 'table')
        t.assert_equals(box.info.status, 'unconfigured')

        local created
        local backend = {new = function()
            return setmetatable({}, {__index = {
                create_point = function(_, id)
                    created = id
                    return {id = id}
                end,
            }})
        end}
        local m = rp.manager_create('pre', {
            backend = backend, backend_cfg = {}, create_interval = 100,
        })
        t.assert_is(rp.managers['pre'], m)
        -- Unconfigured: the id has no instance segment, just <manager>.<uuid>.
        m:create_point()
        t.assert_str_matches(created, 'pre%.[^.]+')
        m:drop()
        t.assert_equals(rp.managers['pre'], nil)

        -- The cfg-requiring backup operations still raise before box.cfg().
        t.assert_error_covers({type = 'ClientError', name = 'UNCONFIGURED'},
                              box.backup.info)
        os.exit(0)
    ]]
    treegen.write_file(dir, 'before_box_cfg.lua', script)
    local res = justrun.tarantool(dir, {}, {'before_box_cfg.lua'},
                                  {nojson = true, stderr = true})
    t.assert_equals(res.exit_code, 0, res.stderr)
end

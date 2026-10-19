local t = require('luatest')
local fio = require('fio')
local yaml = require('yaml')
local treegen = require('luatest.treegen')
local server = require('luatest.server')

local g = t.group()

g.before_each(function(cg)
    local dir = treegen.prepare_directory({}, {})
    local cfg = {
        credentials = {users = {guest = {roles = {'super'}}}},
        iproto = {
            listen = {{uri = 'unix/:./old.sock'},
                      {uri = 'unix/:./admin.sock'}},
            drop_stale_connections = true,
        },
        replication = {election_fencing_mode = 'soft'},
        groups = {g = {replicasets = {r = {instances = {i1 = {}}}}}},
    }
    if require('tarantool').package == 'Tarantool Enterprise' then
        cfg.config = {reload_jitter = 1}
    end
    treegen.write_file(dir, 'config.yaml', yaml.encode(cfg))
    cg.server = server:new({
        alias = 'i1',
        chdir = dir,
        config_file = 'config.yaml',
        -- The test connection must survive listener replacement and cleanup.
        net_box_uri = 'unix/:' .. fio.pathjoin(dir, 'admin.sock'),
    })
    cg.server:start()
end)

g.after_each(function(cg)
    if cg.server ~= nil then
        cg.server:stop()
    end
end)

local function run_case(cg, case)
    cg.server:exec(function(case)
        local t = require('luatest')
        local fiber = require('fiber')
        local yaml = require('yaml')
        local config = require('config')

        if require('tarantool').package ~= 'Tarantool Enterprise' then
            -- CE has no reload_jitter. Change only the delay calculation;
            -- apply(), the reconnect worker, box.cfg() and cleanup stay real.
            local apply = require('internal.config.applier.box_cfg').apply
            local found = false
            for i = 1, 100 do
                local name = debug.getupvalue(apply, i)
                if name == 'reconnect_delay' then
                    debug.setupvalue(apply, i, function() return 1 end)
                    found = true
                    break
                end
            end
            t.assert(found)
        end

        -- Replace an upvalue of apply() or of the functions it uses.
        local function set_upvalue(func, name, value, seen)
            seen = seen or {}
            if seen[func] then
                return false
            end
            seen[func] = true
            for i = 1, 200 do
                local n, v = debug.getupvalue(func, i)
                if n == nil then
                    return false
                elseif n == name then
                    debug.setupvalue(func, i, value)
                    return true
                elseif type(v) == 'function' and
                       set_upvalue(v, name, value, seen) then
                    return true
                end
            end
            return false
        end

        if case == 'superseded_task' then
            -- Keep the reconnect waiting for the full mesh.
            local apply = require('internal.config.applier.box_cfg').apply
            t.assert(set_upvalue(apply, 'is_fullmesh', function()
                return rawget(_G, 'fullmesh_ok') == true
            end))
        end

        local function reload(uri, peers)
            local f = assert(io.open(config._config_file))
            local cfg = yaml.decode(f:read('*a'))
            f:close()
            cfg.iproto.listen[1].uri = uri
            cfg.replication.peers = peers
            f = assert(io.open(config._config_file, 'w'))
            f:write(yaml.encode(cfg))
            f:close()
            config:reload()
        end

        local cn = require('net.box').connect('unix/:./old.sock')
        t.assert(cn:is_connected())
        local peers
        if case == 'reconnect_error' then
            -- Different URIs reach the same UUID: replication rejects this.
            peers = {'unix/:./new.sock', 'guest@unix/:./new.sock'}
        end
        reload('unix/:./new.sock', peers)
        t.assert_equals(box.cfg.election_fencing_mode, 'off')
        fiber.sleep(0)

        local pending
        for id, info in pairs(fiber.info()) do
            if info.name == 'config.box_cfg.replication_reconnect' then
                pending = fiber.find(id)
            end
        end
        t.assert_not_equals(pending, nil)
        t.assert(cn:is_connected())

        if case == 'same_reload' then
            config:reload()
        elseif case == 'failed_reload' then
            local ok, err = pcall(reload, 'unix/:./missing/listener.sock')
            t.assert_not(ok)
            t.assert_str_contains(tostring(err), 'No such file or directory')
        elseif case == 'superseded_task' then
            -- The first reconnect waits for the full mesh, then a
            -- newer URI change replaces it with a new delay.
            fiber.sleep(1.5)
            reload('unix/:./newer.sock')
            rawset(_G, 'fullmesh_ok', true)
            fiber.sleep(0.5)
            -- Only the newer reconnect restores election fencing.
            t.assert_equals(box.cfg.election_fencing_mode, 'off')
        end

        t.helpers.retrying({timeout = 5}, function()
            t.assert_equals(pending:status(), 'dead')
        end)

        if case == 'reconnect_error' then
            local alert = config._aboard:get('box_cfg_replication_reconnect')
            t.assert_not_equals(alert, nil)
            t.assert_str_contains(alert.message,
                                 'duplicate connection to the same replica')
        end

        -- All assertions describe the expected healthy behavior.
        t.assert_equals(box.cfg.election_fencing_mode, 'soft')
        if case == 'control' or case == 'same_reload' then
            t.assert_not(cn:is_connected())
        end
        cn:close()
    end, {case})
end

g.test_control = function(cg)
    run_case(cg, 'control')
end

g.test_same_reload_preserves_cleanup = function(cg)
    run_case(cg, 'same_reload')
end

g.test_failed_reload_preserves_fencing_restoration = function(cg)
    run_case(cg, 'failed_reload')
end

g.test_reconnect_error_restores_fencing = function(cg)
    run_case(cg, 'reconnect_error')
end

g.test_superseded_task_keeps_fencing_disabled = function(cg)
    run_case(cg, 'superseded_task')
end

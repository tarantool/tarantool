local server = require('luatest.server')
local t = require('luatest')

local g = t.group("prepared", {{remote = true}, {remote = false}})

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
    cg.server:exec(function(remote)
        local cn
        local netbox = require('net.box')
        local fiber = require('fiber')
        local execute
        local prepare
        local unprepare
        if remote then
            cn = netbox.connect(box.cfg.listen)
            execute = function(...) return cn:execute(...) end
            prepare = function(...) return cn:prepare(...) end
            unprepare = function(...) return cn:unprepare(...) end
        else
            execute = function(...)
                local res, err = box.execute(...)
                if err ~= nil then
                    error(err)
                end
                return res
            end
            prepare = function(...)
                local res, err = box.prepare(...)
                if err ~= nil then
                    error(err)
                end
                return res
            end
            unprepare = function(...)
                local res, err = box.unprepare(...)
                if err ~= nil then
                    error(err)
                end
                return res
            end
        end
        rawset(_G, 'execute', execute)
        rawset(_G, 'prepare', prepare)
        rawset(_G, 'unprepare', unprepare)
        rawset(_G, 'cn', cn)
        rawset(_G, 'netbox', netbox)
        rawset(_G, 'fiber', fiber)
        _G.execute([[SET SESSION "sql_seq_scan" = true;]])
    end, {cg.params.remote})
end)

g.after_all(function(cg)
    cg.server:drop()
end)

--
-- gh-12537: make sure there is no collisions for prepared statement IDs.
--
g.test_12537_stmt_id_collision = function(cg)
    cg.server:exec(function()
        local HASH1 = 3135652232
        local HASH2 = 3135652233

        -- Make sure that both prepared statements would have the same ID
        -- if there were no collisions.
        local res = _G.prepare("SELECT 111 AS a /*xqiV8CaJLT*/")
        t.assert_equals(res.stmt_id, HASH1)
        _G.unprepare(HASH1)

        res = _G.prepare("SELECT 222 AS b /*aJVtqcrWP5*/")
        t.assert_equals(res.stmt_id, HASH1)
        _G.unprepare(HASH1)

        -- Make sure the IDs are different in case of a collision.
        res = _G.prepare("SELECT 111 AS a /*xqiV8CaJLT*/")
        t.assert_equals(res.stmt_id, HASH1)

        res = _G.prepare("SELECT 111 AS a /*xqiV8CaJLT*/")
        t.assert_equals(res.stmt_id, HASH1)

        res = _G.prepare("SELECT 222 AS b /*aJVtqcrWP5*/")
        t.assert_equals(res.stmt_id, HASH2)

        res = _G.execute(HASH1)
        t.assert_equals(res.rows, {{111}})

        res = _G.execute(HASH2)
        t.assert_equals(res.rows, {{222}})

        -- It is now possible for the same query to be saved more than once.
        _G.unprepare(HASH1)

        res = _G.prepare("SELECT 222 AS b /*aJVtqcrWP5*/")
        t.assert_equals(res.stmt_id, HASH1)

        res = _G.execute(HASH1)
        t.assert_equals(res.rows, {{222}})

        res = _G.execute(HASH2)
        t.assert_equals(res.rows, {{222}})

        _G.unprepare(HASH1)
        _G.unprepare(HASH2)
    end)
end

g.test_12537_too_much_prepared_statements = function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server:exec(function()
        box.error.injection.set('ERRINJ_SQL_PREPARED_STATEMENT_ID_OVERFLOW',
                                true)
        local exp_err = {
            message = "Failed to execute SQL statement: Number of prepare " ..
                      "statements cannot be more than 4294967296",
            name = "SQL_EXECUTE",
        }
        _G.prepare("SELECT 111 AS a /*xqiV8CaJLT*/")
        t.assert_error_covers(exp_err, _G.prepare,
                              "SELECT 222 AS b /*aJVtqcrWP5*/")
    end)
end

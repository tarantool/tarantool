local server = require('luatest.server')
local t = require('luatest')

local g = t.group("bind1")

g.before_all(function()
    g.server = server:new({alias = 'bind'})
    g.server:start()
end)

g.after_all(function()
    g.server:stop()
end)

g.test_bind_1 = function()
    g.server:exec(function()
        local sql = [[SELECT @1asd;]]
        local res = "At line 1 at or near position 9: unrecognized token '1asd'"
        local _, err = box.execute(sql, {{['@1asd'] = 123}})
        t.assert_equals(err.message, res)
    end)
end

g.test_bind_2 = function()
    local conn = g.server.net_box
    local sql = [[SELECT @1asd;]]
    local res = [[At line 1 at or near position 9: unrecognized token '1asd']]
    local _, err = pcall(conn.execute, conn, sql, {{['@1asd'] = 123}})
    t.assert_equals(err.message, res)
end

g = t.group("bind2", {{remote = true}, {remote = false}})

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
    cg.server:exec(function(remote)
        box.execute([[SET SESSION "sql_seq_scan" = true;]])
        local cn
        local netbox = require('net.box')
        local execute
        if remote then
            box.schema.user.create('test_user', {password = 'test'})
            box.schema.user.grant('test_user',
                                  'read, write, execute', 'universe')
            box.schema.user.grant('test_user', 'create', 'space')
            cn = netbox.connect(box.cfg.listen,
                                {user = 'test_user', password = 'test'})
            execute = function(...) return cn:execute(...) end
        else
            execute = function(...)
                local res, err = box.execute(...)
                if err ~= nil then
                    error(err)
                end
                return res
            end
        end
        rawset(_G, 'execute', execute)
        rawset(_G, 'cn', cn)
    end, {cg.params.remote})
end)

g.after_all(function(cg)
    cg.server:exec(function(remote)
        if remote then
            _G.cn:close()
            box.schema.user.drop('test_user')
        end;
    end, {cg.params.remote})
    cg.server:drop()
end)

-- gh-3401: box.execute parameter binding.
g.test_3401_box_execute_parameter_binding = function(cg)
    cg.server:exec(function()
        box.execute('CREATE TABLE test (id INT PRIMARY KEY, a NUMBER, b TEXT);')
        box.space.test:replace{1, 2, '3'}
        box.space.test:replace{7, 8.5, '9'}
        box.space.test:replace{10, 11, box.NULL}

        local parameters = {}
        parameters[1] = {}
        parameters[1][':value'] = 1
        local sql = 'SELECT * FROM test WHERE id = :value;'
        local res = _G.execute(sql, parameters)
        t.assert_equals(res.rows, {{1, 2, '3'}})

        res = _G.execute('SELECT ?, ?, ?;', {1, 2, 3})
        t.assert_equals(res.rows, {{1, 2, 3}})

        parameters = {}
        parameters[1] = 10
        parameters[2] = {}
        parameters[2]['@value2'] = 12
        parameters[3] = {}
        parameters[3][':value1'] = 11
        res = _G.execute('SELECT ?, :value1, @value2', parameters)
        t.assert_equals(res.rows, {{10, 11, 12}})

        parameters = {}
        parameters[1] = {}
        parameters[1][':value3'] = 1
        parameters[2] = 2
        parameters[3] = {}
        parameters[3][':value1'] = 3
        parameters[4] = 4
        parameters[5] = 5
        parameters[6] = {}
        parameters[6]['@value2'] = 6
        sql = 'SELECT :value3, ?, :value1, ?, ?, @value2, ?, :value3;'
        res = _G.execute(sql, parameters)
        t.assert_equals(res.rows, {{1, 2, 3, 4, 5, 6, nil, 1}})

        -- Try not-integer types.
        local msgpack = require('msgpack')
        sql = 'SELECT ?, ?, ?, ?, ?;'
        res = _G.execute(sql, {'abc', -123.456, msgpack.NULL, true, false})
        t.assert_equals(res.rows, {{'abc', -123.456, nil, true, false}})

        -- Try to replace '?' in meta with something meaningful.
        res = _G.execute('SELECT ? AS kek, ? AS kek2;', {1, 2})
        t.assert_equals(res.rows, {{1, 2}})

        -- Try to bind not existing name.
        parameters = {}
        parameters[1] = {}
        parameters[1]['name'] = 300

        local exp_err = {
            message = "Parameter 'name' was not found in the statement",
            name = "SQL_BIND_NOT_FOUND",
            parameter = "'name'",
        }
        sql = 'SELECT ? AS kek'
        t.assert_error_covers(exp_err, _G.execute, sql, parameters)

        -- Try too many parameters in a statement.
        exp_err = {
            message = "SQL bind parameter limit reached: 65000",
            name = "SQL_BIND_PARAMETER_MAX",
        }
        sql = 'SELECT '..
              string.rep('?, ', box.schema.SQL_BIND_PARAMETER_MAX)..'?;'
        t.assert_error_covers(exp_err, _G.execute, sql)

        -- Try too many parameter values.
        sql = 'SELECT ?;'
        parameters = {}
        for i = 1, box.schema.SQL_BIND_PARAMETER_MAX + 1 do
            parameters[i] = i
        end

        exp_err = {
            message = "SQL bind parameter limit reached: 65001",
            name = "SQL_BIND_PARAMETER_MAX",
        }
        t.assert_error_covers(exp_err, _G.execute, sql, parameters)

        --
        -- Errors during parameters binding.
        --
        -- Try value > INT64_MAX. sql can't bind it, since it has no
        -- suitable method in its bind API.
        res = _G.execute('SELECT ? AS big_uint;', {0xefffffffffffffff})
        t.assert_equals(res.rows, {{17293822569102704640}})
        -- Bind incorrect parameters.
        parameters = {}
        parameters[1] = {}
        parameters[1][100] = 200
        local ok = pcall(_G.execute, 'SELECT ?', parameters)
        t.assert_equals(ok, false)
        parameters = {}
        parameters[1] = {}
        parameters[1][':value'] = {kek = 300}
        res = _G.execute('SELECT :value;', parameters)
        t.assert_equals(res.rows, {{{kek = 300}}})

        box.execute('DROP TABLE test;')

        exp_err = "Failed to execute SQL statement: "..
                  "The number of parameters is too large"
        local _, err = box.execute('SELECT ?;', {1, 2})
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute('SELECT $2;', {1, 2, 3})
        t.assert_equals(tostring(err), exp_err)
    end)
end

-- gh-3810: bind values of integer in range up to 2^64 - 1.
g.test_3810_bind_values_of_integer_in_range_up = function(cg)
    cg.server:exec(function()
        local res = _G.execute('SELECT ? ', {18446744073709551615ULL})
        t.assert_equals(res.rows,  {{18446744073709551615ULL}})
    end)
end

-- gh-4206: make sure that VARBINARY values can be bound.
g.test_4206_bind_varbinary = function(cg)
    cg.server:exec(function()
        local varbinary = require('varbinary')
        local res = _G.execute("SELECT TYPEOF(?);", {varbinary.new('\x12\34')})
        t.assert_equals(res.rows, {{"varbinary"}})
    end)
end

-- gh-4566: bind variable to LIKE argument resulted to crash.
g.test_4566_bind_variable_LIKE_argument_resulted_to_crash = function(cg)
    cg.server:exec(function()
        box.execute("CREATE TABLE t (id INT PRIMARY KEY, a TEXT);")
        local res = box.execute("SELECT * FROM t WHERE a LIKE ?;", {'a%'})
        t.assert_equals(res.rows, {})

        box.execute("INSERT INTO t VALUES (1, 'aA'), (2, 'Ba'), (3, 'A');")
        res = box.execute("SELECT * FROM t WHERE a LIKE ?;", {'a%'})
        t.assert_equals(res.rows, {{1, 'aA'}})

        box.space.t:drop()
    end)
end

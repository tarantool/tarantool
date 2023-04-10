-- https://github.com/tarantool/tarantool/issues/6436 Constraints
local server = require('luatest.server')
local netbox = require('net.box')
local t = require('luatest')

local g = t.group('gh-6436-field-constraint-test', {{engine = 'memtx'}, {engine = 'vinyl'}})

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:stop()
    cg.server = nil
end)

g.before_each(function(cg)
    cg.cleanup = {}
end)

g.after_each(function(cg)
    while #cg.cleanup > 0 do
        local cleanup = cg.cleanup[#cg.cleanup]
        cg.cleanup[#cg.cleanup] = nil
        cleanup()
    end
end)

g.test_field_constraint_basics = function(cg)
    local engine = cg.params.engine

    table.insert(cg.cleanup, function()
        cg.server:exec(function()
            if box.space.test then box.space.test:drop() end
            if box.func.field_constr1 then box.func.field_constr1:drop() end
            if box.func.field_constr2 then box.func.field_constr2:drop() end
            if box.func.field_constr3 then box.func.field_constr3:drop() end
        end)
    end)

    cg.server:exec(function(engine)
        local constr_field_body1 = "function(field, name) " ..
            "if name ~= 'field_constr1' then error('wrong name!') end " ..
            "return field < 100 end"
        local constr_field_body2 = "function(field, name) " ..
            "if name ~= 'field_constr2' then error('wrong name!') end " ..
            "return field < 200 end"
        local constr_field_body3 = "function(field, name) " ..
            "if name ~= 'my_constr' then error('wrong name!') end " ..
            "if field >= 300 then error('300!') end return true end"

        local function func_opts(body)
            return {language = 'LUA', is_deterministic = true, body = body}
        end
        box.schema.func.create('field_constr1', func_opts(constr_field_body1))
        box.schema.func.create('field_constr2', func_opts(constr_field_body2))
        box.schema.func.create('field_constr3', func_opts(constr_field_body3))

        local fmt = { {"id1", constraint = 'field_constr1'},
                      {"id2", constraint = {'field_constr2'}},
                      {"id3", constraint = {my_constr = 'field_constr3'}} }

        local s = box.schema.create_space('test', {engine=engine, format=fmt})
        s:create_index('pk')
        box.schema.user.grant('guest', 'read,write', 'space', 'test')
        box.schema.user.grant('guest', 'execute', 'function', 'field_constr1')
        box.schema.user.grant('guest', 'execute', 'function', 'field_constr2')
        box.schema.user.grant('guest', 'execute', 'function', 'field_constr3')
    end, {engine})

    -- check accessing from lua
    local function test_lua(cg, field4)
        cg.server:exec(function(field4)
            local s = box.space.test
            t.assert_equals(s:replace{1, 2, 3, field4}, {1, 2, 3, field4});
            t.assert_error_msg_content_equals(
                "Check constraint 'field_constr1' failed for field '1 (id1)'",
                function() box.space.test:replace{100, 2, 3, field4} end)
            t.assert_error_msg_content_equals(
                "Check constraint 'field_constr2' failed for field '2 (id2)'",
                function() box.space.test:replace{1, 200, 3, field4} end)
            t.assert_error_msg_content_equals(
                "Check constraint 'my_constr' failed for field '3 (id3)'",
                function() box.space.test:replace{1, 2, 300, field4} end)
            t.assert_equals(s:select{}, {{1, 2, 3, field4}})
        end, {field4})
    end

    -- check accessing from net.box
    local function test_net(cg, field4)
        local c = netbox.connect(cg.server.net_box_uri)
        local s = c.space.test
        t.assert_equals(s:replace{1, 2, 3, field4}, {1, 2, 3, field4})
        t.assert_error_msg_content_equals(
            "Check constraint 'field_constr1' failed for field '1 (id1)'",
            function() s:replace{100, 2, 3, field4} end)
        t.assert_error_msg_content_equals(
            "Check constraint 'field_constr2' failed for field '2 (id2)'",
            function() s:replace{1, 200, 3, field4} end)
        t.assert_error_msg_content_equals(
            "Check constraint 'my_constr' failed for field '3 (id3)'",
            function() s:replace{1, 2, 300, field4} end)
        t.assert_equals(s:select{}, {{1, 2, 3, field4}})
     end

    --- Test right after constraint creation.
    test_lua(cg)
    test_net(cg)

    --- Test after recovery from xlog.
    cg.server:restart()
    test_lua(cg)
    test_net(cg)

    --- Test after recovery from snap.
    cg.server:eval('box.snapshot()')
    cg.server:restart()
    test_lua(cg)
    test_net(cg)

    -- Check non-plain format.
    cg.server:exec(function()
        t.assert_error_msg_content_equals(
            "Tuple field [4][\"field\"] required by space format is missing",
            function()
                local parts = {{'[4].field', 'unsigned'}}
                box.space.test:create_index('sk', {unique=false, parts=parts})
            end)

        box.space.test:delete{1}
        local parts = {{'[4].field', 'unsigned'}}
        box.space.test:create_index('sk', {unique=false, parts=parts})

        t.assert_error_msg_content_equals(
            "Tuple field [4][\"field\"] required by space format is missing",
            function() box.space.test:replace{1, 2, 3} end)
    end)

    --- Test right now.
    test_lua(cg, {field=0})
    test_net(cg, {field=0})

    if engine ~= 'vinyl' then -- Disabled until #6778 is fixed.
        --- Test after recovery from xlog.
        cg.server:restart()
        test_lua(cg, {field=0})
        test_net(cg, {field=0})
    end

    --- Test after recovery from snap.
    cg.server:eval('box.snapshot()')
    cg.server:restart()
    test_lua(cg, {field=0})
    test_net(cg, {field=0})
end

g.test_wrong_field_constraint = function(cg)
    local engine = cg.params.engine

    table.insert(cg.cleanup, function()
        cg.server:exec(function()
            if box.space.test then box.space.test:drop() end
            if box.func.field_constr1 then box.func.field_constr1:drop() end
            if box.func.field_constr2 then box.func.field_constr2:drop() end
            if box.func.field_constr3 then box.func.field_constr3:drop() end
        end)
    end)

    cg.server:exec(function(engine)
        local constr_field_body1 = "function(field) return field < 100 end"
        local constr_field_body2 = "function(field) " ..
            "if field >= 300 then error('300!') end return true end"
        local function field_constr3() end
        field_constr3()

        box.schema.func.create('field_constr1',
            {language = 'LUA', is_deterministic = true, body = constr_field_body1})
        box.schema.func.create('field_constr2',
            {language = 'LUA', body = constr_field_body2})
        box.schema.func.create('field_constr3',
            {language = 'LUA', is_deterministic = true})

        local fmt = {{"id1", constraint = 'field_constr1'}, {"id2"}}

        local s = box.schema.create_space('test', {engine=engine, format=fmt})
        s:create_index('pk')
    end, {engine})

    cg.server:exec(function()
        t.assert_error_msg_content_equals(
            "Illegal parameters, format[1]: " ..
            "constraint function was not found by name 'field_constr4'",
            function()
                box.space.test:format({{"id1", constraint = "field_constr4"}, {"id2"}})
            end)

        t.assert_error_msg_content_equals(
            "Illegal parameters, format[1]: " ..
            "constraint function was not found by name 'field_constr4'",
            function()
                box.space.test:format({{"id1", constraint = "field_constr4"}, {"id2"}})
            end)

        t.assert_error_msg_content_equals(
            "Illegal parameters, format[1]: " ..
            "constraint function was not found by name 'field_constr4'",
            function()
                box.space.test:format({{"id1", constraint = {"field_constr4"}}, {"id2"}})
            end)

        t.assert_error_msg_content_equals(
            "Illegal parameters, format[1]: " ..
            "constraint function was not found by name 'field_constr4'",
            function()
                box.space.test:format({{"id1", constraint = {field_constr1="field_constr4"}}, {"id2"}})
            end)

        t.assert_error_msg_content_equals(
            "Illegal parameters, format[1]: " ..
            "constraint must be string or table",
            function()
                box.space.test:format({{"id1", constraint = 666}, {"id2"}})
            end)

        t.assert_error_msg_content_equals(
            "Illegal parameters, format[1]: " ..
            "constraint function is expected to be a string, but got number",
            function()
                box.space.test:format({{"id1", constraint = {666}}, {"id2"}})
            end)

        t.assert_error_msg_content_equals(
            "Illegal parameters, format[1]: " ..
            "constraint function is expected to be a string, but got number",
            function()
                box.space.test:format({{"id1", constraint = {name=666}}, {"id2"}})
            end)

        t.assert_error_msg_content_equals(
            "Failed to create constraint 'field_constr2' in space 'test': " ..
            "constraint function 'field_constr2' must be deterministic",
            function()
                box.space.test:format({{"id1", constraint = "field_constr2"}, {"id2"}})
            end)

        t.assert_error_msg_content_equals(
            "Failed to create constraint 'field_constr3' in space 'test': " ..
            "constraint lua function 'field_constr3' must have persistent body",
            function()
                box.space.test:format({{"id1", constraint = "field_constr3"}, {"id2"}})
            end)

        t.assert_error_msg_content_equals(
            "Failed to create constraint 'field_constr2' in space 'test': " ..
            "constraint function 'field_constr2' must be deterministic",
            function()
                box.space.test:format({{"id1", constraint = {"field_constr2"}}, {"id2"}})
            end)

        t.assert_error_msg_content_equals(
            "Failed to create constraint 'field_constr3' in space 'test': " ..
            "constraint lua function 'field_constr3' must have persistent body",
            function()
                box.space.test:format({{"id1", constraint = {"field_constr3"}}, {"id2"}})
            end)

        t.assert_error_msg_content_equals(
            "Wrong space format: constraint name is too long",
            function()
                box.space.test:format({{"id1", constraint = {[string.rep('a', 66666)] = "field_constr1"}},
                                       {"id2"}})
            end)

        t.assert_error_msg_content_equals(
            "Invalid identifier '' (expected printable symbols only or it is too long)",
            function()
                box.space.test:format({{"id1", constraint = {[''] = "field_constr1"}}, {"id2"}})
            end)

        local func_id = box.func.field_constr1.id
        t.assert_equals(box.space.test:format(),
                        { {constraint = {field_constr1 = func_id},
                           name = "id1", type = "any"},
                          {name = "id2", type = "any"} })
    end)
end

g.test_field_constraint_upsert_update = function(cg)
    local engine = cg.params.engine

    table.insert(cg.cleanup, function()
        cg.server:exec(function()
            if box.space.test then box.space.test:drop() end
            if box.func.field_constr then box.func.field_constr:drop() end
        end)
    end)

    cg.server:exec(function(engine)
        local constr_field_body = "function(field) return field < 100 end"

        box.schema.func.create('field_constr',
            {language = 'LUA', is_deterministic = true, body = constr_field_body})

        local fmt = {{"id"}, {"id2", constraint='field_constr'}}

        local s = box.schema.create_space('test', {engine=engine, format=fmt})
        s:create_index('pk')

        s:replace{1, 50}
    end, {engine})

    local function check(engine)
        local s = box.space.test

        t.assert_error_msg_content_equals(
            "Check constraint 'field_constr' failed for field '2 (id2)'",
            function() s:update({1}, {{'+', 2, 50}}) end
        )

        if engine == 'memtx' then
            t.assert_error_msg_content_equals(
                "Check constraint 'field_constr' failed for field '2 (id2)'",
                function() s:upsert({1, 50}, {{'+', 2, 50}}) end
            )
        else
            s:upsert({1, 50}, {{'+', 2, 50}})
        end

        t.assert_equals(s:select{}, {{1, 50}})
    end

    cg.server:exec(check, {engine})

    cg.server:restart()

    cg.server:exec(check, {engine})

    cg.server:eval('box.snapshot()')
    cg.server:restart()

    cg.server:exec(check, {engine})
end

g.test_field_constraint_nullable = function(cg)
    local engine = cg.params.engine

    table.insert(cg.cleanup, function()
        cg.server:exec(function()
            if box.space.test then box.space.test:drop() end
            if box.func.field_constr then box.func.field_constr:drop() end
        end)
    end)

    cg.server:exec(function(engine)
        local constr_field_body = "function(field) return field < 100 end"

        box.schema.func.create('field_constr',
            {language = 'LUA', is_deterministic = true, body = constr_field_body})

        local fmt = {{"id"},
                     {"id2", constraint = 'field_constr', is_nullable = true}}

        local s = box.schema.create_space('test', {engine=engine, format=fmt})
        s:create_index('pk')

        s:replace{1}

        t.assert_error_msg_content_equals(
            "Check constraint 'field_constr' failed for field '2 (id2)'",
            function() s:update({1}, {{'=', 2, 100}}) end
        )

        s:replace{1, box.NULL}

        t.assert_error_msg_content_equals(
            "Check constraint 'field_constr' failed for field '2 (id2)'",
            function() s:update({1}, {{'=', 2, 100}}) end
        )

    end, {engine})
end

g.test_several_field_constraints = function(cg)
    local engine = cg.params.engine

    table.insert(cg.cleanup, function()
        cg.server:exec(function()
            if box.space.test then box.space.test:drop() end
            if box.func.ge_zero then box.func.ge_zero:drop() end
            if box.func.lt_100 then box.func.lt_100:drop() end
        end)
    end)

    cg.server:exec(function(engine)
        local constr_field_body1 =
            "function(field) if field < 0 then error('<0') end return true end"
        local constr_field_body2 = "function(field) return field < 100 end"

        local function func_opts(body)
            return {language = 'LUA', is_deterministic = true, body = body}
        end
        box.schema.func.create('ge_zero', func_opts(constr_field_body1))
        box.schema.func.create('lt_100', func_opts(constr_field_body2))
        local fmt = { {"id1"},
                      {"id2", constraint={'ge_zero', 'lt_100'}} }
        local s = box.schema.create_space('test', {engine=engine, format=fmt})
        s:create_index('pk')

    end, {engine})

    cg.server:exec(function()
        local s = box.space.test

        t.assert_equals(s:replace{1, 0}, {1, 0})
        t.assert_error_msg_content_equals(
            "Check constraint 'ge_zero' failed for field '2 (id2)'",
            s.replace, s, {2, -1}
        )
        t.assert_error_msg_content_equals(
            "Check constraint 'lt_100' failed for field '2 (id2)'",
            s.replace, s, {3, 100}
        )
    end)

    cg.server:restart()

    cg.server:exec(function()
        local s = box.space.test

        t.assert_equals(s:replace{1, 0}, {1, 0})
        t.assert_error_msg_content_equals(
            "Check constraint 'ge_zero' failed for field '2 (id2)'",
            s.replace, s, {2, -1}
        )
        t.assert_error_msg_content_equals(
            "Check constraint 'lt_100' failed for field '2 (id2)'",
            s.replace, s, {3, 100}
        )
    end)

end

g.test_field_constraint_integrity = function(cg)
    local engine = cg.params.engine

    table.insert(cg.cleanup, function()
        cg.server:exec(function()
            if box.space.test then box.space.test:drop() end
            if box.func.field_constr1 then box.func.field_constr1:drop() end
            if box.func.field_constr2 then box.func.field_constr2:drop() end
            if box.func.field_constr3 then box.func.field_constr3:drop() end
        end)
    end)

    cg.server:exec(function(engine)
        local constr_field_body1 = "function(field) return field < 100 end"
        local constr_field_body2 = "function(field) return field < 200 end"
        local constr_field_body3 = "function(field) " ..
            "if field >= 300 then error('300!') end return true end"

        local function func_opts(body)
            return {language = 'LUA', is_deterministic = true, body = body}
        end
        box.schema.func.create('field_constr1', func_opts(constr_field_body1))
        box.schema.func.create('field_constr2', func_opts(constr_field_body2))
        box.schema.func.create('field_constr3', func_opts(constr_field_body3))

        local s = box.schema.create_space('test', {engine=engine})
        s:create_index('pk')
        box.schema.user.grant('guest', 'read,write', 'space', 'test')
    end, {engine})

    cg.server:exec(function()
        local s = box.space.test

        t.assert_equals(s:format(), {})
        t.assert_error_msg_content_equals(
            "Illegal parameters, format[1]: constraint function was not found by name 'unknown_constr'",
            function() s:format{{"id1", constraint='unknown_constr'}} end
        )
        t.assert_equals(s:format(), {})
        s:format{{"id1", constraint='field_constr1'}}
        t.assert_equals(s:replace{1, 2, 3}, {1, 2, 3})
        t.assert_error_msg_content_equals(
            "Check constraint 'field_constr1' failed for field '1 (id1)'",
            function() s:replace{100, 2, 3} end
        )
    end, {engine})

    local function check_references()
        cg.server:exec(function()
            t.assert_error_msg_contains(
                "Can't drop function",
                function() box.func.field_constr1:drop() end
            )
            t.assert_error_msg_contains(
                "function is referenced by constraint",
                function() box.func.field_constr1:drop() end
            )
        end)
    end

    check_references()

    cg.server:restart()
    check_references()

    cg.server:eval('box.snapshot()')
    cg.server:restart()
    check_references()

    cg.server:exec(function()
        local s = box.space.test

        s:format{{"id1"}, {"id2", constraint='field_constr2'}}
        box.func.field_constr1:drop()

        t.assert_equals(s:replace{100, 2, 3}, {100, 2, 3})
        t.assert_error_msg_content_equals(
            "Check constraint 'field_constr2' failed for field '2 (id2)'",
            function() s:replace{1, 200, 3} end
        )
        t.assert_equals(s:replace{1, 2, 300}, {1, 2, 300})

        t.assert_error_msg_content_equals(
            "Check constraint 'field_constr3' failed for field '3 (id3)'",
            function() s:format{{"id1"},
                                {"id2", constraint='field_constr2'},
                                {"id3", constraint='field_constr3'}} end
        )
        t.assert_error_msg_content_equals(
            "Check constraint 'field_constr2' failed for field '2 (id2)'",
            function() s:replace{1, 200, 3} end
        )

        t.assert_equals(s:select{}, {{1, 2, 300}, {100, 2, 3}})
        t.assert_equals(s:format(),
            { {name = "id1", type = "any"},
              {constraint = {field_constr2 = box.func.field_constr2.id},
               name = "id2", type = "any"}
            })

    end, {engine})
end

-- Check that constraints are replicated and initialized properly.
g.test_constraint_replication = function(cg)
    local engine = cg.params.engine

    table.insert(cg.cleanup, function()
        cg.server:exec(function()
            if box.space.test then box.space.test:drop() end
            if box.func.field_constr then box.func.field_constr:drop() end
        end)
    end)

    cg.server:exec(function(engine)
        local constr_field_body = "function(field) return field < 100 end"
        local function func_opts(body)
            return {language = 'LUA', is_deterministic = true, body = body}
        end
        box.schema.func.create('field_constr', func_opts(constr_field_body))

        local s = box.schema.create_space('test', {engine=engine})
        s:format{{'id1', constraint='field_constr'},{'id2'},{'id3'}}
        s:create_index('pk')
        box.snapshot()

        t.assert_error_msg_content_equals(
            "Check constraint 'field_constr' failed for field '1 (id1)'",
            function() s:replace{100, 2, 3} end
        )
    end, {engine})

    local replica_cfg = {
        replication = cg.server.net_box_uri,
    }
    local replica = server:new({alias = 'replica', box_cfg = replica_cfg})
    replica:start()
    table.insert(cg.cleanup, function()
        replica:stop()
    end)

    replica:exec(function()
        local s = box.space.test
        t.assert_error_msg_content_equals(
            "Check constraint 'field_constr' failed for field '1 (id1)'",
            function() s:replace{100, 2, 3} end
        )
    end)

end

g.test_field_sql_netbox_access = function(cg)
    local engine = cg.params.engine

    table.insert(cg.cleanup, function()
        cg.server:exec(function()
            if box.space.test then box.space.test:drop() end
            if box.func.field_constr1 then box.func.field_constr1:drop() end
        end)
    end)

    cg.server:exec(function(engine)
        local constr_field_body = "function(field) return field < 100 end"

        local function func_opts(body)
            return {language = 'LUA', is_deterministic = true, body = body}
        end
        box.schema.func.create('field_constr', func_opts(constr_field_body))

        local s = box.schema.create_space('test', {engine=engine})
        s:format{{'id1', constraint='field_constr'},{'id2'},{'id3'}}
        s:create_index('pk')
        box.schema.user.grant('guest', 'read,write', 'space', 'test')
        box.schema.user.grant('guest', 'execute', 'function', 'field_constr')
    end, {engine})

    local function check_access()
        cg.server:exec(function()
            local s = box.space.test

            t.assert_equals(s:replace{1, 2, 3}, {1, 2, 3})
            t.assert_error_msg_content_equals(
                "Check constraint 'field_constr' failed for field '1 (id1)'",
                function() s:replace{100, 2, 3} end
            )
            t.assert_equals(s:get{1}.id1, 1)
            t.assert_equals(s:get{1}.id2, 2)
            t.assert_equals(s:get{1}.id3, 3)

            t.assert_equals(box.execute[[SELECT * FROM "test"]],
            { metadata = { {name = "id1", type = "any"},
                           {name = "id2", type = "any"},
                           {name = "id3", type = "any"} },
              rows = {{1, 2, 3}}
            })
        end, {engine})

        local c = netbox.connect(cg.server.net_box_uri)
        local s = c.space.test
        t.assert_equals(s:select{}, {{1, 2, 3}})
        t.assert_equals(s:get{1}.id1, 1)
        t.assert_equals(s:get{1}.id2, 2)
        t.assert_equals(s:get{1}.id3, 3)

        t.assert_equals(c:execute[[SELECT * FROM "test"]],
        { metadata = { {name = "id1", type = "any"},
                       {name = "id2", type = "any"},
                       {name = "id3", type = "any"} },
          rows = {{1, 2, 3}}
        })
    end

    check_access()

    cg.server:restart()
    check_access()

    cg.server:eval('box.snapshot()')
    cg.server:restart()
    check_access()
end

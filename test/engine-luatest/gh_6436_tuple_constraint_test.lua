-- https://github.com/tarantool/tarantool/issues/6436 Constraints
local server = require('luatest.server')
local netbox = require('net.box')
local t = require('luatest')

local g = t.group('gh-6436-tuple-constraint-test', {{engine = 'memtx'}, {engine = 'vinyl'}})

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

g.test_tuple_constraint_basics = function(cg)
    local engine = cg.params.engine

    table.insert(cg.cleanup, function()
        cg.server:exec(function()
            if box.space.test then box.space.test:drop() end
            if box.func.tuple_constr1 then box.func.tuple_constr1:drop() end
        end)
    end)

    cg.server:exec(function(engine)
        local constr_tuple_body1 = "function(tuple, name) " ..
            "if name ~= 'tuple_constr1' then error('wrong name!') end " ..
            "if tuple[1] ~= tuple.id1 then error('wrong format!') end " ..
            "if tuple[2] ~= tuple.id2 then error('wrong format!') end " ..
            "return tuple[1] + tuple.id2 < 100 end"

        local function func_opts(body)
            return {language = 'LUA', is_deterministic = true, body = body}
        end
        box.schema.func.create('tuple_constr1', func_opts(constr_tuple_body1))

        local fmt = {'id1', 'id2', 'id3'}
        local s = box.schema.create_space('test', {engine=engine,
                                                   format=fmt,
                                                   constraint='tuple_constr1'})
        s:create_index('pk')
        box.schema.user.grant('guest', 'read,write', 'space', 'test')
    end, {engine})

    -- check accessing from lua
    local function test_lua(cg, field4)
        cg.server:exec(function(field4)
            local s = box.space.test

            t.assert_equals(s:replace{1, 2, 3, field4}, {1, 2, 3, field4})
            t.assert_error_msg_content_equals(
                "Check constraint 'tuple_constr1' failed for a tuple",
                function() s:replace{100, 2, 3, field4} end
            )
            t.assert_error_msg_content_equals(
                "Check constraint 'tuple_constr1' failed for a tuple",
                function() s:replace{1, 200, 3, field4} end
            )
            t.assert_equals(s:select{}, {{1, 2, 3, field4}})
        end, {field4})
    end

    -- check accessing from net.box
    local function test_net(cg, field4)
        local c = netbox.connect(cg.server.net_box_uri)
        local s = c.space.test
        t.assert_equals(s:replace{1, 2, 3, field4}, {1, 2, 3, field4})
        t.assert_error_msg_content_equals(
            "Check constraint 'tuple_constr1' failed for a tuple",
            function() s:replace{100, 2, 3, field4} end)
        t.assert_error_msg_content_equals(
            "Check constraint 'tuple_constr1' failed for a tuple",
            function() s:replace{1, 200, 3, field4} end)
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
    end)

    --- Test right now.
    test_lua(cg, {field=0})
    test_net(cg, {field=0})

    --[[ Disabled until #6778 is fixed.
    --- Test after recovery from xlog.
    cg.server:restart()
    test_lua(cg, {field=0})
    test_net(cg, {field=0})
    --]]

    --- Test after recovery from snap.
    cg.server:eval('box.snapshot()')
    cg.server:restart()
    test_lua(cg, {field=0})
    test_net(cg, {field=0})
end

g.test_wrong_tuple_constraint = function(cg)
    local engine = cg.params.engine

    table.insert(cg.cleanup, function()
        cg.server:exec(function()
            if box.space.test then box.space.test:drop() end
            if box.func.tuple_constr1 then box.func.tuple_constr1:drop() end
            if box.func.tuple_constr2 then box.func.tuple_constr2:drop() end
            if box.func.tuple_constr3 then box.func.tuple_constr3:drop() end
        end)
    end)

    cg.server:exec(function(engine)
        local constr_tuple_body1 = "function(x) return true end"
        local constr_tuple_body2 = "function(x) return false end"
        local function tuple_constr3() end
        tuple_constr3()

        box.schema.func.create('tuple_constr1',
            {language = 'LUA', is_deterministic = true, body = constr_tuple_body1})
        box.schema.func.create('tuple_constr2',
            {language = 'LUA', body = constr_tuple_body2})
        box.schema.func.create('tuple_constr3',
            {language = 'LUA', is_deterministic = true})

        t.assert_error_msg_content_equals(
            "Illegal parameters, constraint function was not found by name 'tuple_constr4'",
            box.schema.create_space, 'test',
            {engine = engine, constraint = "tuple_constr4"})

        t.assert_error_msg_content_equals(
            "Illegal parameters, constraint function was not found by name 'tuple_constr4'",
            box.schema.create_space, 'test',
            {engine = engine, constraint = {"tuple_constr4"}})

        t.assert_error_msg_content_equals(
            "Illegal parameters, constraint function was not found by name 'tuple_constr4'",
            box.schema.create_space, 'test',
            {engine = engine, constraint = {check = "tuple_constr4"}})

        t.assert_error_msg_content_equals(
            "Illegal parameters, options parameter 'constraint' should be one of types: string, table",
            box.schema.create_space, 'test',
            {engine = engine, constraint = 666})

        t.assert_error_msg_content_equals(
            "Illegal parameters, constraint function is expected to be a string, but got number",
            box.schema.create_space, 'test',
            {engine = engine, constraint = {666}})

        t.assert_error_msg_content_equals(
            "Illegal parameters, constraint function is expected to be a string, but got number",
            box.schema.create_space, 'test',
            {engine = engine, constraint = {name=666}})

        t.assert_error_msg_content_equals(
            "Failed to create constraint 'tuple_constr2' in space 'test': " ..
            "constraint function 'tuple_constr2' must be deterministic",
            box.schema.create_space, 'test',
            {engine = engine, constraint = "tuple_constr2"})

        t.assert_error_msg_content_equals(
            "Failed to create constraint 'tuple_constr3' in space 'test': " ..
            "constraint lua function 'tuple_constr3' must have persistent body",
            box.schema.create_space, 'test',
            {engine = engine, constraint = "tuple_constr3"})

        t.assert_error_msg_content_equals(
            "Failed to create constraint 'check' in space 'test': " ..
            "constraint function 'tuple_constr2' must be deterministic",
            box.schema.create_space, 'test',
            {engine = engine, constraint = {check = "tuple_constr2"}})

        t.assert_error_msg_content_equals(
            "Failed to create constraint 'check' in space 'test': " ..
            "constraint lua function 'tuple_constr3' must have persistent body",
            box.schema.create_space, 'test',
            {engine = engine, constraint = {check = "tuple_constr3"}})

        t.assert_error_msg_content_equals(
            "Wrong space options: constraint name is too long",
            box.schema.create_space, 'test',
            {engine = engine, constraint = {[string.rep('a', 66666)] = "tuple_constr1"}})

        t.assert_error_msg_content_equals(
            "Wrong space options: constraint name isn't a valid identifier",
            box.schema.create_space, 'test',
            {engine = engine, constraint = {[''] = "tuple_constr1"}})

        local s = box.schema.create_space('test', {engine = engine,
                                                   constraint = 'tuple_constr1'})
        t.assert_equals(s.constraint, {tuple_constr1 = box.func.tuple_constr1.id})
        s:create_index('pk')
    end, {engine})

    cg.server:exec(function()
        local s = box.space.test

        t.assert_error_msg_content_equals(
            "Illegal parameters, constraint function was not found by name 'tuple_constr4'",
            function() s:alter({constraint = "tuple_constr4"}) end)

        t.assert_error_msg_content_equals(
            "Illegal parameters, constraint function was not found by name 'tuple_constr4'",
            function() s:alter({constraint = {"tuple_constr4"}}) end)

        t.assert_error_msg_content_equals(
            "Illegal parameters, constraint function was not found by name 'tuple_constr4'",
            function() s:alter({constraint = {check = "tuple_constr4"}}) end)

        t.assert_error_msg_content_equals(
            "Illegal parameters, options parameter 'constraint' should be one of types: string, table",
            function() s:alter({constraint = 666}) end)

        t.assert_error_msg_content_equals(
            "Illegal parameters, constraint function is expected to be a string, but got number",
            function() s:alter({constraint = {666}}) end)

        t.assert_error_msg_content_equals(
            "Illegal parameters, constraint function is expected to be a string, but got number",
            function() s:alter({constraint = {name=666}}) end)

        t.assert_error_msg_content_equals(
            "Failed to create constraint 'tuple_constr2' in space 'test': " ..
            "constraint function 'tuple_constr2' must be deterministic",
            function() s:alter({constraint = "tuple_constr2"}) end)

        t.assert_error_msg_content_equals(
            "Failed to create constraint 'tuple_constr3' in space 'test': " ..
            "constraint lua function 'tuple_constr3' must have persistent body",
            function() s:alter({constraint = "tuple_constr3"}) end)

        t.assert_error_msg_content_equals(
            "Failed to create constraint 'check' in space 'test': " ..
            "constraint function 'tuple_constr2' must be deterministic",
            function() s:alter({constraint = {check = "tuple_constr2"}}) end)

        t.assert_error_msg_content_equals(
            "Failed to create constraint 'check' in space 'test': " ..
            "constraint lua function 'tuple_constr3' must have persistent body",
            function() s:alter({constraint = {check = "tuple_constr3"}}) end)

        s:alter({constraint = {check = "tuple_constr1"}})
        t.assert_equals(s.constraint, {check = box.func.tuple_constr1.id})
    end)
end

g.test_several_tuple_constraints = function(cg)
    local engine = cg.params.engine

    table.insert(cg.cleanup, function()
        cg.server:exec(function()
            if box.space.test then box.space.test:drop() end
            if box.func.tuple_constr1 then box.func.tuple_constr1:drop() end
            if box.func.tuple_constr2 then box.func.tuple_constr2:drop() end
        end)
    end)

    cg.server:exec(function(engine)
        local constr_tuple_body1 = "function(tuple, name) " ..
            "if name ~= 'check1' then error('wrong name!') end " ..
            "return tuple[1] + tuple[2] < 100 end"
        local constr_tuple_body2 = "function(tuple, name) " ..
            "if name ~= 'check2' then error('wrong name!') end " ..
            "if tuple[2] + tuple[3] > 100 then error('wtf!') end return true end"

        local function func_opts(body)
            return {language = 'LUA', is_deterministic = true, body = body}
        end
        box.schema.func.create('tuple_constr1', func_opts(constr_tuple_body1))
        box.schema.func.create('tuple_constr2', func_opts(constr_tuple_body2))

        local constr = {check1='tuple_constr1',check2='tuple_constr2'}
        local s = box.schema.create_space('test', {engine=engine,
                                                   constraint=constr})
        s:create_index('pk')
        box.schema.user.grant('guest', 'read,write', 'space', 'test')
    end, {engine})

    -- check accessing from lua
    local function test_lua(cg, field4)
        cg.server:exec(function(field4)
            local s = box.space.test

            t.assert_equals(s:replace{1, 2, 3, field4}, {1, 2, 3, field4})
            t.assert_error_msg_content_equals(
                "Check constraint 'check1' failed for a tuple",
                function() s:replace{100, 2, 3, field4} end
            )
            t.assert_error_msg_content_equals(
                "Check constraint 'check2' failed for a tuple",
                function() s:replace{1, 200, 3, field4} end
            )
            t.assert_error_msg_content_equals(
                "Check constraint 'check2' failed for a tuple",
                function() s:replace{1, 2, 300, field4} end
            )
            t.assert_equals(s:select{}, {{1, 2, 3, field4}})
        end, {field4})
    end

    -- check accessing from net.box
    local function test_net(cg, field4)
        local c = netbox.connect(cg.server.net_box_uri)
        local s = c.space.test
        t.assert_equals(s:replace{1, 2, 3, field4}, {1, 2, 3, field4})
        t.assert_error_msg_content_equals(
            "Check constraint 'check1' failed for a tuple",
            function() s:replace{100, 2, 3, field4} end)
        t.assert_error_msg_content_equals(
            "Check constraint 'check2' failed for a tuple",
            function() s:replace{1, 200, 3, field4} end)
        t.assert_error_msg_content_equals(
            "Check constraint 'check2' failed for a tuple",
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
    end)

    --- Test right now.
    test_lua(cg, {field=0})
    test_net(cg, {field=0})

    --[[ Disabled until #6778 is fixed.
    --- Test after recovery from xlog.
    cg.server:restart()
    test_lua(cg, {field=0})
    test_net(cg, {field=0})
    --]]

    --- Test after recovery from snap.
    cg.server:eval('box.snapshot()')
    cg.server:restart()
    test_lua(cg, {field=0})
    test_net(cg, {field=0})
end

g.test_tuple_constraint_integrity = function(cg)
    local engine = cg.params.engine

    table.insert(cg.cleanup, function()
        cg.server:exec(function()
            if box.space.test then box.space.test:drop() end
            if box.func.tuple_constr1 then box.func.tuple_constr1:drop() end
            if box.func.tuple_constr2 then box.func.tuple_constr2:drop() end
            if box.func.tuple_constr3 then box.func.tuple_constr3:drop() end
        end)
    end)

    cg.server:exec(function(engine)
        local constr_tuple_body1 = "function(tuple) return tuple[1] < 100 end"
        local constr_tuple_body2 = "function(tuple) return tuple[2] < 200 end"
        local constr_tuple_body3 = "function(tuple) " ..
            "if tuple[3] >= 300 then error('300!') end return true end"

        local function func_opts(body)
            return {language = 'LUA', is_deterministic = true, body = body}
        end
        box.schema.func.create('tuple_constr1', func_opts(constr_tuple_body1))
        box.schema.func.create('tuple_constr2', func_opts(constr_tuple_body2))
        box.schema.func.create('tuple_constr3', func_opts(constr_tuple_body3))

        local s = box.schema.create_space('test', {engine=engine})
        s:create_index('pk')
        box.schema.user.grant('guest', 'read,write', 'space', 'test')
    end, {engine})

    cg.server:exec(function()
        local s = box.space.test

        t.assert_equals(s.constraint, nil)
        s:alter{constraint='tuple_constr1'}
        t.assert_equals(s.constraint, {tuple_constr1 = box.func.tuple_constr1.id})
        t.assert_equals(s:replace{1, 2, 3}, {1, 2, 3})
        t.assert_error_msg_content_equals(
            "Check constraint 'tuple_constr1' failed for a tuple",
            function() s:replace{100, 2, 3} end
        )
    end, {engine})

    local function check_references()
        cg.server:exec(function()
            t.assert_error_msg_content_equals(
                "Can't drop function " .. box.func.tuple_constr1.id ..
                ": function is referenced by constraint",
                function() box.func.tuple_constr1:drop() end
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

        t.assert_equals(s.constraint, {tuple_constr1 = box.func.tuple_constr1.id})
        s:alter{constraint={check2='tuple_constr2'}}
        t.assert_equals(s.constraint, {check2 = box.func.tuple_constr2.id})
        box.func.tuple_constr1:drop()
        t.assert_error_msg_content_equals(
            "Can't drop function " .. box.func.tuple_constr2.id ..
            ": function is referenced by constraint",
            function() box.func.tuple_constr2:drop() end
        )

        t.assert_equals(s:replace{100, 2, 3}, {100, 2, 3})
        t.assert_error_msg_content_equals(
            "Check constraint 'check2' failed for a tuple",
            function() s:replace{1, 200, 3} end
        )
        t.assert_equals(s:replace{1, 2, 300}, {1, 2, 300})

        t.assert_error_msg_content_equals(
            "Check constraint 'check3' failed for a tuple",
            function() s:alter{constraint = {check2='tuple_constr2',
                                             check3='tuple_constr3'}} end
        )
        t.assert_error_msg_content_equals(
            "Check constraint 'check2' failed for a tuple",
            function() s:replace{1, 200, 3} end
        )

        t.assert_equals(s:select{}, {{1, 2, 300}, {100, 2, 3}})

    end, {engine})
end

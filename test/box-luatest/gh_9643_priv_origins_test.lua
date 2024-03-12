local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test_origins_in_privs = function(cg)
    cg.server:exec(function()
        local bit = require('bit')
        box.schema.user.create('myuser')
        box.schema.space.create('myspace')
        local uid = box.space._user.index.name:get('myuser').id
        local sid = box.space._space.index.name:get('myspace').id
        local grant = box.schema.user.grant
        local revoke = box.schema.user.revoke
        local _priv = box.space._priv
        t.assert_equals(_priv:get({uid, 'space', sid}), nil)

        local function get_privilege_and_origins()
            local tuple = _priv:get({uid, 'space', sid})
            local privilege = tuple.privilege
            local origins = nil
            if tuple[6] ~= nil then
                origins = tuple[6].origins
            end
            return {privilege, origins}
        end

        -- Check grant with origins.
        local exp_priv = box.priv.R
        local exp_orig = nil
        grant('myuser', 'read', 'space', 'myspace')
        t.assert_equals(get_privilege_and_origins(), {exp_priv, exp_orig})

        exp_priv = bit.bor(box.priv.W, box.priv.R)
        exp_orig = {
            [''] = box.priv.R,
            one = box.priv.W,
        }
        grant('myuser', 'write', 'space', 'myspace', {_origin = 'one'})
        t.assert_equals(get_privilege_and_origins(), {exp_priv, exp_orig})

        exp_priv = bit.bor(box.priv.D, box.priv.W, box.priv.R)
        exp_orig = {
            [''] = bit.bor(box.priv.R, box.priv.D),
            one = box.priv.W,
        }
        grant('myuser', 'drop', 'space', 'myspace')
        t.assert_equals(get_privilege_and_origins(), {exp_priv, exp_orig})

        -- Make sure if_not_exists is set, grant() works as expected.
        exp_priv = bit.bor(box.priv.D, box.priv.W, box.priv.R)
        exp_orig = {
            [''] = bit.bor(box.priv.R, box.priv.D),
            one = bit.bor(box.priv.W, box.priv.D),
        }
        grant('myuser', 'drop', 'space', 'myspace',
              {_origin = 'one', if_not_exists = true})
        t.assert_equals(get_privilege_and_origins(), {exp_priv, exp_orig})

        local ok, err = pcall(grant, 'myuser', 'read', 'space', 'myspace',
                              {_origin = 1})
        local exp_err = "Illegal parameters, options parameter '_origin' " ..
            "should be of type 'string'"
        t.assert(not ok)
        t.assert_equals(err.message, exp_err)
        t.assert_equals(get_privilege_and_origins(), {exp_priv, exp_orig})

        ok, err = pcall(grant, 'myuser', 'read', 'space', 'myspace')
        exp_err = "User 'myuser' already has read access on space 'myspace'"
        t.assert(not ok)
        t.assert_equals(err.message, exp_err)
        t.assert_equals(get_privilege_and_origins(), {exp_priv, exp_orig})

        ok, err = pcall(grant, 'myuser', 'write', 'space', 'myspace',
                        {_origin = 'one'})
        exp_err = "User 'myuser' already has write access on space 'myspace'"
        t.assert(not ok)
        t.assert_equals(err.message, exp_err)
        t.assert_equals(get_privilege_and_origins(), {exp_priv, exp_orig})

        -- Check that if_not_exists is set, error is not thrown.
        ok, err = pcall(grant, 'myuser', 'write', 'space', 'myspace',
                        {_origin = 'one', if_not_exists = true})
        t.assert(ok)
        t.assert_equals(err, nil)
        t.assert_equals(get_privilege_and_origins(), {exp_priv, exp_orig})

        -- Check revoke with origins.
        ok, err = pcall(revoke, 'myuser', 'read', 'space', 'myspace',
                        {_origin = true})
        exp_err = "Illegal parameters, options parameter '_origin' should " ..
            "be of type 'string'"
        t.assert(not ok)
        t.assert_equals(err.message, exp_err)
        t.assert_equals(get_privilege_and_origins(), {exp_priv, exp_orig})

        ok, err = pcall(revoke, 'myuser', 'write', 'space', 'myspace')
        exp_err = "User 'myuser' does not have write access on space " ..
                  "'myspace' provided by default origin"
        t.assert(not ok)
        t.assert_equals(err.message, exp_err)
        t.assert_equals(get_privilege_and_origins(), {exp_priv, exp_orig})

        ok, err = pcall(revoke, 'myuser', 'read', 'space', 'myspace',
                        {_origin = 'one'})
        exp_err = "User 'myuser' does not have read access on space " ..
                  "'myspace' provided by one origin"
        t.assert(not ok)
        t.assert_equals(err.message, exp_err)
        t.assert_equals(get_privilege_and_origins(), {exp_priv, exp_orig})

        -- Check that if_exists is set, error is not thrown.
        ok, err = pcall(revoke, 'myuser', 'read', 'space', 'myspace',
                        {_origin = 'one', if_exists = true})
        t.assert(ok)
        t.assert_equals(err, nil)
        t.assert_equals(get_privilege_and_origins(), {exp_priv, exp_orig})

        exp_priv = bit.bor(box.priv.D, box.priv.R)
        exp_orig = {
            [''] = bit.bor(box.priv.D, box.priv.R),
            one = box.priv.D,
        }
        revoke('myuser', 'write', 'space', 'myspace', {_origin = 'one'})
        t.assert_equals(get_privilege_and_origins(), {exp_priv, exp_orig})

        exp_priv = box.priv.D
        exp_orig = {
            [''] = box.priv.D,
            one = box.priv.D,
        }
        revoke('myuser', 'read', 'space', 'myspace')
        t.assert_equals(get_privilege_and_origins(), {exp_priv, exp_orig})

        -- Make sure if_exists is set, revoke() works as expected.
        exp_priv = box.priv.D
        exp_orig = nil
        revoke('myuser', 'drop', 'space', 'myspace',
               {_origin = 'one', if_exists = true})
        t.assert_equals(get_privilege_and_origins(), {exp_priv, exp_orig})

        revoke('myuser', 'drop', 'space', 'myspace')
        t.assert_equals(_priv:get({uid, 'space', sid}), nil)
    end)
end

g.test_origins_in_roles = function(cg)
    cg.server:exec(function()
        box.schema.role.create('myrole')
        box.schema.role.create('somerole')
        local rid = box.space._user.index.name:get('myrole').id
        local tid = box.space._user.index.name:get('somerole').id
        local grant = box.schema.role.grant
        local revoke = box.schema.role.revoke
        local _priv = box.space._priv
        t.assert_equals(_priv:get({rid, 'role', tid}), nil)

        local function get_origins()
            local tuple = _priv:get({rid, 'role', tid})
            t.assert_equals(tuple.privilege, box.priv.X)
            if tuple[6] ~= nil then
                return tuple[6].origins
            end
            return nil
        end

        -- Check grant with origins.
        local exp_orig = nil
        grant('myrole', 'somerole', nil, nil)
        t.assert_equals(get_origins(), exp_orig)

        exp_orig = {
            [''] = box.priv.X,
            two = box.priv.X,
        }
        grant('myrole', 'somerole', nil, nil, {_origin = 'two'})
        t.assert_equals(get_origins(), exp_orig)

        local ok, err = pcall(grant, 'myrole', 'somerole')
        local exp_err = "User 'myrole' already has role 'somerole'"
        t.assert(not ok)
        t.assert_equals(err.message, exp_err)
        t.assert_equals(get_origins(), exp_orig)

        ok, err = pcall(grant, 'myrole', 'somerole', nil, nil,
                        {_origin = 'two'})
        t.assert(not ok)
        t.assert_equals(err.message, exp_err)
        t.assert_equals(get_origins(), exp_orig)

        -- Check revoke with origins.
        exp_orig = {
            two = box.priv.X,
        }
        revoke('myrole', 'somerole')
        t.assert_equals(get_origins(), exp_orig)

        ok, err = pcall(revoke, 'myrole', 'somerole')
        exp_err = "User 'myrole' does not have role 'somerole' provided by " ..
                  "default origin"
        t.assert(not ok)
        t.assert_equals(err.message, exp_err)
        t.assert_equals(get_origins(), exp_orig)
    end)
end

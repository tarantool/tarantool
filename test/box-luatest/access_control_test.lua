local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_each(function(cg)
    cg.server = server:new()
    cg.server:start()
end)

g.after_each(function(cg)
    cg.server:drop()
end)

-- Make sure requested privileges granted properly.
g.test_grant = function(cg)
    cg.server:exec(function()
        local luatest_helpers = require('luatest.helpers')
        local access = require('access_control')

        box.session.su('admin')
        t.assert_equals(access.get(), {})

        -- Request new privileges and make sure they have been granted.
        local credentials = {
            users = {
                guest = {
                    privileges = {{
                        permissions = {'drop'},
                        spaces = {'_space'},
                    }},
                    roles = {'super'},
                }
            }
        }
        access.set('one', credentials)
        t.assert_equals(access.get(), {one = credentials})
        t.assert_equals(access.get('one'), credentials)
        local uid = box.space._user.index.name:get('guest').id
        local public_id = box.space._user.index.name:get('public').id
        local super_id = box.space._user.index.name:get('super').id
        local exp_space = {
            {1, uid, 'space', box.schema.SPACE_ID, box.priv.D},
        }
        local exp_role = {
            {1, uid, 'role', public_id, box.priv.X},
            {1, uid, 'role', super_id, box.priv.X},
        }
        luatest_helpers.retrying({}, function()
            t.assert_equals(box.space._priv:select({uid, 'space'}), exp_space)
            t.assert_equals(box.space._priv:select({uid, 'role'}), exp_role)
        end)
    end)
end

-- Make sure that the user or role for which the privilege is being requested is
-- created if it does not already exist.
g.test_user_role = function(cg)
    cg.server:exec(function()
        local luatest_helpers = require('luatest.helpers')
        local access = require('access_control')

        box.session.su('admin')
        t.assert_equals(access.get(), {})
        t.assert_equals(#box.space._user.index.name:select('somerole'), 0)
        t.assert_equals(#box.space._user.index.name:select('someone'), 0)

        -- Request new privileges for a non-existent user and make sure that the
        -- user is created and privileges are granted.
        local credentials = {
            roles = {
                somerole = {
                    privileges = {{
                        permissions = {'write'},
                        spaces = {'_space'},
                    }},
                },
            },
            users = {
                someone = {},
            }
        }
        access.set('one', credentials)
        t.assert_equals(access.get(), {one = credentials})
        t.assert_equals(access.get('one'), credentials)
        luatest_helpers.retrying({}, function()
            t.assert_equals(#box.space._user.index.name:select('somerole'), 1)
            t.assert_equals(#box.space._user.index.name:select('someone'), 1)
            local uid = box.space._user.index.name:get('somerole').id
            local exp = {{1, uid, 'space', box.schema.SPACE_ID, box.priv.W}}
            t.assert_equals(box.space._priv:select({uid}), exp)
        end)
    end)
end

-- Make sure that the user or role for which the privilege is being requested is
-- created if it does not already exist.
g.test_non_existing_object = function(cg)
    cg.server:exec(function()
        local luatest_helpers = require('luatest.helpers')
        local access = require('access_control')

        box.session.su('admin')
        t.assert_equals(access.get(), {})

        -- Request access to a non-existent object.
        local credentials = {
            users = {
                guest = {
                    privileges = {{
                        permissions = {'write'},
                        spaces = {'asd'},
                    }},
                },
            },
        }
        access.set('one', credentials)
        t.assert_equals(access.get(), {one = credentials})
        t.assert_equals(access.get('one'), credentials)
        t.assert_equals(#box.space._space.index.name:select('asd'), 0)
        box.schema.space.create('asd')
        local uid = box.space._user.index.name:get('guest').id
        luatest_helpers.retrying({}, function()
            t.assert_equals(#box.space._space.index.name:select('asd'), 1)
            local exp = {{1, uid, 'space', box.space.asd.id, box.priv.W}}
            t.assert_equals(box.space._priv:select({uid, 'space'}), exp)
        end)
    end)
end

-- Make sure that the requested privileges will be granted when the instance
-- becomes RW.
g.test_ro_rw = function(cg)
    cg.server:exec(function()
        local luatest_helpers = require('luatest.helpers')
        local access = require('access_control')

        box.session.su('admin')
        box.cfg{read_only = true}
        t.assert_equals(access.get(), {})
        t.assert_equals(#box.space._user.index.name:select('someone'), 0)

        -- Request new privileges for a non-existent user and make sure that the
        -- nothing happens because instance in RO mode.
        local credentials = {
            users = {
                someone = {
                    privileges = {{
                        permissions = {'write'},
                        spaces = {'_space'},
                    }},
                }
            }
        }
        access.set('one', credentials)
        t.assert_equals(access.get(), {one = credentials})
        t.assert_equals(access.get('one'), credentials)
        t.assert_equals(#box.space._user.index.name:select('someone'), 0)

        -- Chenge mode to RW and make sure that the user is created and
        -- privileges are granted.
        box.cfg{read_only = false}
        luatest_helpers.retrying({}, function()
            t.assert_equals(#box.space._user.index.name:select('someone'), 1)
            local uid = box.space._user.index.name:get('someone').id
            local exp = {{1, uid, 'space', box.schema.SPACE_ID, box.priv.W}}
            t.assert_equals(box.space._priv:select({uid, 'space'}), exp)
        end)
    end)
end

g.test_intersect_requests = function(cg)
    cg.server:exec(function()
        local luatest_helpers = require('luatest.helpers')
        local access = require('access_control')
        local bit = require('bit')

        box.session.su('admin')
        t.assert_equals(access.get(), {})
        t.assert_equals(#box.space._user.index.name:select('somerole'), 0)

        -- Request new privileges and make sure thay were granted and role was
        -- created.
        local credentials_one = {
            roles = {
                somerole = {
                    privileges = {{
                        permissions = {'drop'},
                        spaces = {'_space'},
                    }},
                },
            },
        }
        access.set('one', credentials_one)
        t.assert_equals(access.get(), {one = credentials_one})
        t.assert_equals(access.get('one'), credentials_one)
        luatest_helpers.retrying({}, function()
            t.assert_equals(#box.space._user.index.name:select('somerole'), 1)
        end)
        local uid = box.space._user.index.name:get('somerole').id

        luatest_helpers.retrying({}, function()
            local exp = {{1, uid, 'space', box.schema.SPACE_ID, box.priv.D}}
            t.assert_equals(box.space._priv:select({uid, 'space'}), exp)
        end)

        -- Request more privileges for the same role and make sure they were
        -- granted.
        local credentials_two = {
            roles = {
                somerole = {
                    privileges = {
                        {
                            permissions = {'write'},
                            spaces = {'_index'},
                        },
                        {
                            permissions = {'read'},
                            spaces = {'_space'},
                        },
                    },
                }
            }
        }
        access.set('two', credentials_two)
        t.assert_equals(access.get(), {
            one = credentials_one,
            two = credentials_two,
        })
        t.assert_equals(access.get('one'), credentials_one)
        t.assert_equals(access.get('two'), credentials_two)
        luatest_helpers.retrying({}, function()
            local exp = {
                {1, uid, 'space', box.schema.SPACE_ID,
                 bit.bor(box.priv.R, box.priv.D)},
                {1, uid, 'space', box.schema.INDEX_ID, box.priv.W},
            }
            t.assert_equals(box.space._priv:select({uid}), exp)
        end)

        -- Remove request some request for the credentials and make sure that
        -- some privileges were revoked.
        access.drop('one')
        t.assert_equals(access.get(), {two = credentials_two})

        local exp = {
            {1, uid, 'space', box.schema.SPACE_ID, box.priv.R},
            {1, uid, 'space', box.schema.INDEX_ID, box.priv.W},
        }
        luatest_helpers.retrying({}, function()
            t.assert_equals(box.space._priv:select({uid}), exp)
        end)

        -- Remove all requests and make sure that previous privileges were not
        -- changed.
        access.drop('two')
        t.assert_equals(box.space._priv:select({uid}), exp)
    end)
end

g.test_errors = function(cg)
    cg.server:exec(function()
        local access = require('access_control')

        local exp_err = 'Name of privilege request source must be a string'
        t.assert_error_msg_equals(exp_err, access.set)
        t.assert_error_msg_equals(exp_err, access.set, 1)
        exp_err = 'Wrong format of privilege requests'
        t.assert_error_msg_equals(exp_err, access.set, 'one', 1)
        exp_err = '[instance_config] credentials: Unexpected field "a"'
        t.assert_error_msg_equals(exp_err, access.set, 'one', {a = 1})

        exp_err = 'Name of privilege request source must be a string'
        t.assert_error_msg_equals(exp_err, access.drop)
        t.assert_error_msg_equals(exp_err, access.drop, 1)

        exp_err = 'Name of privilege request source must be a string or nil'
        t.assert_error_msg_equals(exp_err, access.get, 1)
    end)
end

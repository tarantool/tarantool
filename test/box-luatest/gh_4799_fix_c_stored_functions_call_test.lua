local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
    -- Set the CPATH for all tests. Register the test function.
    cg.server:exec(function()
        local build_path = os.getenv('BUILDDIR')
        local lib_suffix = jit.os == 'OSX' and 'dylib' or 'so'
        package.cpath = ('%s/test/box-luatest/?.%s;%s'):format(
            build_path, lib_suffix, package.path
        )

        box.schema.func.create('gh_4799_lib.multires', {language = 'C'})
    end)
end)

g.after_all(function(cg)
    cg.server:stop()
end)

g.test_compat_c_func_iproto_multireturn = function()
    g.server:exec(function()
        local net = require('net.box')
        local compat = require('compat')

        -- This function returns `true` and `-1` as the results.
        local C_FUNC = 'gh_4799_lib.multires'
        local EXPECTED_NEW = {true, -1}
        local EXPECTED_OLD = {EXPECTED_NEW}

        -- This compatibility option doesn't expect to be switched
        -- on the fly. At least, there are no guarantees.
        -- So just use one connection here.
        local connect = net:connect(box.cfg.listen)

        -- Test that the old behaviour wraps returning values.
        compat.c_func_iproto_multireturn = 'old'
        t.assert_equals({connect:call(C_FUNC)}, EXPECTED_OLD)

        -- Test that the new behaviour returns multiresults.
        compat.c_func_iproto_multireturn = 'new'
        t.assert_equals({connect:call(C_FUNC)}, EXPECTED_NEW)

        -- Test the default behaviour.
        compat.c_func_iproto_multireturn = 'default'
        t.assert_equals({connect:call(C_FUNC)}, EXPECTED_NEW)
    end)
end

-- Test for consistent results when calling the C function locally
-- and via iproto.
g.test_c_func_consistent_results = function()
    g.server:exec(function()
        local net = require('net.box')
        -- This function returns `true` and `-1` as the results.
        local C_FUNC = 'gh_4799_lib.multires'
        local connect = net:connect(box.cfg.listen)
        t.assert_equals({connect:call(C_FUNC)}, {box.func[C_FUNC]:call()})
    end)
end

local server = require('luatest.server')
local t = require('luatest')

local WARNING_PATTERN = 'W> box%.session%.push is deprecated. ' ..
                        'Consider using box%.broadcast instead%.$'

local g = t.group('box_session_push_deprecation', t.helpers.matrix({
    api = {'lua', 'c'},
    compat = {'old', 'new'},
}))

g.before_all(function(cg)
    cg.server = server:new()
end)

g.before_each(function(cg)
    cg.server:start()
    cg.server:exec(function(params)
        local func
        if params.api == 'lua' then
            func = box.session.push
        elseif params.api == 'c' then
            local ffi = require('ffi')
            local msgpack = require('msgpack')
            ffi.cdef([[
                int box_session_push(const char *data, const char *data_end);
            ]])
            func = function(value)
                local data = msgpack.encode(value)
                if ffi.C.box_session_push(
                        ffi.cast('const char *', data),
                        ffi.cast('const char *', data) + #data) ~= 0 then
                    box.error()
                end
            end
        end
        t.assert(func)
        rawset(_G, 'box_session_push', func)
    end, {cg.params})
end)

g.after_each(function(cg)
    cg.server:stop()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test = function(cg)
    cg.server:exec(function(params)
        local compat = require('compat')
        compat.box_session_push_deprecation = params.compat
    end, {cg.params})

    t.assert_not(cg.server:grep_log(WARNING_PATTERN),
                 'No deprecation warning in the log initially')

    -- Test box.session.push. It shouldn't work with the new behavior.
    -- The warning should be logged in any case.
    cg.server:exec(function()
        local compat = require('compat')
        local box_session_push = rawget(_G, 'box_session_push')
        local ok, err = pcall(box_session_push, 'foo')
        t.assert_equals(ok, compat.box_session_push_deprecation:is_old(),
                        'box.session.push status')
        if not ok then
            t.assert_equals(tostring(err), 'box.session.push is deprecated',
                            'box.session.push error')
        end
    end)
    t.assert(cg.server:grep_log(WARNING_PATTERN),
             'Deprecation warning is logged')

    -- Check that the warning is logged only once.
    cg.server:exec(function()
        local log = require('log')
        for _ = 1, 10 do
            log.warn(string.rep('x', 1000))
        end
        local compat = require('compat')
        local box_session_push = rawget(_G, 'box_session_push')
        local ok, err = pcall(box_session_push, 'foo')
        t.assert_equals(ok, compat.box_session_push_deprecation:is_old(),
                        'box.session.push status')
        if not ok then
            t.assert_equals(tostring(err), 'box.session.push is deprecated',
                            'box.session.push error')
        end
    end)
    t.assert_not(cg.server:grep_log(WARNING_PATTERN, 5000),
                 'Deprecation warning is logged only once')
end

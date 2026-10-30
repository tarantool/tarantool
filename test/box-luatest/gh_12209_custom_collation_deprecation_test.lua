local t = require('luatest')
local server = require('luatest.server')

local DEPRECATION_WARNING = "Usage of custom collations is deprecated%. " ..
                            "Tarantool 4%.0 and newer will not start if " ..
                            "there are custom collations in the database%."
local CREATION_WARNING = "Creating custom collation .*"

local g = t.group()

g.before_each(function(cg)
    cg.server = server:new()
    cg.server:start()
end)

g.after_each(function(cg)
    cg.server:drop()
end)

g.test = function(cg)
    -- No warning initially.
    t.assert_not(cg.server:grep_log(DEPRECATION_WARNING))
    t.assert_not(cg.server:grep_log(CREATION_WARNING))

    -- No warning if the created collation is predefined.
    cg.server:exec(function()
        box.internal.collation.create('test1', 'ICU', '',
                                      {strength = 'primary'})
        box.internal.collation.create('test2', 'ICU', 'af',
                                      {strength = 'secondary'})
    end)
    t.assert_not(cg.server:grep_log(DEPRECATION_WARNING))
    t.assert_not(cg.server:grep_log(CREATION_WARNING))

    -- Warning if the created collation is not predefined.
    cg.server:exec(function()
        box.internal.collation.create('test3', 'ICU', '',
                                      {strength = 'secondary'})
    end)
    t.assert(cg.server:grep_log(DEPRECATION_WARNING))
    t.assert_str_contains(cg.server:grep_log(CREATION_WARNING),
                          ".* locale: '', .* strength: 2", true)

    -- Deprecation warning is logged once while creation warning
    -- is logged each time.
    cg.server:exec(function()
        local log = require('log')
        for _ = 1, 10 do
            log.warn(string.rep('x', 1000))
        end
        box.internal.collation.create('test4', 'ICU', 'af',
                                      {alternate_handling = 'shifted'})
    end)
    t.helpers.retrying({}, function()
        t.assert_not(cg.server:grep_log(DEPRECATION_WARNING, 5000))
        t.assert(cg.server:grep_log(CREATION_WARNING))
    end)

    -- Warnings are logged during recovery.
    cg.server:restart()
    t.assert(cg.server:grep_log(DEPRECATION_WARNING))
    t.assert(cg.server:grep_log(CREATION_WARNING))
end

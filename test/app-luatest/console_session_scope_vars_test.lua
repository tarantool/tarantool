local t = require('luatest')
local it = require('test.interactive_tarantool')
local cbuilder = require('test.config-luatest.cbuilder')
local cluster = require('test.config-luatest.cluster')

local g = t.group()

g.before_all(cluster.init)
g.after_each(cluster.drop)
g.after_all(cluster.clean)

local function close_on_demand(g, name)
    if g[name] ~= nil then
        g[name]:close()
        g[name] = nil
    end
end

g.after_each(function(g)
    close_on_demand(g, 'it')
    close_on_demand(g, 'it_1')
    close_on_demand(g, 'it_2')
end)

g.test_compat_default = function(g)
    local config = cbuilder.new()
        :add_instance('i-001', {})
        :config()
    local cluster = cluster.new(g, config)
    cluster:start()

    cluster['i-001']:exec(function()
        local compat = require('compat')

        assert(compat.console_session_scope_vars:is_old())
    end)
end

g.test_local_old = function(g)
    g.it = it.new()

    g.it:roundtrip("require('compat').console_session_scope_vars = 'old'")

    -- Non-local assignment is written to globals.
    g.it:roundtrip('x = 5')
    g.it:roundtrip('x', 5)
    g.it:roundtrip("rawget(_G, 'x')", 5)

    -- A global is readable without explicit _G.
    g.it:roundtrip('_G.y = 6')
    g.it:roundtrip('_G.y', 6)
    g.it:roundtrip('y', 6)

    -- A last write wins.
    g.it:roundtrip('x = 5')
    g.it:roundtrip('_G.x = 7')
    g.it:roundtrip('x', 7)
    g.it:roundtrip('x = 8')
    g.it:roundtrip('x', 8)
    g.it:roundtrip('_G.x = 9')
    g.it:roundtrip('x', 9)
    g.it:roundtrip('_G.x', 9)
end

g.test_local_new = function(g)
    g.it = it.new()

    g.it:roundtrip("require('compat').console_session_scope_vars = 'new'")

    -- Non-local assignment doesn't affect globals.
    g.it:roundtrip('x = 5')
    g.it:roundtrip('x', 5)
    g.it:roundtrip("rawget(_G, 'x')", nil)

    -- A global is readable without explicit _G.
    g.it:roundtrip('_G.y = 6')
    g.it:roundtrip('_G.y', 6)
    g.it:roundtrip('y', 6)

    -- A session scope variable is preferred.
    g.it:roundtrip('x = 5')
    g.it:roundtrip('_G.x = 7')
    g.it:roundtrip('x', 5)
    g.it:roundtrip('x = 8')
    g.it:roundtrip('x', 8)
    g.it:roundtrip('_G.x = 9')
    g.it:roundtrip('x', 8)
    g.it:roundtrip('_G.x', 9)
end

g.test_remote_old = function(g)
    local config = cbuilder.new()
        :set_global_option('compat.console_session_scope_vars', 'old')
        :add_instance('i-001', {})
        :config()
    local cluster = cluster.new(g, config)
    cluster:start()

    g.it_1 = it.connect(cluster['i-001'])
    g.it_2 = it.connect(cluster['i-001'])

    -- Write the same named variables.
    g.it_1:roundtrip('x = 1')
    g.it_2:roundtrip('x = 2')

    -- The variable is shared.
    g.it_1:roundtrip('x', 2)
    g.it_2:roundtrip('x', 2)

    -- And it is written to globals.
    g.it_1:roundtrip("rawget(_G, 'x')", 2)
end

g.test_remote_new = function(g)
    local config = cbuilder.new()
        :set_global_option('compat.console_session_scope_vars', 'new')
        :add_instance('i-001', {})
        :config()
    local cluster = cluster.new(g, config)
    cluster:start()

    g.it_1 = it.connect(cluster['i-001'])
    g.it_2 = it.connect(cluster['i-001'])

    -- Write the same named variables.
    g.it_1:roundtrip('x = 1')
    g.it_2:roundtrip('x = 2')

    -- Each console session has its own variable with this name.
    g.it_1:roundtrip('x', 1)
    g.it_2:roundtrip('x', 2)

    -- There is no global variable with such a name.
    g.it_1:roundtrip("rawget(_G, 'x')", nil)
    g.it_2:roundtrip("rawget(_G, 'x')", nil)
end

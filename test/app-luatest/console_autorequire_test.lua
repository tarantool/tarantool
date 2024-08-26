local t = require('luatest')
local it = require('test.interactive_tarantool')
local cbuilder = require('luatest.cbuilder')
local cluster = require('test.config-luatest.cluster')

local g = t.group()

local autorequire_list = {
    'clock',
    'compat',
    'config',
    'datetime',
    'decimal',
    'ffi',
    'fiber',
    'fio',
    'fun',
    'json',
    'log',
    'msgpack',
    'popen',
    'uuid',
    'varbinary',
    'yaml',
}

local function assert_no_autorequire(it)
    it:roundtrip("require('strict').off()")

    for _, m in ipairs(autorequire_list) do
        it:roundtrip(m, nil)
    end

    it:roundtrip("require('strict').on()")
end

local function assert_autorequire(it)
    for _, m in ipairs(autorequire_list) do
        it:roundtrip(('type(%s)'):format(m), 'table')
    end
end

g.before_all(cluster.init)
g.after_each(cluster.drop)
g.after_all(cluster.clean)

g.after_each(function(g)
    if g.it ~= nil then
        g.it:close()
        g.it = nil
    end
end)

g.test_local_old_no_autorequire = function(g)
    g.it = it.new()

    g.it:roundtrip("require('compat').console_session_scope_vars = 'old'")
    assert_no_autorequire(g.it)
end

g.test_local_new_autorequire = function(g)
    g.it = it.new()

    g.it:roundtrip("require('compat').console_session_scope_vars = 'new'")
    assert_autorequire(g.it)
end

g.test_remote_old_no_autorequire = function(g)
    local config = cbuilder:new()
        :set_global_option('compat.console_session_scope_vars', 'old')
        :add_instance('i-001', {})
        :config()
    local cluster = cluster.new(g, config)
    cluster:start()

    g.it = it.connect(cluster['i-001'])
    assert_no_autorequire(g.it)
end

g.test_remote_new_autorequire = function(g)
    local config = cbuilder:new()
        :set_global_option('compat.console_session_scope_vars', 'new')
        :add_instance('i-001', {})
        :config()
    local cluster = cluster.new(g, config)
    cluster:start()

    g.it = it.connect(cluster['i-001'])
    assert_autorequire(g.it)
end

-- Add a variable into the initial console environment.
g.test_remote_new_extend_env = function(g)
    local config = cbuilder:new()
        :set_global_option('compat.console_session_scope_vars', 'new')
        :add_instance('i-001', {})
        :config()
    local cluster = cluster.new(g, config)
    cluster:start()

    cluster['i-001']:exec(function()
        local console = require('console')
        local initial_env = console.initial_env()
        initial_env.foo = 42
    end)

    g.it = it.connect(cluster['i-001'])
    g.it:roundtrip('foo', 42)

    -- All the autorequired modules are kept.
    assert_autorequire(g.it)
end

g.test_remote_new_set_env_illegal_param = function(g)
    local config = cbuilder:new()
        :set_global_option('compat.console_session_scope_vars', 'new')
        :add_instance('i-001', {})
        :config()
    local cluster = cluster.new(g, config)
    cluster:start()

    cluster['i-001']:exec(function()
        local console = require('console')

        local exp_err = 'console.set_initial_env: expected table or nil, ' ..
            'got number'
        t.assert_error_msg_content_equals(exp_err, console.set_initial_env, 42)
    end)
end

-- Set a user defined initial console environment.
g.test_remote_new_set_env = function(g)
    local config = cbuilder:new()
        :set_global_option('compat.console_session_scope_vars', 'new')
        :add_instance('i-001', {})
        :config()
    local cluster = cluster.new(g, config)
    cluster:start()

    -- Replace the initial environment.
    cluster['i-001']:exec(function()
        local console = require('console')
        console.set_initial_env({foo = 42})
    end)

    -- Verify that console.initial_env() reflects the change.
    cluster['i-001']:exec(function()
        local console = require('console')
        t.assert_equals(console.initial_env(), {foo = 42})
    end)

    -- Verify that the new environment is in effect.
    g.it = it.connect(cluster['i-001'])
    g.it:roundtrip('foo', 42)

    -- Verify that a console environment has no autorequired
    -- modules anymore.
    assert_no_autorequire(g.it)

    g.it:close()
    g.it = nil

    -- Drop the initial environment to default.
    cluster['i-001']:exec(function()
        local console = require('console')
        console.set_initial_env(nil)
    end)

    -- Verify that all the autorequired modules are there.
    g.it = it.connect(cluster['i-001'])
    assert_autorequire(g.it)
end

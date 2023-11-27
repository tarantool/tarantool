local server = require('luatest.server')
local t = require('luatest')

local g = t.group('gh_9131')

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
    cg.server:exec(function()
        package.cpath = ('%s/test/box-luatest/?.%s;%s'):format(
            os.getenv('BUILDDIR'), jit.os == 'OSX' and 'dylib' or 'so',
            package.path)
        box.schema.func.create('gh_9131_lib.c_func_echo', {language = 'c'})
        box.schema.func.create('gh_9131_lib.c_func_error', {language = 'c'})
        box.schema.func.create('gh_9131_lib.c_func_undef', {language = 'c'})
        box.schema.func.create('c_func_undef', {language = 'c'})
        rawset(_G, 'lua_func_echo', function(...) return ... end)
        box.schema.func.create('lua_func_echo')
        rawset(_G, 'lua_func_error', function() return error('test') end)
        box.schema.func.create('lua_func_error')
        box.schema.func.create('lua_func_undef')
        box.schema.func.create('stored_lua_func_echo', {
            body = [[function(...) return ... end]],
        })
        box.schema.func.create('stored_lua_func_error', {
            body = [[function() return error('test') end]],
        })
        rawset(_G, 'unreg_lua_func_echo',
               function(...) return ... end)
        rawset(_G, 'unreg_lua_func_error',
               function() return error('test') end)
    end)
end)

g.after_all(function(cg)
    cg.server:drop()
    cg.server = nil
end)

g.test_c_func = function(cg)
    cg.server:exec(function()
        local self = require('net.box').self
        t.assert_equals({self:call('gh_9131_lib.c_func_echo')}, {})
        t.assert_equals({self:call('gh_9131_lib.c_func_echo', {})}, {})
        t.assert_equals({self:call('gh_9131_lib.c_func_echo', {1})}, {1})
        t.assert_equals({self:call('gh_9131_lib.c_func_echo', {1LL})}, {1})
        t.assert_equals({self:call('gh_9131_lib.c_func_echo', {1, 2, 3})},
                        {1, 2, 3})
        t.assert_error_msg_equals('test', self.call, self,
                                  'gh_9131_lib.c_func_error')
        t.assert_error_msg_equals(
            "Procedure 'gh_9131.c_func_undef' is not defined",
            self.call, self, 'gh_9131.c_func_undef')
        t.assert_error_msg_equals(
            "Failed to dynamically load module 'c_func_undef': " ..
            "module not found", self.call, self, 'c_func_undef')
    end)
end

g.test_lua_func = function(cg)
    cg.server:exec(function()
        local self = require('net.box').self
        t.assert_equals({self:call('lua_func_echo')}, {})
        t.assert_equals({self:call('lua_func_echo', {})}, {})
        t.assert_equals({self:call('lua_func_echo', {1})}, {1})
        t.assert_equals({self:call('lua_func_echo', {1LL})}, {1})
        t.assert_equals({self:call('lua_func_echo', {1, 2, 3})}, {1, 2, 3})
        t.assert_error_msg_equals('test', self.call, self, 'lua_func_error')
        t.assert_error_msg_equals("Procedure 'lua_func_undef' is not defined",
                                  self.call, self, 'lua_func_undef')
    end)
end

g.test_stored_lua_func = function(cg)
    cg.server:exec(function()
        local self = require('net.box').self
        t.assert_equals({self:call('stored_lua_func_echo')}, {})
        t.assert_equals({self:call('stored_lua_func_echo', {})}, {})
        t.assert_equals({self:call('stored_lua_func_echo', {1})}, {1})
        t.assert_equals({self:call('stored_lua_func_echo', {1LL})}, {1})
        t.assert_equals({self:call('stored_lua_func_echo', {1, 2, 3})},
                        {1, 2, 3})
        t.assert_error_msg_equals('test', self.call, self,
                                  'stored_lua_func_error')
    end)
end

g.test_unreg_lua_func = function(cg)
    cg.server:exec(function()
        local self = require('net.box').self
        t.assert_equals({self:call('unreg_lua_func_echo')}, {})
        t.assert_equals({self:call('unreg_lua_func_echo', {})}, {})
        t.assert_equals({self:call('unreg_lua_func_echo', {1})}, {1})
        t.assert_equals({self:call('unreg_lua_func_echo', {1LL})}, {1})
        t.assert_equals({self:call('unreg_lua_func_echo', {1, 2, 3})},
                        {1, 2, 3})
        t.assert_error_msg_equals('test', self.call, self,
                                  'unreg_lua_func_error')
        t.assert_error_msg_equals(
            "Procedure 'unreg_lua_func_undef' is not defined",
            self.call, self, 'unreg_lua_func_undef')
    end)
end

local g_local = t.group('gh_9131.local')

g_local.before_all(function()
    rawset(_G, 'local_func_echo', function(...) return ... end)
    rawset(_G, 'local_func_error', function() return error('test') end)
end)

g_local.after_all(function()
    rawset(_G, 'local_func_echo', nil)
    rawset(_G, 'local_func_error', nil)
end)

g_local.test_box_not_configured = function()
    local self = require('net.box').self
    t.assert_equals({self:call('local_func_echo')}, {})
    t.assert_equals({self:call('local_func_echo', {})}, {})
    t.assert_equals({self:call('local_func_echo', {1})}, {1})
    t.assert_equals({self:call('local_func_echo', {1LL})}, {1})
    t.assert_equals({self:call('local_func_echo', {1, 2, 3})}, {1, 2, 3})
    t.assert_error_msg_equals('test', self.call, self, 'local_func_error')
end

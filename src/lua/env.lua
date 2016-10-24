local ffi = require('ffi')

ffi.cdef[[
    extern char **environ;

    int   setenv(const char *name, const char *value, int overwrite);
    int   unsetenv(const char *name);
    int   clearenv(void);
    char *getenv(const char *name);
]]

local environ = ffi.C.environ

local env = setmetatable({}, {
    __call = function()
        if not environ then
            return nil
        end
        local r = {}
        local i = 0
        while environ[i] ~= nil do
            local e = ffi.string(environ[i])
            local eq = e:find('=')
            if eq then
                r[e:sub(1, eq - 1)] = e:sub(eq + 1)
            end
            i = i + 1
        end
        return r
    end,
    __index = function(self, key)
        local var = ffi.C.getenv(key)
        if var == nil then
            return nil
        end
        return ffi.string(var)
    end,
    __newindex = function(self, key, value)
        local rv = nil
        if value ~= nil then
            rv = ffi.C.setenv(key, value, 1)
        else
            rv = ffi.C.unsetenv(key)
        end
        if rv == -1 then
            error(string.format('error %d: %s', errno(), errno.errstring()))
        end
    end
})

return env

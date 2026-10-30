-- A fiber can't use multiple transactions simultaneously;
-- i.e. [fiber] --? [transaction] in UML parlor.
--
-- This module provides a simple transaction proxy facility
-- to control multiple transactions at once. A proxy executes
-- statements in a worker fiber in order to overcome
-- "one transaction per fiber" limitation.
--
-- Ex:
-- proxy = require('txn_proxy').new()
-- proxy:begin()
-- proxy('box.space.test:replace{1, 42}')
-- proxy:commit() -- or proxy:rollback()

local ffi = require('ffi')
local yaml = require('yaml')
local fiber = require('fiber')
local console = require('console')

local array_mt = { __serialize = 'array' }

local mt = {
    __call = function(self, code_str)
        self.c1:put(code_str)
        local res = yaml.decode(self.c2:get())
        return type(res) == 'table' and setmetatable(res, array_mt) or res
    end,
    __index = {
        begin    = function(self) return self('box.begin()') end,
        commit   = function(self) return self('box.commit()') end,
        rollback = function(self) return self('box.rollback()') end,
        close    = function(self) self.c1:close(); self.c2:close() end
    }
}

local function fiber_main(c1, c2)
    local code_str = c1:get()
    if code_str then
        c2:put(console.eval(code_str))
        return fiber_main(c1, c2) -- tail call
    end
end

local function new_txn_proxy()
    local c1, c2 = fiber.channel(), fiber.channel()
    local function on_gc() c1:close(); c2:close() end
    fiber.create(fiber_main, c1, c2)
    return setmetatable({
        c1 = c1, c2 = c2,
        __gc = ffi.gc(ffi.new('char[1]'), on_gc)
    }, mt)
end

return { new = new_txn_proxy }

local ffi = require('ffi')
local tarantool = require('tarantool')

local asan = {}

ffi.cdef[[
void
__asan_poison_memory_region(void const volatile *addr, size_t size);
void
__asan_unpoison_memory_region(void const volatile *addr, size_t size);
int
__asan_address_is_poisoned(void const volatile *addr);
]]

if tarantool.build.asan then
    asan.poison_memory_region = function(start, size)
        ffi.C.__asan_poison_memory_region(start, size)
    end
    asan.unpoison_memory_region = function(start, size)
        ffi.C.__asan_unpoison_memory_region(start, size)
    end
    asan.memory_region_is_poisoned = function(start, size)
        for i = 0, size - 1 do
            if ffi.C.__asan_address_is_poisoned(start + i) == 0 then
                return false
            end
        end
        return true
    end
else
    asan.poison_memory_region = function() end
    asan.unpoison_memory_region = function() end
    asan.memory_region_is_poisoned = function() return false end
end

return asan
